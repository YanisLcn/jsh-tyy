// Original code developed by Yago Iglesias for a
// c project. Github: https://github.com/Yag000/l2_s4_c-project
#ifndef TEST_CORE_H
#define TEST_CORE_H

#include <limits.h>
#include <stdbool.h>
#include <time.h>

extern bool debug;
extern bool allow_slow;

/** Structure to hold test information. */
typedef struct test_info {
    int passed;
    int failed;
    int total;
    double time;
} test_info;

test_info *create_test_info();
void destroy_test_info(test_info *);
void print_test_info(const test_info *);

// Test utils
double clock_ticks_to_seconds(clock_t);

void print_test_header(const char *);
void print_test_footer(const char *, const test_info *);
void print_test_name(const char *);

void handle_string_test(const char *, const char *, int, const char *, test_info *);
void handle_boolean_test(bool, bool, int, const char *, test_info *);
void handle_int_test(int, int, int, const char *, test_info *);
void handle_null_test(void *actual, int line, const char *file, test_info *info);

int open_test_file_to_read(const char *);
int open_test_file_to_write(const char *);

void helper_mute_update_jobs(char *file_name);

// All the tests
test_info *test_command();
test_info *test_cd();
test_info *test_string_utils();
test_info *test_last_exit_code_command();
test_info *test_pwd();
test_info *test_exit();
test_info *test_utils();
test_info *test_jobs();
test_info *test_prompt();
test_info *test_background_jobs();
test_info *test_running_jobs();
test_info *test_redirection();
test_info *test_redirection_parsing();

#endif // TEST_CORE_H
