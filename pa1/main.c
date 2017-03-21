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
struct MessagePayload {
    char     *s_data;
    uint16_t s_size;
};
int create_massage(Message *msg, MessageType type, const MessagePayload *payload);


int n;
int ev_log;
char log_msg[MAX_PAYLOAD_LEN];

local_id local_proc_id = 0;


int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Not enough arguments\n");
		return 1;
	}
	int proc_count = 0;
	switch(getopt(argc, argv, "p:")) {
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

	if (pipe_init(n) == -1) exit(EXIT_FAILURE);

	//test
	/*for (int i = 0; i < 2 * n * (n - 1); i++) {
		printf("%d\t", i);
		for (int j = 0; j < 2; j++) printf("%d  ", pipes[i][j]);
		printf("\n");
	}*/

	for (int i = 1; i < n; i++) {
		switch(fork()) {
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

	close_unnecessary_pipes(0);

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

void child_handle(int id) {
	local_proc_id = id;
	sprintf(log_msg, log_started_fmt, local_proc_id, getpid(), getppid());
    printf(log_msg, NULL);
	write(ev_log, log_msg, strlen(log_msg));

	close_unnecessary_pipes(local_proc_id);

	//first stage -- start
	MessagePayload messagePayload;
	messagePayload.s_data = log_msg; //already contains log_started_fmt
	messagePayload.s_size = (uint16_t)strlen(log_msg);

	Message *msg = malloc(sizeof(Message));
	create_massage(msg, STARTED, &messagePayload);
	send_multicast(NULL, msg);

	//wait_other(STARTED);
	//log
	//second stage -- work
	//third stage -- done
	//send_multicast(void * self, const Message * msg);
	//wait_other(DONE);
	//log

	close(ev_log);
	exit(EXIT_SUCCESS);
}


int create_massage(Message *msg, MessageType type, const MessagePayload *payload) {
	if (payload->s_size > MAX_PAYLOAD_LEN) {
		return 1;
	}

	MessageHeader header;
	header.s_magic = MESSAGE_MAGIC;
	header.s_type = type;
	header.s_payload_len = payload->s_size;
	header.s_local_time = time(NULL); //check!

	msg->s_header = header;
	memcpy(msg->s_payload, payload->s_data, payload->s_size);
	return 0;
}

//void create_message(Message *msg, MessageType type, const char *payload) {
//	MessageHeader header;
//	header.s_magic = MESSAGE_MAGIC;
//	header.s_type = type;
//	header.s_payload_len = (uint16_t)strlen(payload);
//	header.s_local_time = time(NULL); //check!
//
//	msg->s_header = header;
//	strcpy(msg->s_payload, payload);
//}
