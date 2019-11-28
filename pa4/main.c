#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wait.h>

#include "banking.h"
#include "common.h"
#include "ipc.h"
#include "pa2345.h"
#include "pipes.h"
#include "proc.h"

#define PA3_MAX(x, y) ((x > y) ? x : y)

timestamp_t g_time = 0;

timestamp_t time_cmp_and_set(timestamp_t time)
{
    g_time = PA3_MAX(g_time, time);
    return g_time;
}

timestamp_t time_inc()
{
    return ++g_time;
}

timestamp_t get_lamport_time()
{
    return g_time;
}

int event_log(TaskStruct * this, const char * msg, int length)
{
    write(STDOUT_FILENO, msg, length);
    return (write(this->events_log_fd, msg, length) < 0) ? -1 : 0;
}

int event_log_printf(TaskStruct * this, const char * fmt, ...)
{
    va_list argp;

    va_start(argp, fmt);

    char buf[4096];
    int len = vsprintf(buf, fmt, argp);
    int rc = (write(this->events_log_fd, buf, len) < 0) ? -1 : 0;
    va_end(argp);
    return rc;
}

int create_message(Message * msg, MessageType type, const MessagePayload * payload)
{
    MessageHeader header;
    header.s_magic = MESSAGE_MAGIC;
    header.s_type = type;
    header.s_local_time = get_lamport_time();

    if (payload == NULL) {
        msg->s_header = header;
        msg->s_header.s_payload_len = 0;
        return 0;
    }

    if (payload->s_size > MAX_PAYLOAD_LEN) {
        return 1;
    }
    header.s_payload_len = payload->s_size;

    msg->s_header = header;
    memcpy(msg->s_payload, payload->s_data, payload->s_size);
    return 0;
}

enum ParentFSM {
    p_init,
    p_starting,
    p_stopping,
    p_stopped,
    p_terminate
};
typedef enum ParentFSM ParentFSM;

void parent_fsm(TaskStruct * this)
{
    ParentFSM state = p_init;
    Message * msg;
    size_t replies = 0;

    while (1) {
        switch (state) {
        case p_init:
            msg = malloc(sizeof(Message));

            close_redundant_pipes(this);

            state = p_starting;
            break;
        case p_starting:
            receive_any(this, msg);
            if (msg->s_header.s_type != STARTED) {
                event_log_printf(this, "%s[%d]: unexpected message instead of STARTED: %d\n", __FILE__, __LINE__, msg->s_header.s_type);
                state = p_terminate;
                continue;
            }
            if (++replies == this->total_proc - 1) {
                state = p_stopping;
                replies = 0;
            }
            break;
        case p_stopping:
            receive_any(this, msg);
            if (msg->s_header.s_type != DONE) {
                event_log_printf(this, "%s[%d]: unexpected message instead of DONE: %d\n", __FILE__, __LINE__, msg->s_header.s_type);
                state = p_terminate;
                continue;
            }
            if (++replies == this->total_proc - 1) {
                state = p_stopped;
            }
            break;
        case p_stopped:
            for (int i = 0; i < this->total_proc - 1; i++) {
                wait(NULL);
            }
            exit(EXIT_SUCCESS);
            break;
        case p_terminate:
            exit(EXIT_FAILURE);
            break;
        }
    }
}

enum ChildFSM {
    c_init,
    c_starting,
    c_work,
    c_stopping,
    c_stopped,
    c_terminate
};
typedef enum ChildFSM ChildFSM;

void child_fsm(TaskStruct * this)
{
    ChildFSM state = c_init;
    char log_msg[MAX_PAYLOAD_LEN];
    Message * msg;
    local_id replies = 0;
    while (1) {
        switch (state) {
        case c_init: {
            msg = malloc(sizeof(Message));

            close_redundant_pipes(this);

            time_inc();
            int size = sprintf(log_msg,
                               log_started_fmt,
                               get_lamport_time(),
                               this->local_pid,
                               getpid(),
                               getppid(),
                               0 /* balance */);

            if (RC_FAIL(event_log(this, log_msg, size))) {
                perror("write ev_log error");
                state = c_terminate;
                continue;
            }

            MessagePayload payload = (MessagePayload){log_msg, size + 1};
            if (RC_FAIL(create_message(msg, STARTED, &payload))) {
                event_log_printf(this, "%s[%d]: Can not create message\n", __FILE__, __LINE__);
                state = c_terminate;
                continue;
            }
            send_multicast(this, msg);

            state = c_starting;
        } break;
        case c_starting:
            if (replies >= this->total_proc - 1 - 1) {
                state = c_work;
                continue;
            }
            receive_any(this, msg);
            if (msg->s_header.s_type != STARTED) {
                event_log_printf(this, "%s[%d]: unexpected message instead of STARTED: %d\n", __FILE__, __LINE__, msg->s_header.s_type);
                state = c_terminate;
                continue;
            }
            (void)time_cmp_and_set(msg->s_header.s_local_time);
            (void)time_inc();

            replies++;

            break;
        case c_work: {
            const uint32_t to = this->local_pid * 5;
            for (uint32_t i = 1; i <= to; i++) {
                if (RC_FAIL(request_cs(this))) {
                    event_log_printf(this, "%s[%d]: Process %d request CS failed\n", __FILE__, __LINE__, this->local_pid);
                    state = c_terminate;
                    continue;
                }
                event_log_printf(this, "%s[%d]: Process %d started to work %u\n", __FILE__, __LINE__, this->local_pid, i);
                (void)sprintf(log_msg,
                              log_loop_operation_fmt,
                              this->local_pid,
                              i,
                              to);
                print(log_msg);
                event_log_printf(this, "%s[%d]: Process %d have finished work %u\n", __FILE__, __LINE__, this->local_pid, i);
                if (RC_FAIL(release_cs(this))) {
                    event_log_printf(this, "%s[%d]: Process %d release CS failed\n", __FILE__, __LINE__, this->local_pid);
                    state = c_terminate;
                    continue;
                }
            }

            time_inc();

            int size = sprintf(log_msg,
                               log_done_fmt,
                               get_lamport_time(),
                               this->local_pid,
                               0 /* balance */);

            if (RC_FAIL(event_log(this, log_msg, size))) {
                perror("write ev_log error");
                state = c_terminate;
                continue;
            }

            MessagePayload payload = (MessagePayload){log_msg, size + 1};
            if (RC_FAIL(create_message(msg, DONE, &payload))) {
                event_log_printf(this, "%s[%d]: Can not create message\n", __FILE__, __LINE__);
                state = c_terminate;
                continue;
            }
            send_multicast(this, msg);

            state = c_stopping;
        } break;
        case c_stopping:
            event_log_printf(this, "%s[%d]: Process %d stopping, replies %u\n", __FILE__, __LINE__, this->local_pid, this->done);
            if (this->done == this->total_proc - 1 - 1) {
                state = c_stopped;
                continue;
            }
            local_id from = receive_any(this, msg);
            event_log_printf(this, "%s[%d]: Process %d stopping, received message %d\n", __FILE__, __LINE__, this->local_pid, msg->s_header.s_type);

            (void)time_cmp_and_set(msg->s_header.s_local_time);
            (void)time_inc();

            switch (msg->s_header.s_type) {
            case DONE:
                this->done++;
                break;
            case CS_REQUEST:
                create_message(msg, CS_REPLY, NULL);
                send(this, from, msg);
                break;
            default:
                break;
            }
            break;
        case c_stopped:
            event_log_printf(this, "%s[%d]: Process %d successfuly exited\n", __FILE__, __LINE__, this->local_pid);
            exit(EXIT_SUCCESS);
            break;
        case c_terminate:
            event_log_printf(this, "%s[%d]: Process %d exit by an error\n", __FILE__, __LINE__, this->local_pid);
            exit(EXIT_FAILURE);
            break;
        }
    }
}

int send_multicast_except_main(void * self, const Message * msg)
{
    TaskStruct * task = self;
    for (local_id dst = 1; dst < task->total_proc; dst++) {
        if (dst == task->local_pid) {
            continue;
        }
        if (RC_FAIL(send(self, dst, msg))) {
            return -1;
        }
    }

    return 0;
}

int item_comparator(const void * a, const void * b)
{
    const Item * lhs = (const Item *)a;
    const Item * rhs = (const Item *)b;
    if (lhs->time == rhs->time) {
        if (lhs->pid < rhs->pid) {
            return -1;
        }
        return 1;
    }
    if (lhs->time < rhs->time) {
        return -1;
    }
    return 1;
}

int push_item(TaskStruct * this, Item item)
{
    if (this->queue_size == this->queue_capacity) {
        event_log_printf(this, "Queue capacity breached for process %d for item (%d,%d)\n", this->local_pid, item.time, item.pid);
        return -1;
    }
    this->queue[this->queue_size++] = item;
    qsort(this->queue, this->queue_size, sizeof(Item), &item_comparator);

    for (int i = 0; i < this->queue_size; i++) {
        event_log_printf(this, "%s[%d]: Process %d add queue n=%d (%d,%d)\n", __FILE__, __LINE__, this->local_pid, i, this->queue[i].time, this->queue[i].pid);
    }

    return 0;
}

int remove_item(TaskStruct * this, local_id from)
{
    if (this->queue[0].pid != from) {
        event_log_printf(this, "Head of queue is not busy by %d", from);
        return -1;
    }
    for (int i = 0; i < this->queue_size; i++) {
        if (this->queue[i].pid == from) {
            // exchange removable item and the last one
            this->queue[i].pid = this->queue[--this->queue_size].pid;
            this->queue[i].time = this->queue[this->queue_size].time;

            this->queue[this->queue_size].pid = 0;
            this->queue[this->queue_size].time = 0;

            // restore total order
            qsort(this->queue, this->queue_size, sizeof(Item), &item_comparator);

            for (int i = 0; i < this->queue_size; i++) {
                event_log_printf(this, "%s[%d]: Process %d remove queue n=%d (%d,%d)\n", __FILE__, __LINE__, this->local_pid, i, this->queue[i].time, this->queue[i].pid);
            }
            return 0;
        }
    }

    perror("Trying to remove request that doesn't exist");
    return -1;
}

int request_cs(const void * self)
{
    TaskStruct * this = (TaskStruct *)self;

    if (!this->locking) {
        return 0;
    }

    event_log_printf(this, "%s[%d]: Process %d request cs\n", __FILE__, __LINE__, this->local_pid);

    (void)time_inc();

    // request to go in critical section

    // send REQUEST to everyone

    // add request to local queue
    Item item = (Item){get_lamport_time(), this->local_pid};
    if (RC_FAIL(push_item(this, item))) {
        return -1;
    }

    Message msg;
    create_message(&msg, CS_REQUEST, NULL);
    send_multicast_except_main(this, &msg);
    event_log_printf(this, "%s[%d]: Process %d request cs sent\n", __FILE__, __LINE__, this->local_pid);

    // wait for total_proc - 1 replies (except main process)
    // or total_proc - 2 requests

    // go in critical section

    // if our process's request is in the head of the queue
    // then go in critical section

    // otherwise wait for all processes on the left side of us

    // in queue have released their locks

    int allowed = this->total_proc < 3 ? 1 : 0;
    size_t replies = 0;
    while (!allowed) {
        local_id from = receive_any(this, &msg);
        event_log_printf(this, "%s[%d]: Process %d message received from %d\n", __FILE__, __LINE__, this->local_pid, from);
        (void)time_cmp_and_set(msg.s_header.s_local_time);
        (void)time_inc();
        switch (msg.s_header.s_type) {
        case CS_REQUEST:
            item.pid = from;
            item.time = msg.s_header.s_local_time;
            event_log_printf(this, "%s[%d]: Process %d request cs received (%d,%d)\n", __FILE__, __LINE__, this->local_pid, item.time, item.pid);
            if (RC_FAIL(push_item(this, item))) {
                return -1;
            }
            (void)time_inc();
            create_message(&msg, CS_REPLY, NULL);
            if (RC_FAIL(send(this, from, &msg))) {
                return -1;
            }
            event_log_printf(this, "%s[%d]: Process %d request reply sent\n", __FILE__, __LINE__, this->local_pid);
            break;
        case CS_RELEASE:
            event_log_printf(this, "%s[%d]: Process %d release received\n", __FILE__, __LINE__, this->local_pid);
            if (RC_FAIL(remove_item(this, from))) {
                return -1;
            }
            if (replies == this->total_proc - 1 - 1 &&
                this->queue[0].pid == this->local_pid) {
                allowed = 1;
            }
            break;
        case CS_REPLY:
            event_log_printf(this, "%s[%d]: Process %d reply received\n", __FILE__, __LINE__, this->local_pid);
            // queue is non empty as we're here
            if (++replies == this->total_proc - 1 - 1 &&
                this->queue[0].pid == this->local_pid) {
                allowed = 1;
            }
            break;
        case DONE:
            this->done++;
            break;
        default:
            event_log_printf(this, "%s[%d]: Process %d unexpected message %d\n", __FILE__, __LINE__, this->local_pid, msg.s_header.s_type);
            return -1;
        break;
        }
    }

    event_log_printf(this, "%s[%d]: Process %d request cs finish\n", __FILE__, __LINE__, this->local_pid);

    return 0;
}

int release_cs(const void * self)
{
    TaskStruct * this = (TaskStruct *)self;

    if (!this->locking) {
        return 0;
    }

    event_log_printf(this, "%s[%d]: Process %d release cs\n", __FILE__, __LINE__, this->local_pid);

    (void)time_inc();

    // remove our request from local queue
    // send release to total_proc - 1

    Message msg;
    create_message(&msg, CS_RELEASE, NULL);

    if (RC_FAIL(remove_item(this, this->local_pid))) {
        return -1;
    }
    send_multicast_except_main(this, &msg);

    event_log_printf(this, "%s[%d]: Process %d release cs finished\n", __FILE__, __LINE__, this->local_pid);

    return 0;
}

int main(int argc, char * argv[])
{
    if (argc < 2) {
        fprintf(stderr, "./lab <N processes to run> [--mutexl]\n");
        return 1;
    }
    int proc_count = -1;
    static const struct option long_options[] = {
            {"mutexl", no_argument, 0, 'm'},
    };
    int locking = 0;
    int loop = 1;
    while (loop) {
        switch (getopt_long(argc, argv, "mp:", long_options, NULL)) {
        case 'p':
            proc_count = atoi(optarg);
            break;
        case 'm':
            locking = 1;
            break;
        case -1:
            loop = 0;
            break;
        case '?':
            exit(EXIT_FAILURE);
        }
    }

    if (proc_count <= 0 || proc_count > 10) {
        fprintf(stderr, "Invalid amount of child processes to create\n");
        exit(EXIT_FAILURE);
    }

    TaskStruct task = {0};
    task.total_proc = proc_count + 1;
    task.local_pid = 0;
    task.locking = locking;

    if ((task.events_log_fd = open(events_log, LOG_FILE_FLAGS, MODE)) < 0) {
        perror("events log open error");
        exit(EXIT_FAILURE);
    }
    if ((task.pipe_log_fd = open(pipes_log, LOG_FILE_FLAGS, MODE)) < 0) {
        perror("pipe log open error");
        exit(EXIT_FAILURE);
    }

    if (RC_FAIL(pipe_init(&task))) {
        perror("pipe init failed");
        exit(EXIT_FAILURE);
    }

    for (local_id i = 1; i < task.total_proc; i++) {
        switch (fork()) {
        case -1:
            perror("fork error");
            exit(EXIT_FAILURE);
        case 0: {
            TaskStruct this = task;
            this.local_pid = i;
            // We can't receive more than total_proc requests except main
            this.queue_capacity = this.total_proc - 1;
            this.queue_size = 0;
            this.done = 0;
            this.queue = malloc(sizeof(Item) * this.queue_capacity);
            child_fsm(&this);
        } break;
        default:
            break;
        }
    }

    parent_fsm(&task);

    return 0;
}
