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
typedef struct TaskStruct TaskStruct;
struct TaskStruct
{
    local_id local_pid;
    local_id total_proc;
    int pipe_log_fd;
    int events_log_fd;
};

typedef struct MessagePayload MessagePayload;
struct MessagePayload
{
    char * s_data;
    uint16_t s_size;
};
int create_message(Message * msg, MessageType type, const MessagePayload * payload);

local_id n;
int p_log;
TaskStruct * taskStruct;
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

    taskStruct = malloc(sizeof(TaskStruct));
    taskStruct->total_proc = proc_count + 1;
    taskStruct->local_pid = 0;

    if ((taskStruct->events_log_fd = open(events_log, LOG_FILE_FLAGS, MODE)) == -1) {
        perror("events log open error");
        exit(EXIT_FAILURE);
    }
    if ((taskStruct->pipe_log_fd = open(pipes_log, LOG_FILE_FLAGS, MODE)) == -1) {
        perror("pipe log open error");
        exit(EXIT_FAILURE);
    }
    
p_log = taskStruct->pipe_log_fd;
    //n = proc_count + 1;

    if (pipe_init(taskStruct->total_proc) == -1) {
        exit(EXIT_FAILURE);
    }

    for (local_id i = 1; i < taskStruct->total_proc; i++) {
        switch (fork()) {
        case -1:
            perror("fork error");
            exit(EXIT_FAILURE);
        case 0:
            child_handle(i); 
            break;
        default:
            break;
        }
    }

    close_redundant_pipes(0);

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

    close(taskStruct->events_log_fd);
    close(taskStruct->pipe_log_fd);
    return 0;
}

void child_handle(int id)
{
    local_proc_id = id;
    taskStruct->local_pid = id;
    sprintf(log_msg, log_started_fmt, taskStruct->local_pid, getpid(), getppid());
    printf(log_msg, NULL);
    if (write(taskStruct->events_log_fd, log_msg, strlen(log_msg)) < 0) {
        perror("write ev_log error");
        exit(EXIT_FAILURE);
    }

    close_redundant_pipes();

    //first stage -- start
    MessagePayload messagePayload;
    messagePayload.s_data = log_msg; //already contains log_started_fmt
    messagePayload.s_size = (uint16_t)strlen(log_msg);

    Message * msg = malloc(sizeof(Message));
    create_message(msg, STARTED, &messagePayload);
    if (send_multicast(taskStruct, msg) < 0) 
    {
        perror("send_multicast STARTED");
        exit(EXIT_FAILURE);
    }

    //wait_other(STARTED);
    /*local_id proc = 1;
    local_id listened_proc = 0;
    while (listened_proc + 1 < taskStruct->total_proc)
    {
        if (proc > taskStruct->total_proc) 
        {
            proc = 1;
        }
        if (proc == taskStruct->local_pid)
        {
            proc++;
            continue;
        }
        if (receive(taskStruct, proc, msg) < 0)
        {
            perror("receive error");
            proc++; //
        } 
        else
        {
            if ((msg->s_header).s_type == STARTED)
            {
                proc++;
                listened_proc++;
            } 
        }
    }*/
    /* log
     * sprintf(log_msg, log_received_all_started_fmt, taskStruct->local_pid);
     * printf(log_msg, NULL);
     * write(taskStruct->events_log_fd, log_msg, strlen(log_msg));
     */

    //second stage -- work

    //third stage -- done
    sprintf(log_msg, log_done_fmt, taskStruct->local_pid);
    printf(log_msg, NULL);
    if (write(taskStruct->events_log_fd, log_msg, strlen(log_msg)) < 0) {
        perror("write ev_log error");
        exit(EXIT_FAILURE);
    }

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
     * spring(log_msg, log_received_all_done_fmt, taskStruct->local_pid);
     * printf(log_msg, NULL);
     * write(taskStruct->events_log_fd, log_msg, strlen(log_msg));
     */

    close(taskStruct->events_log_fd);
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
