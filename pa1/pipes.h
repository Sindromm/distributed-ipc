#ifndef PIPES_H_
#define PIPES_H_

#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "ipc.h"

int pipe_init(local_id num);

int get_pipe(local_id requested, local_id base);

int close_redundant_pipes();

int get_recipient(local_id dst);

int get_sender(local_id from);

int test_pipes();

int pipe_log(char const *str, int fd);
#endif
