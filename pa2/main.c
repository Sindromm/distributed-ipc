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

int event_log(TaskStruct * this, const char * msg, int length)
{ return write(this->events_log_fd, msg, length); }

void transfer(void * parent_data, local_id src, local_id dst, balance_t amount)
{
    // student, please implement me
}

int create_message(Message * msg, MessageType type, const MessagePayload * payload)
{
    if (payload->s_size > MAX_PAYLOAD_LEN) {
        return 1;
    }

    MessageHeader header;
    header.s_magic = MESSAGE_MAGIC;
    header.s_type = type;
    header.s_payload_len = payload->s_size;
    header.s_local_time = get_physical_time();

    msg->s_header = header;
    memcpy(msg->s_payload, payload->s_data, payload->s_size);
    return 0;
}

/*
int wait_other(TaskStruct * task, MessageType type)
{
    Message msg;
    local_id id = 1;
    while (id < task->total_proc) {
        if (id == task->local_pid) {
            id++;
            continue;
        }

        if (receive(task, id, &msg) < 0) {
            perror("receive from child error");
            return -1;
        }
        else {
            if ((msg.s_header).s_type == type) {
                id++;
            }
        }
    }
    return 0;
}

void child_handle(TaskStruct * task)
{
    char log_msg[MAX_PAYLOAD_LEN];
    int symb = sprintf(log_msg, log_started_fmt, get_physical_time(), task->local_pid, getpid(), getppid(), task->balance);
    printf(log_msg, NULL);

    if (write(task->events_log_fd, log_msg, symb) < 0) {
        perror("write ev_log error");
        exit(EXIT_FAILURE);
    }

    close_redundant_pipes(task);

    //first stage -- start
    MessagePayload payload;
    payload.s_data = log_msg;
    payload.s_size = (uint16_t)symb + 1;

    Message * msg = malloc(sizeof(Message));
    create_message(msg, STARTED, &payload);
    if (send_multicast(task, msg) < 0) {
        perror("send_multicast STARTED");
        exit(EXIT_FAILURE);
    }

    if (wait_other(task, STARTED) < 0) {
        symb = sprintf(log_msg, "Process %1d: did'n receive all STARTED messages\n", task->local_pid);
        printf(log_msg, NULL);
        if (write(task->events_log_fd, log_msg, symb) < 0) {
            perror("write ev_log error");
            exit(EXIT_FAILURE);
        }
    }
    else {
        symb = sprintf(log_msg, log_received_all_started_fmt, task->local_pid, task->balance);
        printf(log_msg, NULL);
        if (write(task->events_log_fd, log_msg, symb) < 0) {
            perror("write ev_log error");
            exit(EXIT_FAILURE);
        }
    }
    //second stage -- work

    //third stage -- done

    symb = sprintf(log_msg, log_done_fmt, get_physical_time(), task->local_pid, task->balance);
    printf(log_msg, NULL);
    if (write(task->events_log_fd, log_msg, symb) < 0) {
        perror("write ev_log error");
        exit(EXIT_FAILURE);
    }

    payload.s_data = log_msg;
    payload.s_size = (uint16_t)symb + 1;

    create_message(msg, DONE, &payload);
    if (send_multicast(task, msg) < 0) {
        perror("send_multicast DONE");
        exit(EXIT_FAILURE);
    }

    if (wait_other(task, DONE) < 0) {
        symb = sprintf(log_msg, "Process %1d: did'n receive all DONE messages\n", task->local_pid);
        printf(log_msg, NULL);
        if (write(task->events_log_fd, log_msg, symb) < 0) {
            perror("write ev_log error");
            exit(EXIT_FAILURE);
        }
    }
    else {
        symb = sprintf(log_msg, log_received_all_done_fmt, task->local_pid, task->balance);
        printf(log_msg, NULL);
        if (write(task->events_log_fd, log_msg, symb) < 0) {
            perror("write ev_log error");
            exit(EXIT_FAILURE);
        }
    }

    free(msg);

    close(task->events_log_fd);
    close(task->pipe_log_fd);
    exit(EXIT_SUCCESS);
}
*/

/*
 * Bank department FSM
 */

enum department_state {
    d_initial = 0,
    d_send_started,
    d_handle_messages,
    d_handle_out_transfer,
    d_handle_in_transfer,
    d_handle_stop,
    d_handle_done,
    d_send_transfer,
    d_send_ack,
    d_send_done,
    d_finish
};
typedef enum department_state department_state;

void department_fsm(TaskStruct * this)
{
    department_state state = d_initial;

    Message * msg = malloc(sizeof(Message));

    int next = 1;
    while (next) {
        switch (state) {
        case d_initial: {
            state = d_send_started;
        } break;
        case d_send_started: {
            char log_msg[MAX_PAYLOAD_LEN];
            int symb = sprintf(log_msg,
                               log_started_fmt,
                               get_physical_time(),
                               this->local_pid,
                               getpid(), getppid(),
                               this->balance);
            printf(log_msg, NULL);

            if (event_log(this, log_msg, symb) < 0) {
                perror("write ev_log error");
                //TODO: would be better to introduce d_failed_finish
                exit(EXIT_FAILURE);
            }
            MessagePayload payload;
            payload.s_data = log_msg;
            payload.s_size = (uint16_t)symb + 1;
            create_message(msg, STARTED, &payload);

            send_multicast(this, msg);

            state = d_handle_messages;
        } break;
        case d_handle_messages: {
            int status = receive_any(this, msg);
            if (RC_OK(status)) {
                switch (msg->s_header.s_type) {
                case DONE:
                    state = d_handle_done;
                    break;
                case STOP:
                    state = d_handle_stop;
                    break;
                case TRANSFER: {
                    TransferOrder * order = (TransferOrder *)msg->s_payload;
                    if (order->s_src == this->local_pid) {
                        state = d_handle_out_transfer;
                    }
                    else {
                        state = d_handle_in_transfer;
                    }
                } break;
                }
            }
        } break;
        case d_handle_out_transfer: {
        } break;
        case d_handle_in_transfer: {
        } break;
        case d_handle_stop: {
        } break;
        case d_handle_done: {
        } break;
        case d_send_transfer: {
        } break;
        case d_send_ack: {
        } break;
        case d_send_done: {
        } break;
        case d_finish: {
        } break;
        }
    }
}

/*
 * FSM for transfer manage
 */

enum manager_state {
    m_initial = 0,
    m_handle_messages,
    m_handle_started,
    m_handle_done,
    m_handle_ack,
    m_handle_balance_history,
    m_all_started,
    m_all_done,
    m_all_balances,
    m_finish
};
typedef enum manager_state manager_state;

void manager_fsm(TaskStruct * this)
{
    manager_state state = m_initial;

    Message * msg = malloc(sizeof(Message));

    int next = 1;
    while (next) {
        switch (state) {
        case m_initial: {
            state = m_handle_messages;
        } break;
        case m_handle_messages: {
            int status = receive_any(this, msg);
            if (RC_OK(status)) {
                switch (msg->s_header.s_type) {
                case STARTED:
                    state = m_handle_started;
                    break;
                case DONE:
                    state = m_handle_done;
                    break;
                case ACK:
                    state = m_handle_ack;
                    break;
                case BALANCE_HISTORY:
                    state = m_handle_balance_history;
                    break;
                }
            }
        } break;
        case m_handle_started: {
        } break;
        case m_handle_done: {
        } break;
        case m_handle_ack: {
        } break;
        case m_handle_balance_history: {
        } break;
        case m_all_started: {
        } break;
        case m_all_done: {
        } break;
        case m_all_balances: {
        } break;
        case m_finish: {
        } break;
        }
    }
}

int main(int argc, char * argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Not enough arguments\n");
        return 1;
    }
    int proc_count = 0;
    switch (getopt(argc, argv, "p:")) {
    case 'p':
        proc_count = atoi(optarg);
        break;
    case -1:
        exit(EXIT_FAILURE);
    case '?':
        exit(EXIT_FAILURE);
    }

    if (proc_count <= 0 || proc_count > 10) {
        fprintf(stderr, "Invalid amount of child processes to create\n");
        exit(EXIT_FAILURE);
    }

#define ARG_OFFSET 3
    if (proc_count + ARG_OFFSET != argc) {
        fprintf(stderr, "Bad list of balances for given amount of processes\n");
        exit(EXIT_FAILURE);
    }

    TaskStruct task;
    task.total_proc = proc_count + 1;
    task.local_pid = 0;

    if ((task.events_log_fd = open(events_log, LOG_FILE_FLAGS, MODE)) == -1) {
        perror("events log open error");
        exit(EXIT_FAILURE);
    }
    if ((task.pipe_log_fd = open(pipes_log, LOG_FILE_FLAGS, MODE)) == -1) {
        perror("pipe log open error");
        exit(EXIT_FAILURE);
    }

    if (pipe_init(&task) == -1) {
        exit(EXIT_FAILURE);
    }

    for (local_id i = 1; i < task.total_proc; i++) {
        switch (fork()) {
        case -1:
            perror("fork error");
            exit(EXIT_FAILURE);
        case 0:
            task.local_pid = i;
            task.balance = atoi(argv[i + ARG_OFFSET - 1]);
            department_fsm(&task);
            break;
        default:
            break;
        }
    }

    close_redundant_pipes(&task);

    manager_fsm(&task);

    /*
    int symb;
    char log_msg[MAX_PAYLOAD_LEN];

    if (wait_other(&task, STARTED) < 0) {
        symb = sprintf(log_msg, "Process %1d: didn't receive all STARTED messages\n", task.local_pid);
        printf(log_msg, NULL);
        if (write(task.events_log_fd, log_msg, symb) < 0) {
            perror("write ev_log error");
            exit(EXIT_FAILURE);
        }
    }
    else {
        symb = sprintf(log_msg, log_received_all_started_fmt, task.local_pid, task.balance);
        printf(log_msg, NULL);
        if (write(task.events_log_fd, log_msg, symb) < 0) {
            perror("write ev_log error");
            exit(EXIT_FAILURE);
        }
    }

    if (wait_other(&task, DONE) < 0) {
        symb = sprintf(log_msg, "Process %1d: did'n receive all DONE messages\n", task.local_pid);
        printf(log_msg, NULL);
        if (write(task.events_log_fd, log_msg, symb) < 0) {
            perror("write ev_log error");
            exit(EXIT_FAILURE);
        }
    }
    else {
        symb = sprintf(log_msg, log_received_all_done_fmt, task.local_pid, task.balance);
        printf(log_msg, NULL);
        if (write(task.events_log_fd, log_msg, symb) < 0) {
            perror("write ev_log error");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < proc_count; i++) {
        if (wait(NULL) == -1) {
            perror("wait error");
            exit(EXIT_FAILURE);
        }
    }
    */

    close(task.events_log_fd);
    close(task.pipe_log_fd);
    return 0;
}
