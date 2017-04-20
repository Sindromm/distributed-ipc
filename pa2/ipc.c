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

    pipe_log(task, dst, msg->s_payload, "%d => %d: %s");
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
    
    int err = read(fd, msg, sizeof(MessageHeader));
    if (err < 0) {
        return err;
    }

    err = read(fd, (MessageHeader *)msg + 1, (msg->s_header).s_payload_len);
    if (err < 0) {
        return err;
    }

    pipe_log(task, from, msg->s_payload, "%d <= %d: %s");
    return 0;
}

int receive_any(void * self, Message * msg)
{
    TaskStruct * task = self;
    while (1) {
        for (local_id from = 1; from < task->total_proc; from++) {
            int err = receive(self, from, msg);
            if (err >= 0) {
                return 0;
            }

            if (from == task->total_proc) {
                from = 1;
            }
        }
    }
}