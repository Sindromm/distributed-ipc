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

void transfer(void * parent_data, local_id src, local_id dst, balance_t amount)
{
    TaskStruct * this = (TaskStruct *)parent_data;

    TransferOrder order = (TransferOrder){ src, dst, amount };
    MessagePayload payload;
    payload.s_data = (char *)&order;
    payload.s_size = sizeof(TransferOrder);
    Message msg;

    if (RC_FAIL(create_message(&msg, TRANSFER, &payload))) {
        printf("%s[%d]: Can not create message", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }

    send(this, dst, &msg);
}

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
    d_failed_finish,
    d_finish
};
typedef enum department_state department_state;

void department_fsm(TaskStruct * this)
{
    department_state state = d_initial;

    Message * msg;
    char log_msg[MAX_PAYLOAD_LEN];

    int next = 1;
    while (next) {
        switch (state) {
        case d_initial: {
            msg   = malloc(sizeof(Message));
            close_redundant_pipes(this);
            state = d_send_started;
        } break;
        case d_send_started: {
            int symb = sprintf(log_msg,
                               log_started_fmt,
                               get_physical_time(),
                               this->local_pid,
                               getpid(), getppid(),
                               this->balance);
            printf(log_msg, NULL);

            if (RC_FAIL(event_log(this, log_msg, symb))) {
                perror("write ev_log error");
                state = d_failed_finish;
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
            //TODO: history

            state = d_send_transfer;
        } break;
        case d_handle_in_transfer: {
            //TODO: history

            state = d_send_ack;
        } break;
        case d_handle_stop: {

            state = d_handle_messages;
        } break;
        case d_handle_done: {

            state = d_handle_messages;
        } break;
        case d_send_transfer: {
            TransferOrder * order = (TransferOrder *)msg->s_payload;
            transfer(this, order->s_src, order->s_dst, order->s_amount);

            state = d_handle_messages;
        } break;
        case d_send_ack: {
            MessagePayload payload = (MessagePayload) { 0, 0 };
            if (RC_FAIL(create_message(msg, ACK, &payload))) {
                state = d_failed_finish;
                continue;
            }

            send(this, 0 /* is always manager */, msg);
            state = d_handle_messages;
        } break;
        case d_send_done: {

            state = d_handle_messages;
        } break;
        case d_failed_finish: {

            exit(EXIT_FAILURE);
        } break;
        case d_finish: {
            next = 0;
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

    Message * msg;
    AllHistory all_history = { 0 };

    int next = 1;
    while (next) {
        switch (state) {
        case m_initial: {
            msg   = malloc(sizeof(Message));
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

            state = m_handle_messages;
        } break;
        case m_handle_done: {

            state = m_handle_messages;
        } break;
        case m_handle_ack: {

            state = m_handle_messages;
        } break;
        case m_handle_balance_history: {

            state = m_handle_messages;
        } break;
        case m_all_started: {
            bank_robbery(this, this->total_proc);

            state = m_handle_messages;
        } break;
        case m_all_done: {
        } break;
        case m_all_balances: {
            //TODO: here all balances should be collected so need to call
            print_history(&all_history);
            state = m_finish;
        } break;
        case m_finish: {
            next = 0;
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

    TaskStruct task = { 0 };
    task.total_proc = proc_count + 1;
    task.local_pid = 0;

    if (RC_FAIL(task.events_log_fd = open(events_log, LOG_FILE_FLAGS, MODE))) {
        perror("events log open error");
        exit(EXIT_FAILURE);
    }
    if (RC_FAIL(task.pipe_log_fd = open(pipes_log, LOG_FILE_FLAGS, MODE))) {
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

    close(task.events_log_fd);
    close(task.pipe_log_fd);
    return 0;
}
