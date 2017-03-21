#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "pipes.h"

#define LOG_FILE_FLAGS O_CREAT | O_WRONLY | O_APPEND | O_TRUNC
#define MODE 0666

int n;
int (*pipes)[2];
extern local_id local_proc_id;

int pipe_init(int num){
	n = num;
	pipes = malloc(sizeof(int) * 2 * 2 * n * (n - 1));
	int fds_element_pointer = 0;
	for (int i = 0; i < n; i++) {		//master_proc_id
		for (int j = 0; j < n; j++) { 	//slave_proc_id
			if (j > i) {
				if (pipe2(pipes[fds_element_pointer++], O_NONBLOCK) == -1) 
					return -1;
				if (pipe2(pipes[fds_element_pointer++], O_NONBLOCK) == -1) 
					return -1;
			}
			else if (j < i) {
				memcpy(&pipes[fds_element_pointer++], &pipes[j * 2 * (n - 1) + 2 * (i - 1) + 1], 8);
				memcpy(&pipes[fds_element_pointer++], &pipes[j * 2 * (n - 1) + 2 * (i - 1)], 8);
			}
		}
	}
	
	test_pipes();
	return 0;
}

//Attention: Should we close write for parent(local_proc_id = 0) proc?
int close_unnecessary_pipes(){ //Descend through the rows
	int to = 2 * n * (n - 1);
	for (int i = 0; i < to; i++) {
		if ((i < 2 * local_proc_id * (n - 1)) || (i >= 2 * (local_proc_id + 1) * (n - 1))) {
					if (close(pipes[i][0]) < 0) perror("close_unnecessary_pipes 1 error");
					if (close(pipes[i][1]) < 0) perror("close_unnecessary_pipes 2 error");
		}
		else {
			//(i % 2 == 0)?close(pipes[i][0]):close(pipes[i][1]);
			if (i % 2 == 0) {
				if (close(pipes[i][0]) < 0) perror("close_unnecessary_pipes 3 error");
			}
			else {
				if (close(pipes[i][1]) < 0) perror("close_unnecessary_pipes 4 error");
			}
		}
	}
	return 0;
}

int get_recipient(local_id dst) {
	if (dst < local_proc_id) {
		return pipes[2 * local_proc_id * (n - 1) + 2 * dst][1];
	}
	else if (dst > local_proc_id) { // dst == proc_id --> error?
		return pipes[2 * local_proc_id * (n - 1) + 2 * (dst - 1)][1];
	}
	return -1;
}

int get_sender(local_id from) {
	if (from < local_proc_id) {
		return pipes[2 * local_proc_id * (n - 1) + 2 * from + 1][0];
	}
	else if (from > local_proc_id) { 
		return pipes[2 * local_proc_id * (n - 1) + 2 * from - 1][0];
	}
	return -1;
}


/*const char * const log_pipe_open_fmt =
	"Process %d open pipe to process %d\n";

const char * const log_pipe_close_fmt =
	"Process %d close fd = %d to process %d\n";

int pipe_log(int fd, int slave){
	int p_log;
	char log_msg[128];

	if ((p_log = open(pipes_log, LOG_FILE_FLAGS, MODE)) == -1) {
			perror("pipe_log error");
			return -1;
	}
	
	sprintf(log_msg, log_pipe_close_fmt, local_proc_id, fd, slave);
	write(p_log, log_msg, strlen(log_msg));
	
	close(p_log);
	return 0;
}*/


int test_pipes() {
	for (int i = 0; i < 2 * n * (n - 1); i++) {
		printf("%d\t", i);
		for (int j = 0; j < 2; j++) printf("%d  ", pipes[i][j]);
		printf("\n");
	}
	return 0;
}
