#include <unistd.h>

#include "ipc.h"
#include "pipes.h"

extern int n;
extern local_id local_proc_id;

int send(void * self, local_id dst, const Message * msg) {
	int fd = get_recipient(dst);
	if (fd < 0 || write(fd, msg, sizeof(MessageHeader) + (msg->s_header).s_payload_len) <= 0) {
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
					return 0;
			}
		}
	}
}

