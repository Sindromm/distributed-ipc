#ifndef __IFMO_DISTRIBUTED_CLASS_PROC__H
#define __IFMO_DISTRIBUTED_CLASS_PROC__H

#include "ipc.h"
#include "banking.h"

#define LOG_FILE_FLAGS O_CREAT | O_WRONLY | O_APPEND | O_TRUNC
#define MODE 0666
#define RC_OK(x) (x == 0)
#define RC_FAIL(x) !(RC_OK(x))

typedef struct TaskStruct TaskStruct;
typedef struct Item Item;

struct TaskStruct
{
    local_id local_pid;
    local_id total_proc;
    int (*pipes)[2];

    timestamp_t last_time;

    // --mutexl
    int locking;

    Item * queue;
    int queue_size;
    int queue_capacity;

    int done;

    // logging
    int pipe_log_fd;
    int events_log_fd;
};

struct Item {
    timestamp_t time;
    int pid;
};

typedef struct MessagePayload MessagePayload;
struct MessagePayload
{
    char * s_data;
    uint16_t s_size;
};

#endif
