#include "libtest.h"

#include <unistd.h>
#include <sys/wait.h>

Vector results;
int test_fd;

void write_complete(void* buf, size_t size) {
    size_t written = 0;
    while(size - written > 0) {
        ssize_t ret = write(test_fd, buf + written, size - written);
        if(ret >= 0) {
            written += ret;
        } else {
            exit(1);
        }
    }
}

void finalize_assert(struct TestResult* result) {
    write_complete(result, sizeof(struct TestResult));
    write_complete(result->extra, result->extra_len);
}

struct TestResult assertEqPtr_internal(char* name, void* value, void* expected) {
    struct TestResult result = {
        .type = TEST_EQ_PTR,
        .success = value == expected,
        .ptr_eq = {
            .name = name,
            .actual = value,
            .expected = expected,
        }
    };
    finalize_assert(&result);
    return result;
}

struct TestResult assertEq_internal(char* name, uint64_t value, uint64_t expected) {
    struct TestResult result = {
        .type = TEST_EQ,
        .success = value == expected,
        .eq = {
            .name = name,
            .actual = value,
            .expected = expected,
        }
    };
    finalize_assert(&result);
    return result;
}

struct TestResult assertEqFloat_internal(char* name, double value, double expected) {
    // @IMPROVEMENT: Maybe we shouldn't be doing == for floats.
    struct TestResult result = {
        .type = TEST_EQ_FLOAT,
        .success = value == expected,
        .eq_flt = {
            .name = name,
            .actual = value,
            .expected = expected,
        }
    };
    finalize_assert(&result);
    return result;
}

struct TestResult assertEqArray_internal(char* name, void* var, void* value, size_t size) {
    struct TestResult result = {
        .type = TEST_EQ_ARRAY,
        .eq_arr = {
            .name = name,
        }
    };
    result.success = memcmp(var, value, size) == 0;
    finalize_assert(&result);
    return result;
}

struct TestResult assertEqString_internal(char* name, char* var, char* value, size_t size) {
    struct TestResult result = {
        .type = TEST_EQ_STRING,
        .eq_str = {
            .name = name,
            .actual = 0,
            .expected = size,
            .length = size,
        }
    };

    result.extra_len = size * 2;
    result.extra = malloc(result.extra_len);

    memcpy(result.extra + result.eq_str.actual, var, size);
    memcpy(result.extra + result.eq_str.expected, value, size);

    result.success = memcmp(var, value, size) == 0;
    finalize_assert(&result);
    return result;
}

void test_parseName(char* name, struct TestName* res) {
    char* thing_start = name;
    char* thing_end = strstr(thing_start, "__");
    size_t thing_len = thing_end - thing_start;
    res->thing = malloc(sizeof(char) * thing_len + 1);
    memcpy(res->thing, thing_start, thing_len);
    res->thing[thing_len] = '\0';

    char* will_start = thing_end + 2;
    char* will_end = strstr(will_start, "__");
    size_t will_len = will_end - will_start;
    res->will = malloc(sizeof(char) * will_len + 1);
    memcpy(res->will, will_start, will_len);
    for(size_t i = 0; i < will_len; i++) {
        if(res->will[i] == '_') {
            res->will[i] = ' ';
        }
    }
    res->will[will_len] = '\0';

    char* when_start = will_end + 2;
    size_t when_len = strlen(when_start);
    res->when = malloc(sizeof(char) * when_len + 1);
    memcpy(res->when, when_start, when_len);
    for(size_t i = 0; i < when_len; i++) {
        if(res->when[i] == '_') {
            res->when[i] = ' ';
        }
    }
    res->when[when_len] = '\0';
}

void test_receiveData(int fd, struct Test* test) {
    // @HACK: Right now we just keep reading until we either error out or
    // receive 0. We need some errorhandling instead
    size_t offset = 0;
    while(sizeof(test->res) - offset > 0) {
        ssize_t ret = read(fd, ((void*)&test->res) + offset, sizeof(test->res) - offset);
        if(ret == 0)
            break;

        if(ret < 0) {
            test->outcome = OUTCOME_INTERNAL_FAILURE;
            return;
        }

        offset += ret;
    }
    // The testresult we just received will have a pointer to the extra data in
    // the other process. Reset it here.
    test->res.extra = NULL;

    // @HACK: As a bit of a hack. I'm just sending the struct first, and then
    // some dynamic block of memory after. This works fine, but it might be
    // a bit brittle.
    if(test->res.extra_len != 0) {
        test->res.extra = malloc(test->res.extra_len);
        if(test->res.extra == NULL) {
            test->outcome = OUTCOME_INTERNAL_FAILURE;
            return;
        }
        offset = 0;
        while(test->res.extra_len - offset > 0) {
            ssize_t ret = read(fd, test->res.extra + offset, test->res.extra_len - offset);
            if(ret == 0)
                break;

            if(ret < 0) {
                test->outcome = OUTCOME_INTERNAL_FAILURE;
                free(test->res.extra);
                return;
            }

            offset += ret;
        }
    }
}

void test_run(char* name, test_func func) {
    int fds[2];

    //0 is read, 1 is write
    if(pipe(fds) != 0) {
        return;
    }

    int pid = fork();
    if(pid == 0) {
        close(0);
        close(1);
        close(2);
        close(fds[0]);

        test_fd = fds[1];
        func();

        close(fds[1]);

        exit(0);
    } else {
        close(fds[1]);
    }

    struct Test test = {
        .name = name,
        .outcome = OUTCOME_SUCCESS,
    };

    test_receiveData(fds[0], &test);

    int status;
    waitpid(pid, &status, 0);
    close(fds[0]);

    if(!WIFEXITED(status) && test.outcome != OUTCOME_INTERNAL_FAILURE) {
        test.outcome = OUTCOME_ASSERT;
    }

    vector_putBack(&results, &test);
}

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_WHITE   "\x1b[90m"
#define ANSI_COLOR_RESET   "\x1b[0m"

uint32_t test_end() {
    uint32_t failed = 0;

    size_t index;
    struct Test* test = vector_getFirst(&results, &index);
    while(test != NULL) {
        bool success;
        if(test->outcome == OUTCOME_SUCCESS) {
            success = test->res.success;
        } else {
            success = false;
        }

        struct TestName name;
        test_parseName(test->name, &name);

        if(success) {
            printf(ANSI_COLOR_GREEN "✓ "
                    ANSI_COLOR_WHITE "A" ANSI_COLOR_RESET " %s "
                    ANSI_COLOR_WHITE "will" ANSI_COLOR_RESET " %s "
                    ANSI_COLOR_WHITE "when" ANSI_COLOR_RESET " %s"
                    ANSI_COLOR_RESET "\n", name.thing, name.will, name.when);
        } else {
            printf(ANSI_COLOR_RED "✗ "
                    ANSI_COLOR_RED "A" ANSI_COLOR_RESET " %s "
                    ANSI_COLOR_RED "won't" ANSI_COLOR_RESET " %s "
                    ANSI_COLOR_RED "when" ANSI_COLOR_RESET " %s"
                    ANSI_COLOR_RESET "\n", name.thing, name.will, name.when);
            failed++;
        }

        if(test->outcome == OUTCOME_SUCCESS) {
            struct TestResult result = test->res;

            switch(result.type) {
                case TEST_EQ:
                    printf("\tBy equality test on %s %ld==%ld\n", result.eq.name, result.eq.actual, result.eq.expected);
                    break;
                case TEST_EQ_FLOAT:
                    printf("\tBy floating equality test on %s %f==%f\n", result.eq_flt.name, result.eq_flt.actual, result.eq_flt.expected);
                    break;
                case TEST_EQ_PTR:
                    printf("\tBy pointer equality test on %s %p==%p\n", result.ptr_eq.name, result.ptr_eq.actual, result.ptr_eq.expected);
                    break;
                case TEST_EQ_ARRAY:
                    printf("\tBy array equality test on %s\n", result.eq_arr.name);
                    break;
                case TEST_EQ_STRING:
                    printf("\tBy string equality test on %s %.*s==%.*s\n",result.eq_str.name,
                            result.eq_str.length, (char*)result.extra + result.eq_str.actual,
                            result.eq_str.length, (char*)result.extra + result.eq_str.expected);
                    // @LEAK: We just leak the actual and expected strings here.
                    // It's a test script, so who cares?
                    break;
            }
        } else {
            printf("\tBy crash during test\n");
        }
        test = vector_getNext(&results, &index);
    }

    printf("%d/%d tests failed\n", failed, vector_size(&results));
    return failed;
}
