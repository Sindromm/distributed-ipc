#include <unistd.h>

#include <stdio.h>

#include "ipc.h"
#include "pipes.h"
#include "proc.h"


int send(void * self, local_id dst, const Message * msg)
{
    TaskStruct * task = self;
    int fd = get_recipient(task, dst);
    if (fd < 0 || write(fd, msg, sizeof(MessageHeader) + (msg->s_header).s_payload_len) <= 0) {
        perror("send error");
        return -1;
    }
    
    pipe_log(task, task->pipe_log_fd, dst, msg->s_payload,  
            "Process %d write to pid = %d message: %s");
    return 0;
}

int send_multicast(void * self, const Message * msg)
{
    TaskStruct * task = self;
    for (int dst = 0; dst < task->total_proc; dst++) {
        if (dst != task->local_pid) {
            if (send(self, dst, msg) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

int receive(void * self, local_id from, Message * msg)
{
    TaskStruct * task = self;
    int fd = get_sender(task, from);
    if (fd < 0) {
        return -1;
    }

    if (read(fd, msg, sizeof(MessageHeader)) < 0) {
        return -1;
    }

    if (read(fd, (MessageHeader *)msg + 1, (msg->s_header).s_payload_len) < 0) {
        return -1;
    }

    pipe_log(task, task->pipe_log_fd, from, msg->s_payload,
            "Process %d read from pid = %d message: %s");
    return 0;
}

int receive_any(void * self, Message * msg)
{
    TaskStruct * task = self;
    while (1) {
        for (local_id from = 1; from < task->total_proc; from++) {
            if (receive(self, from, msg) < 0) {
                return -1;
            }
            else {
                return 0;
            }
        }
    }
}
