#ifndef PIPES_H_
#define PIPES_H_

#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "ipc.h"
#include "proc.h"


int pipe_init(TaskStruct * task);

int get_pipe(TaskStruct * task, local_id requested, local_id base);

int close_redundant_pipes(TaskStruct * task);

int get_recipient(TaskStruct * task, local_id dst);

int get_sender(TaskStruct * task, local_id from);

int pipe_log(TaskStruct * task, int pid, const char * msg, const char * str);
#endif
