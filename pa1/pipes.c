#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>

#include "pipes.h"

int n;
int (*pipes)[2];
extern local_id local_proc_id;

int pipe_init(int num){
	n = num; //Rename
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
	return 0;
}

//Attention: Should we close write for parent(local_proc_id = 0) proc?
void close_unnecessary_pipes(){ //Descend through the rows
	int to = 2 * n * (n - 1);
	for (int i = 0; i < to; i++) {
		if ((i < 2 * local_proc_id * (n - 1)) || (i >= 2 * (local_proc_id + 1) * (n - 1))) {
					close(pipes[i][0]);
					close(pipes[i][1]);
		}
		else {
			(i % 2 == 0)?close(pipes[i][0]):close(pipes[i][1]);
		}
	}
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

/*
const char * const log_pipe_fmt =
	"Process %d ";

void pipes_log(){
	int pipe_log = open(pipes_log, LOG_FILE_FLAGS, MODR);
	for (int i = 0; i < 2 * n * (n - 1); i++) {
		
	}
}
*/


