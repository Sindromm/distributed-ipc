#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "pipes.h"

local_id n;
int (*pipes)[2];
extern local_id local_proc_id;
extern int p_log;

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

int pipe_init(local_id num)
{
    n = num;
    pipes = malloc(sizeof(int) * 2 * 2 * n * (n - 1));
    int fds_element_pointer = 0;
    for (local_id i = 0; i < n; i++) {     //master_proc_id
        for (local_id j = 0; j < n; j++) { //slave_proc_id
            if (j > i) {
                if (pipe2(pipes[fds_element_pointer++], O_NONBLOCK) == -1)
                    return -1;
                if (pipe2(pipes[fds_element_pointer++], O_NONBLOCK) == -1)
                    return -1;
            }
            else if (j < i) {
                memcpy(&pipes[fds_element_pointer++], &pipes[get_pipe(i, j) + 1], 8);
                memcpy(&pipes[fds_element_pointer++], &pipes[get_pipe(i, j)], 8);
            }
        }
    }

    test_pipes();
    return 0;
}

int get_pipe(local_id requested, local_id base)
{
    if (requested == base) {
        return -1;
    }

    if (requested < base) {
        return 2 * (n - 1) * base + 2 * requested;
    }
    else {
        return 2 * (n - 1) * base + 2 * (requested - 1);
    }
}

int close_rw_pipes() {
    //Position of current process fds in pid's fds block
    int block_size = 2 * (n - 1);
    int block = local_proc_id * block_size;
    int block_end = block + block_size;

    for (int i = block; i < block_end; i++) {
        if (close(((i % 2)?pipes[i][1]:pipes[i][0]))) {
            perror("close_rw_pipes close error");
            return 1;
        }
    }

    return 0;
}

int close_redundant_pipes()
{
    local_id max_pid = n;
    int block_size = 2 * (n - 1);

    for (local_id pid = 0; pid < max_pid; pid++) {
        if (pid == local_proc_id) { //fds for current process
            continue;
        }

        //For each process descriptor block (PDB)
        //Need to close descriptors which have no
        //duplicated in current process PDB

        //for this line don't need to check for -1
        //since checked for this condition above
        //Position of current process fds in pid's fds block
        int lid_pos = get_pipe(local_proc_id, pid);
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

            int tip = get_pipe(pid, closed_id);
            pipes[tip][0] = pipes[tip][1] = pipes[tip + 1][0] = pipes[tip + 1][1] = -1;
        }
    }

    close_rw_pipes();
    return 0;
}

int get_recipient(local_id dst)
{
    if (dst == local_proc_id) {
        return -1;
    }

    return pipes[get_pipe(dst, local_proc_id)][1];
}

int get_sender(local_id from)
{
    if (from == local_proc_id) {
        return -1;
    }

    return pipes[get_pipe(from, local_proc_id) + 1][0];
}

const char * const log_pipe_write_fmt =
	"Process %d write to pipe with = %d\n";

const char * const log_pipe_read_fmt =
	"Process %d read from pipe with = %d\n";

int pipe_log(const char * str, int fd)
{
    char log_msg[128];

    sprintf(log_msg, str, local_proc_id, fd);
    if (write(p_log, log_msg, strlen(log_msg)) < 0) {
        return -1;
    }

    return 0;
}

int test_pipes()
{
    int max = 2 * n * (n - 1);
    for (int i = 0; i < max; i++) {
        printf("%d:\t[0] = %d \t[1] = %d\n",
                i,
                pipes[i][0],
                pipes[i][1]);
    }
    return 0;
}
