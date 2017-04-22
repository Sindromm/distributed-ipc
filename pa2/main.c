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
{
    write(STDOUT_FILENO, msg, length);
    return (write(this->events_log_fd, msg, length) < 0) ? -1 : 0;
}

int create_message(Message * msg, MessageType type, const MessagePayload * payload)
{
    MessageHeader header;
    header.s_magic = MESSAGE_MAGIC;
    header.s_type = type;
    header.s_local_time = get_physical_time();

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

void transfer(void * parent_data, local_id src, local_id dst, balance_t amount)
{
    TaskStruct * this = (TaskStruct *)parent_data;

    TransferOrder order = (TransferOrder){src, dst, amount};
    MessagePayload payload;
    payload.s_data = (char *)&order;
    payload.s_size = sizeof(TransferOrder);
    Message msg;

    if (RC_FAIL(create_message(&msg, TRANSFER, &payload))) {
        printf("%s[%d]: Can not create message\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }

    send(this, src, &msg);
}

timestamp_t push_history(BalanceHistory * history, timestamp_t last_time, balance_t balance, balance_t incoming, timestamp_t time)
{
    for (timestamp_t i = last_time; i < time; i++) {
        history->s_history[i] = (BalanceState){balance, i, 0};
    }
    history->s_history[time] = (BalanceState){balance + incoming, time, 0};
    history->s_history_len = time + 1;
    return time;
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
    d_all_done,
    d_failed_finish,
    d_finish
};
typedef enum department_state department_state;

void department_fsm(TaskStruct * this)
{
    department_state state = d_initial;

    Message * msg;
    char log_msg[MAX_PAYLOAD_LEN];

    int done_n = 0;

    int next = 1;
    while (next) {
        switch (state) {
        case d_initial: {
            close_redundant_pipes(this);

            msg = malloc(sizeof(Message));
            state = d_send_started;
        } break;
        case d_send_started: {
            int symb = sprintf(log_msg,
                               log_started_fmt,
                               get_physical_time(),
                               this->local_pid,
                               getpid(),
                               getppid(),
                               this->balance);

            if (RC_FAIL(event_log(this, log_msg, symb))) {
                perror("write ev_log error");
                state = d_failed_finish;
                continue;
            }
            MessagePayload payload = (MessagePayload) { log_msg, symb + 1 };
            if (RC_FAIL(create_message(msg, STARTED, &payload))) {
                printf("%s[%d]: Can not create message\n", __FILE__, __LINE__);
                state = d_failed_finish;
                continue;
            }
            send(this, 0 /* is always manager */, msg);

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
                    state = (order->s_src == this->local_pid) ? d_handle_out_transfer : d_handle_in_transfer;
                } break;
                }
            }
        } break;
        case d_handle_out_transfer: {
            TransferOrder * order = (TransferOrder *)msg->s_payload;
            timestamp_t time = msg->s_header.s_local_time;
            this->last_time = push_history(&this->history, this->last_time, this->balance, -order->s_amount, time);
            this->balance -= order->s_amount;

            int symb = sprintf(log_msg,
                               log_transfer_out_fmt,
                               time,
                               order->s_src,
                               order->s_amount,
                               order->s_dst);
            if (RC_FAIL(event_log(this, log_msg, symb))) {
                perror("write ev_log error");
                state = d_failed_finish;
                continue;
            }

            state = d_send_transfer;
        } break;
        case d_handle_in_transfer: {
            TransferOrder * order = (TransferOrder *)msg->s_payload;
            timestamp_t time = msg->s_header.s_local_time;
            this->last_time = push_history(&this->history, this->last_time, this->balance, order->s_amount, time);
            this->balance += order->s_amount;

            int symb = sprintf(log_msg,
                               log_transfer_in_fmt,
                               time,
                               order->s_dst,
                               order->s_amount,
                               order->s_src);
            if (RC_FAIL(event_log(this, log_msg, symb))) {
                perror("write ev_log error");
                state = d_failed_finish;
                continue;
            }

            state = d_send_ack;
        } break;
        case d_handle_stop: {
            state = d_send_done;
        } break;
        case d_handle_done: {
            state = d_handle_messages;

            done_n++;
            if (done_n == this->total_proc - 2) { //except manager and himself
                state = d_all_done;
            }
        } break;
        case d_send_transfer: {
            TransferOrder * order = (TransferOrder *)msg->s_payload;
            send(this, order->s_dst, msg); //retransmit trasfer message
            state = d_handle_messages;
        } break;
        case d_send_ack: {
            if (RC_FAIL(create_message(msg, ACK, NULL))) {
                printf("%s[%d]: Can not create message\n", __FILE__, __LINE__);
                state = d_failed_finish;
                continue;
            }

            send(this, 0 /* is always manager */, msg);
            state = d_handle_messages;
        } break;
        case d_send_done: {
            int symb = sprintf(log_msg,
                               log_done_fmt,
                               get_physical_time(),
                               this->local_pid,
                               this->balance);
            if (RC_FAIL(event_log(this, log_msg, symb))) {
                perror("write ev_log error");
                state = d_failed_finish;
                continue;
            }

            MessagePayload payload = (MessagePayload) { log_msg, symb + 1 };
            if (RC_FAIL(create_message(msg, DONE, &payload))) {
                printf("%s[%d]: Can not create message\n", __FILE__, __LINE__);
                state = d_failed_finish;
                continue;
            }

            send_multicast(this, msg);
            state = d_handle_messages;
        } break;
        case d_all_done: {
            state = d_finish;

            timestamp_t time = get_physical_time();
            int symb = sprintf(log_msg,
                               log_received_all_done_fmt,
                               time,
                               this->local_pid);
            if (RC_FAIL(event_log(this, log_msg, symb))) {
                perror("event_log failed");
                state = d_failed_finish;
            }

            //refresh history
            this->last_time = push_history(&this->history, this->last_time, this->balance, 0, time);

            int balance_size = sizeof(BalanceHistory) - sizeof(this->history.s_history) + sizeof(BalanceState) * this->history.s_history_len;
            MessagePayload payload = (MessagePayload){(char *)&this->history, balance_size};
            if (RC_FAIL(create_message(msg, BALANCE_HISTORY, &payload))) {
                printf("%s[%d]: Can not create message\n", __FILE__, __LINE__);
                state = d_failed_finish;
                continue;
            }

            send(this, 0 /* is always manager */, msg);
        } break;
        case d_failed_finish: {
            exit(EXIT_FAILURE);
        } break;
        case d_finish: {
            exit(EXIT_SUCCESS);
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
    m_send_stop,
    m_all_started,
    m_all_done,
    m_all_balances,
    m_failed_finish,
    m_finish
};
typedef enum manager_state manager_state;

void manager_fsm(TaskStruct * this)
{
    manager_state state = m_initial;

    Message * msg;
    AllHistory all_history = {0};
    char log_msg[MAX_PAYLOAD_LEN];

    int started_n = 0;
    int done_n = 0;

    int next = 1;
    while (next) {
        switch (state) {
        case m_initial: {
            msg = malloc(sizeof(Message));
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

            started_n++;
            if (started_n == this->total_proc - 1) {
                state = m_all_started;
            }
        } break;
        case m_handle_done: {
            state = m_handle_messages;

            done_n++;
            if (done_n == this->total_proc - 1) {
                state = m_all_done;
            }
        } break;
        case m_handle_ack: {
            //Don't need to to anything here
            state = m_handle_messages;
        } break;
        case m_handle_balance_history: {
            state = m_handle_messages;

            BalanceHistory * history = (BalanceHistory *)msg->s_payload;
            all_history.s_history[all_history.s_history_len++] = *history;

            if (all_history.s_history_len == this->total_proc - 1) {
                state = m_all_balances;
            }
        } break;
        case m_all_started: {
            state = m_send_stop;

            int symb = sprintf(log_msg,
                               log_received_all_started_fmt,
                               get_physical_time(),
                               this->local_pid);
            if (RC_FAIL(event_log(this, log_msg, symb))) {
                perror("event_log failed");
                state = m_failed_finish;
            }
            bank_robbery(this, this->total_proc - 1);
        } break;
        case m_send_stop: {
            if (RC_FAIL(create_message(msg, STOP, NULL))) {
                printf("%s[%d]: Can not create message\n", __FILE__, __LINE__);
                state = m_failed_finish;
                continue;
            }

            send_multicast(this, msg);
            state = m_handle_messages;
        } break;
        case m_all_done: {
            int symb = sprintf(log_msg,
                               log_received_all_done_fmt,
                               get_physical_time(),
                               this->local_pid);
            if (RC_FAIL(event_log(this, log_msg, symb))) {
                perror("event_log failed");
                state = m_failed_finish;
            }

            state = m_handle_messages;
        } break;
        case m_all_balances: {
            print_history(&all_history);

            state = m_finish;
        } break;
        case m_failed_finish: {
            exit(EXIT_FAILURE);
        } break;
        case m_finish: {
            exit(EXIT_SUCCESS);
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

    TaskStruct task = {0};
    task.total_proc = proc_count + 1;
    task.local_pid = 0;

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
            this.history.s_id = i;
            this.balance = atoi(argv[i + ARG_OFFSET - 1]);
            department_fsm(&this);
        } break;
        default:
            break;
        }
    }

    close_redundant_pipes(&task);

    manager_fsm(&task);

    for (local_id i = 0; i < task.total_proc; i++) {
        if (wait(NULL) == -1) {
            perror("wait error");
            exit(EXIT_FAILURE);
        }
    }
    return 0;
}
