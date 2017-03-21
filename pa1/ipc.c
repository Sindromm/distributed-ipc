#include <unistd.h>

#include <stdio.h>

#include "ipc.h"
#include "pipes.h"

extern int n;
extern local_id local_proc_id;

int send(void * self, local_id dst, const Message * msg) {
	int fd = get_recipient(dst);
	printf("\t%d\n", fd);
	if (fd < 0 || write(fd, msg, sizeof(MessageHeader) + (msg->s_header).s_payload_len) <= 0) {
		perror("\tsend error");
		return -1;
	}
	return 0;
}

int send_multicast(void * self, const Message * msg) {
	for (int dst = 0; dst < n; dst++) {
		if (dst != local_proc_id) {
			if (send(self, dst, msg) != 0) 
					return -1;
		}
	}
	return 0;
}

int receive(void * self, local_id from, Message * msg) {
	int fd = get_sender(from);
	if (fd < 0) 
			return -1;
	if (read(fd, msg, sizeof(MessageHeader)) < 0)
			return -1;
	if (read(fd, (MessageHeader *)msg + 1, (msg->s_header).s_payload_len) < 0)
			return -1;
	return 0;
}

int receive_any(void * self, Message * msg) {
	while(1) {
		for (local_id from = 1; from < n; from++) {
			if (receive(NULL, from, msg) < 0) { 
					return -1;
			}
			else {
			//	if (self != NULL)
			//			*self = (void*)from;
				return 0;
			}
		}
	}
}

int wait_other(){
	//char state[n] = {0};
	char *state = calloc(n, sizeof(char));
	state[0] = 1;
	state[local_proc_id] = 1;

	for (int id = 1; id < n; id++) {
		
	}

	return 0;
}











