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
#include "proc.h"


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

void child_handle(TaskStruct * task)
{
    char log_msg[MAX_PAYLOAD_LEN];
    sprintf(log_msg, log_started_fmt, task->local_pid, getpid(), getppid());
    printf(log_msg, NULL);

    if (write(task->events_log_fd, log_msg, strlen(log_msg)) < 0) {
        perror("write ev_log error");
        exit(EXIT_FAILURE);
    }

    close_redundant_pipes(task);

    //first stage -- start
    MessagePayload payload;
    payload.s_data = log_msg;
    payload.s_size = (uint16_t)strlen(log_msg);

    Message * msg = malloc(sizeof(Message));
    create_message(msg, STARTED, &payload);
    if (send_multicast(task, msg) < 0)
    {
        perror("send_multicast STARTED");
        exit(EXIT_FAILURE);
    }

    //second stage -- work

    //third stage -- done
//TODO: FIX DAT SHIT
    sprintf(log_msg, log_done_fmt, task->local_pid);
    printf(log_msg, NULL);
    if (write(task->events_log_fd, log_msg, strlen(log_msg)) < 0) {
        perror("write ev_log error");
        exit(EXIT_FAILURE);
    }

    payload.s_data = log_msg;
    payload.s_size = (uint16_t)strlen(log_msg);

    create_message(msg, DONE, &payload);
    if (send_multicast(task, msg) < 0) {
        perror("send_multicast DONE");
        exit(EXIT_FAILURE);
    }

    free(msg);

    close(task->events_log_fd);
    close(task->pipe_log_fd);
    exit(EXIT_SUCCESS);
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
            child_handle(&task);
            break;
        default:
            break;
        }
    }

    close_redundant_pipes(&task);

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

    close(task.events_log_fd);
    close(task.pipe_log_fd);
    return 0;
}
