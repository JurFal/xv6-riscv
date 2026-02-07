#ifndef COPIER_H
#define COPIER_H

#include "types.h"
#include "spinlock.h"

// Define copier task structure
struct copier_task {
    uint64 src;
    uint64 dst;
    int len;
    int status; // 0: pending, 1: completed, -1: failed
    struct proc *p; // The process that requested the copy
    int task_id;
};

#define COPIER_QUEUE_SIZE 16

struct copier_queue {
    struct copier_task tasks[COPIER_QUEUE_SIZE];
    int head;
    int tail;
    struct spinlock lock;
};

void copier_init(void);
int copier_submit(uint64 src, uint64 dst, int len);
int copier_wait(uint64 addr, int len);

#endif
