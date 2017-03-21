#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wait.h>

#include "common.h"
#include "ipc.h"
#include "pa1.h"
#include "pipes.h"

#define LOG_FILE_FLAGS O_CREAT | O_WRONLY | O_APPEND | O_TRUNC
#define MODE 0666

void child_handle(int id);

typedef struct MessagePayload MessagePayload;
struct MessagePayload
{
    char * s_data;
    uint16_t s_size;
};
int create_message(Message * msg, MessageType type, const MessagePayload * payload);

int n;
int ev_log;
char log_msg[MAX_PAYLOAD_LEN];

local_id local_proc_id = 0;

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

    if ((ev_log = open(events_log, LOG_FILE_FLAGS, MODE)) == -1) {
        perror("log open error");
        exit(EXIT_FAILURE);
    }

    n = proc_count + 1;

    if (pipe_init(n) == -1)
        exit(EXIT_FAILURE);

    for (int i = 1; i < n; i++) {
        switch (fork()) {
        case -1:
            perror("fork error");
            exit(EXIT_FAILURE);
        case 0:
            child_handle((local_id)i);
            break;
        default:
            break;
        }
    }

    //close_unnecessary_pipes(0);

    //wait_child(STARTED);
    //log
    //wait_child(DONE);
    //log

    for (int i = 0; i < proc_count; i++) {
        if (wait(NULL) == -1) {
            perror("wait error");
            exit(EXIT_FAILURE);
        }
    }

    close(ev_log);
    return 0;
}

void child_handle(int id)
{
    local_proc_id = id;
    sprintf(log_msg, log_started_fmt, local_proc_id, getpid(), getppid());
    printf(log_msg, NULL);
    write(ev_log, log_msg, strlen(log_msg));

    //char *test_msg = "test fd";
    //char test_rec[10] = {0};

    //close_unnecessary_pipes(local_proc_id);

    //if (write(11, test_msg, strlen(test_msg)) < 0) perror("DUCK!");
    //read(10, &test_rec, 8);
    //printf("\t%s\n", test_rec);
    //first stage -- start
    MessagePayload messagePayload;
    messagePayload.s_data = log_msg; //already contains log_started_fmt
    messagePayload.s_size = (uint16_t)strlen(log_msg);

    Message * msg = malloc(sizeof(Message));
    create_message(msg, STARTED, &messagePayload);
    if (send_multicast(NULL, msg) < 0) {
        perror("\tsend_multicast STARTED");
        exit(EXIT_FAILURE);
    }

    //wait_other(STARTED);

    /* log
     * sprintf(log_msg, log_received_all_started_fmt, local_proc_id);
     * printf(log_msg, NULL);
     * write(ev_log, log_msg, strlen(log_msg));
     */

    //second stage -- work

    //third stage -- done
    sprintf(log_msg, log_done_fmt, local_proc_id);
    printf(log_msg, NULL);
    write(ev_log, log_msg, strlen(log_msg));

    messagePayload.s_data = log_msg; //already contains log_started_fmt
    messagePayload.s_size = (uint16_t)strlen(log_msg);

    create_message(msg, DONE, &messagePayload);
    if (send_multicast(NULL, msg) < 0) {
        perror("send_multicast DONE");
        exit(EXIT_FAILURE);
    }

    free(msg);
    //wait_other(DONE);
    /* log
     * spring(log_msg, log_received_all_done_fmt, local_proc_id);
     * printf(log_msg, NULL);
     * write(ev_log, log_msg, strlen(log_msg));
     */

    close(ev_log);
    exit(EXIT_SUCCESS);
}

int create_message(Message * msg, MessageType type, const MessagePayload * payload)
{
    if (payload->s_size > MAX_PAYLOAD_LEN) {
        return 1;
    }

    MessageHeader header;
    header.s_magic       = MESSAGE_MAGIC;
    header.s_type        = type;
    header.s_payload_len = payload->s_size;
    header.s_local_time  = time(NULL);

    msg->s_header = header;
    memcpy(msg->s_payload, payload->s_data, payload->s_size);
    return 0;
}
