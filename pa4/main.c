#include <fcntl.h>
#include <getopt.h>
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


#define PA3_MAX(x,y) ((x > y)?x:y)

timestamp_t g_time = 0;

timestamp_t time_cmp_and_set(timestamp_t time)
{
    g_time = PA3_MAX(g_time, time);
    return g_time;
}

timestamp_t time_inc()
{ return ++g_time; }

timestamp_t get_lamport_time()
{ return g_time; }

int event_log(TaskStruct * this, const char * msg, int length)
{
    write(STDOUT_FILENO, msg, length);
    return (write(this->events_log_fd, msg, length) < 0) ? -1 : 0;
}

int create_message(Message * msg, MessageType type, const MessagePayload * payload)
{
    MessageHeader header;
    header.s_magic = MESSAGE_MAGIC;
    header.s_type = type;
    header.s_local_time = get_lamport_time();

    if (payload == NULL) {
        msg->s_header.s_payload_len = 0;
        msg->s_header = header;
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

enum ParentFSM
{
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
                if (RC_FAIL(receive_any(this, msg))) {
                    printf("%s[%d]: Can not create message\n", __FILE__, __LINE__);
                    state = p_terminate;
                    continue;
                }
                if (msg->s_header.s_type != STARTED) {
                    printf("%s[%d]: unexpected message instead of STARTED: %d\n", __FILE__, __LINE__, msg->s_header.s_type);
                    state = p_terminate;
                    continue;
                }
                if (++replies == this->total_proc - 1) {
                    state = p_stopping;
                    replies = 0;
                }
                break;
            case p_stopping:
                if (RC_FAIL(receive_any(this, msg))) {
                    printf("%s[%d]: Can not create message\n", __FILE__, __LINE__);
                    state = p_terminate;
                    continue;
                }
                if (msg->s_header.s_type != DONE) {
                    printf("%s[%d]: unexpected message instead of DONE: %d\n", __FILE__, __LINE__, msg->s_header.s_type);
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

enum ChildFSM
{
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
    size_t replies = 0;
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

                MessagePayload payload = (MessagePayload) { log_msg, size + 1 };
                if (RC_FAIL(create_message(msg, STARTED, &payload))) {
                    printf("%s[%d]: Can not create message\n", __FILE__, __LINE__);
                    state = c_terminate;
                    continue;
                }
                send_multicast(this, msg);

                state = c_starting;
            } break;
            case c_starting:
                if (RC_FAIL(receive_any(this, msg))) {
                    printf("%s[%d]: Can not create message\n", __FILE__, __LINE__);
                    state = c_terminate;
                    continue;
                }
                if (msg->s_header.s_type != STARTED) {
                    printf("%s[%d]: unexpected message instead of STARTED: %d\n", __FILE__, __LINE__, msg->s_header.s_type);
                    state = c_terminate;
                    continue;
                }
                (void)time_cmp_and_set(msg->s_header.s_local_time);
                (void)time_inc();

                if (++replies == this->total_proc - 2) {
                    state = c_work;
                }
                break;
            case c_work: {
                const uint32_t to = this->local_pid * 5;
                for (uint32_t i = 1; i < to; i++) {
                    (void)request_cs(this);
                    (void)sprintf(log_msg,
                                       log_loop_operation_fmt,
                                       this->local_pid, i, to);
                    print(log_msg);
                    (void)release_cs(this);
                }

                time_inc();
                replies = 0;

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

                MessagePayload payload = (MessagePayload) { log_msg, size + 1 };
                if (RC_FAIL(create_message(msg, DONE, &payload))) {
                    printf("%s[%d]: Can not create message\n", __FILE__, __LINE__);
                    state = c_terminate;
                    continue;
                }
                send_multicast(this, msg);

                state = c_stopping;
            } break;
            case c_stopping:
                if (RC_FAIL(receive_any(this, msg))) {
                    printf("%s[%d]: Can not create message\n", __FILE__, __LINE__);
                    state = c_terminate;
                    continue;
                }
                if (msg->s_header.s_type != DONE) {
                    printf("%s[%d]: unexpected message instead of DONE: %d\n", __FILE__, __LINE__, msg->s_header.s_type);
                    state = c_terminate;
                    continue;
                }
                (void)time_cmp_and_set(msg->s_header.s_local_time);
                (void)time_inc();

                if (++replies == this->total_proc - 2) {
                    state = c_stopped;
                }
                break;
            case c_stopped:
                exit(EXIT_SUCCESS);
                break;
            case c_terminate:
                exit(EXIT_FAILURE);
                break;
        }
    }
}

int request_cs(const void * self)
{
    TaskStruct * this = (TaskStruct *)self;

    if (!this->locking) {
        return 0;
    }

    return 0;
}

int release_cs(const void * self)
{
    TaskStruct * this = (TaskStruct *)self;

    if (!this->locking) {
        return 0;
    }

    return 0;
}


int main(int argc, char * argv[])
{
    if (argc < 2) {
        fprintf(stderr, "./lab <N processes to run> [--mutexl]\n");
        return 1;
    }
    int proc_count = 0;
    static const struct option long_options[] = {
        {"mutexl", no_argument, 0,  'm'},
    };
    int locking = 0;
    switch (getopt_long(argc, argv, "mp:", long_options, NULL)) {
    case 'p':
        proc_count = atoi(optarg);
        break;
    case 'm':
        locking = 1;
    case -1:
        exit(EXIT_FAILURE);
    case '?':
        exit(EXIT_FAILURE);
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
            child_fsm(&this);
        } break;
        default:
            break;
        }
    }

    parent_fsm(&task);

    return 0;
}
