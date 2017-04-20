#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "pipes.h"


/* Pipe descriptors storage
 * Here is N processes
 * Need to create (N - 1) for each
 *
 * Create array of 2 * N * (N - 1) pairs of integers
 * To make easier navigating through this array
 *
 * This way we split pipes array into N blocks
 * Process X need only [X * (N - 1), X * N) subset
 * There are all fds he needs to communicate over pipes
 * with each other process
 * In total, we need N * (N - 1) pipe2 calls
 * And store this fds in 2 * N * (N - 1) array
 * with reversed order duplication
 *
 * Pipe array for 2 child processes:
 *    +----+----+
 *  0 | r0 | w0 |
 *    +----+----+  0 <-> 1
 *  1 | r1 | w1 |
 *    +----+----+
 *  2 | r2 | w2 |
 *    +----+----+  0 <-> 2
 *  3 | r3 | w3 |
 *    +----+----+
 *  4 | r1 | w1 |
 *    +----+----+  1 <-> 0
 *  5 | r0 | w0 |
 *    +----+----+
 *  6 | r4 | w4 |
 *    +----+----+  1 <-> 2
 *  7 | r5 | w5 |
 *    +----+----+
 *  8 | r3 | w3 |
 *    +----+----+  2 <-> 0
 *  9 | r2 | w2 |
 *    +----+----+
 * 10 | r5 | w5 |
 *    +----+----+  2 <-> 1
 * 11 | r4 | w4 |
 *    +----+----+
 */


int test_pipes(TaskStruct * task)
{
    int n = task->total_proc;
    int max = 2 * n * (n - 1);
    for (int i = 0; i < max; i++) {
        printf("%d:\t[0] = %d \t[1] = %d\n",
               i,
               task->pipes[i][0],
               task->pipes[i][1]);
    }
    return 0;
}

int pipe_init(TaskStruct * task)
{
    int n = task->total_proc;
    task->pipes = (int (*)[2])malloc(sizeof(int) * 2 * 2 * n * (n - 1));
    int (*pipes)[2] = task->pipes;
    int fdp = 0;
    for (local_id i = 0; i < n; i++) {     //master_proc_id
        for (local_id j = 0; j < n; j++) { //slave_proc_id
            if (j > i) {
                if (RC_FAIL(pipe2(pipes[fdp++], O_NONBLOCK)))
                    return -1;
                if (RC_FAIL(pipe2(pipes[fdp++], O_NONBLOCK)))
                    return -1;
            }
            else if (j < i) {
                memcpy(&pipes[fdp++], &pipes[get_pipe(task, i, j) + 1], 8);
                memcpy(&pipes[fdp++], &pipes[get_pipe(task, i, j)], 8);
            }
        }
    }

    return 0;
}

int get_pipe(TaskStruct * task, local_id requested, local_id base)
{
    if (requested == base) {
        return -1;
    }

    int n = task->total_proc;
    if (requested < base) {
        return 2 * (n - 1) * base + 2 * requested;
    }
    else {
        return 2 * (n - 1) * base + 2 * (requested - 1);
    }
}

int close_rw_pipes(TaskStruct * task)
{
    //Position of current process fds in pid's fds block
    int block_size = 2 * (task->total_proc - 1);
    int block = task->local_pid * block_size;
    int block_end = block + block_size;

    for (int i = block; i < block_end; i++) {
        if (close(((i % 2) ? task->pipes[i][1] : task->pipes[i][0]))) {
            perror("close_rw_pipes close error");
            return 1;
        }
    }

    return 0;
}

int close_redundant_pipes(TaskStruct * task)
{
    local_id max_pid = task->total_proc;
    int (*pipes)[2] = task->pipes;
    int block_size = 2 * (max_pid - 1);

    for (local_id pid = 0; pid < max_pid; pid++) {
        if (pid == task->local_pid) { //fds for current process
            continue;
        }

        //For each process descriptor block (PDB)
        //Need to close descriptors which have no
        //duplicated in current process PDB

        //for this line don't need to check for -1
        //since checked for this condition above
        //Position of current process fds in pid's fds block
        int lid_pos = get_pipe(task, task->local_pid, pid);
        //start of pid's fds block
        int fdp = pid * block_size;
        //end of pid's fds block
        int max_fdp = fdp + block_size;

        for (; fdp < max_fdp; fdp += 2) {
            if (fdp == lid_pos || pipes[fdp][0] == -1) {
                //mustn't be closed because they are duplicated in
                //current process block
                //
                //or closed already
                continue;
            }

            //Close redundant fdps related to communication between
            //pid and set of all processes except this one
            if (close(pipes[fdp    ][0]) || close(pipes[fdp    ][1]) ||
                close(pipes[fdp + 1][0]) || close(pipes[fdp + 1][1])) {
                perror("close_redundant_pipes close error");
            }

            //For which process fds were closed
            //
            //This is pid's fds block

            local_id closed_id;
            int offset = fdp % block_size / 2;
            if (offset == 0) {
                closed_id = 1;
            }
            else {
                closed_id = offset;
                if (pid <= closed_id) {
                    closed_id++;
                }
            }

            int tip = get_pipe(task, pid, closed_id);
            pipes[tip][0] = pipes[tip][1] = pipes[tip + 1][0] = pipes[tip + 1][1] = -1;
        }
    }

    close_rw_pipes(task);
    return 0;
}

int get_recipient(TaskStruct * task, local_id dst)
{
    if (dst == task->local_pid) {
        return -1;
    }

    return task->pipes[get_pipe(task, dst, task->local_pid)][1];
}

int get_sender(TaskStruct * task, local_id from)
{
    if (from == task->local_pid) {
        return -1;
    }

    return task->pipes[get_pipe(task, from, task->local_pid) + 1][0];
}

int pipe_log(TaskStruct * task, int pid, const char * msg, const char * str)
{
    char log_msg[128] = { 0 };

    sprintf(log_msg, str, task->local_pid, pid, msg, task->balance);
    if (RC_FAIL(write(task->pipe_log_fd, log_msg, strlen(log_msg)))) {
        return -1;
    }

    return 0;
}
