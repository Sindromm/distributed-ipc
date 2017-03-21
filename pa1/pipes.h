#ifndef PIPES_H_
#define PIPES_H_

#include <string.h>
#include <stdlib.h>
#include "ipc.h"

int pipe_init(int num);

void close_unnecessary_pipes();

int get_recipient(local_id dst);

int get_sender(local_id from);

#endif
