#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "command.h"
#include "jobs.h"

void destroy_job_table();

job **job_table;
size_t job_table_size;
size_t job_table_capacity;

char *job_status_to_string(job_status status) {
    switch (status) {
        case RUNNING:
            return "Running";
        case STOPPED:
            return "Stopped";
        case DONE:
            return "Done";
        case KILLED:
            return "Killed";
        case DETACHED:
            return "Detached";
    }
    perror("Invalid job status");
    exit(1);
}

subjob *new_subjob(command_call *command, pid_t pid, job_status status) {
    subjob *j = malloc(sizeof(subjob));

    if (j == NULL) {
        perror("malloc");
        return NULL;
    }

    j->command = command;
    j->pid = pid;
    j->last_status = status;

    return j;
}

void destroy_subjob(subjob *j) {
    if (j == NULL) {
        return;
    }

    soft_destroy_command_call(j->command);
    free(j);
}

job *new_job(size_t subjobs_size, job_type type) {
    job *j = malloc(sizeof(job));
    j->id = UNINITIALIZED_JOB_ID;

    j->subjobs_size = subjobs_size;

    subjob **subjobs = malloc(subjobs_size * sizeof(subjob *));
    for (size_t i = 0; i < subjobs_size; i++) {
        subjobs[i] = NULL;
    }
    j->subjobs = subjobs;
    j->pgid = 0;
    j->type = type;

    return j;
}

void destroy_job(job *j) {
    if (j == NULL) {
        return;
    }

    for (size_t i = 0; i < j->subjobs_size; i++) {
        destroy_subjob(j->subjobs[i]);
    }

    free(j->subjobs);
    free(j);
}

void soft_destroy_job(job *j) {
    if (j == NULL) {
        return;
    }

    for (size_t i = 0; i < j->subjobs_size; i++) {
        free(j->subjobs[i]);
    }

    free(j->subjobs);
    free(j);
}

void print_subjob(subjob *j, int fd) {
    dprintf(fd, "\t%d\t%s\t", j->pid, job_status_to_string(j->last_status));
    command_call_print(j->command, fd);
    dprintf(fd, "\n");
}

void print_job(job *j, int fd) {
    dprintf(fd, "[%ld]", j->id);
    for (size_t i = 0; i < j->subjobs_size; i++) {
        if (j->subjobs[i] == NULL) {
            continue;
        }
        print_subjob(j->subjobs[i], fd);
    }
}
void init_job_table() {
    destroy_job_table();
    job_table = malloc(INITIAL_JOB_TABLE_CAPACITY * sizeof(job *));
    if (job_table == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    job_table_capacity = INITIAL_JOB_TABLE_CAPACITY;
    job_table_size = 0;

    for (size_t i = 0; i < job_table_capacity; i++) {
        job_table[i] = NULL;
    }
}

void destroy_job_table() {
    if (job_table == NULL) {
        return;
    }
    for (size_t i = 0; i < job_table_capacity; i++) {
        destroy_job(job_table[i]);
        job_table[i] = NULL;
    }
    free(job_table);
}

int add_job(job *j) {
    if (j == NULL) {
        return -1;
    }
    if (job_table_size == job_table_capacity) {
        job_table_capacity += INITIAL_JOB_TABLE_CAPACITY;
        job_table = reallocarray(job_table, job_table_capacity, sizeof(job *));
        if (job_table == NULL) {
            perror("reallocarray");
            exit(EXIT_FAILURE);
        }
        for (size_t i = job_table_size; i < job_table_capacity; i++) {
            job_table[i] = NULL;
        }
    }
    for (size_t i = 0; i < job_table_capacity; i++) {
        if (job_table[i] == NULL) {
            j->id = i + 1;
            job_table[i] = j;
            job_table_size++;
            return i + 1;
        }
    }

    return -1;
}

int remove_job(size_t id) {
    if (id < 1 || id > job_table_capacity || job_table_size == 0 || job_table == NULL) {
        return 1;
    }

    if (job_table[id - 1] == NULL) {
        return 1;
    }

    destroy_job(job_table[id - 1]);

    job_table[id - 1] = NULL;

    job_table_size--;

    return 0;
}

job_status job_status_from_int(int status) {
    // What to do with detached jobs?
    if WIFEXITED (status) {
        return DONE;
    } else if WIFSTOPPED (status) {
        return STOPPED;
    } else if WIFCONTINUED (status) {
        return RUNNING;
    } else if WIFSIGNALED (status) {
        return KILLED;
    }

    return RUNNING;
}

/**
 * Updates the last status of the job with the given pid.
 * If there is an error, -1 is returned.
 * If the status of the job has not changed, 0 is returned.
 * If the status of the job has changed, 1 is returned.
 */
int get_job_status(int pid, job_status *last_status) {
    int status;

    int pid_ = waitpid(pid, &status, WNOHANG | WUNTRACED | WCONTINUED);
    if (pid_ < 0) {
        perror("waitpid");
        return -1;
    }

    if (pid_ == 0) {
        return 0;
    }

    *last_status = job_status_from_int(status);

    return 1;
}

int is_job_running(job *j) {
    for (size_t i = 0; i < j->subjobs_size; i++) {
        if (j->subjobs[i] == NULL) {
            continue;
        }
        if (j->subjobs[i]->last_status == RUNNING || j->subjobs[i]->last_status == STOPPED) {
            return 1;
        }
    }
    return 0;
}

int are_jobs_running() {
    for (size_t i = 0; i < job_table_capacity; i++) {

        if (job_table[i] != NULL) {
            if (is_job_running(job_table[i])) {
                return 1;
            }
        }
    }
    return 0;
}

/**
 * Returns 1 if the job should be removed from the job table.
 * Returns 0 otherwise.
 */
int is_remove_status(job_status status) {
    if (status == DONE || status == KILLED || status == DETACHED) {
        return 1;
    }
    return 0;
}

int should_remove_job(job *job) {
    for (size_t j = 0; j < job->subjobs_size; j++) {
        if (job->subjobs[j] == NULL) {
            continue;
        }
        if (!is_remove_status(job->subjobs[j]->last_status)) {
            return 0;
        }
    }
    return 1;
}

int update_subjob(job *job, size_t subjob_index) {
    subjob *subjob = job->subjobs[subjob_index];
    int has_changed = get_job_status(subjob->pid, &subjob->last_status);

    if (has_changed == -1) {
        return 0;
    }

    if (has_changed) {
        return 1;
    }

    return 0;
}

void fd_update_jobs(int fd) {
    for (size_t i = 0; i < job_table_capacity; i++) {
        if (job_table[i] != NULL) {
            int should_print = 0;
            for (size_t j = 0; j < job_table[i]->subjobs_size; j++) {
                if (job_table[i]->subjobs[j] == NULL) {
                    continue;
                }
                int has_changed = update_subjob(job_table[i], j);
                if (has_changed) {
                    should_print = 1;
                }
            }

            if (should_print) {
                print_job(job_table[i], fd);
            }

            if (should_remove_job(job_table[i])) {
                remove_job(job_table[i]->id);
            }
        }
    }
}

void update_jobs() {
    fd_update_jobs(STDERR_FILENO);
}

int jobs_command(command_call *command_call) {

    // TODO: Implement remaining functionalities

    fd_update_jobs(command_call->stdout);

    // TODO: Delete this when the full functionalities will be implemented
    if (command_call->argc > 1) {
        dprintf(command_call->stderr, "jobs: too many arguments\n");
        return 1;
    }

    for (size_t i = 0; i < job_table_capacity; i++) {
        if (job_table[i] != NULL) {
            print_job(job_table[i], command_call->stdout);
        }
    }

    return 0;
}
