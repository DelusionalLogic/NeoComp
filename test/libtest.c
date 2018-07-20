#include "libtest.h"

Vector results;

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
    return result;
}

struct TestResult assertEqString_internal(char* name, char* var, char* value, size_t size) {
    struct TestResult result = {
        .type = TEST_EQ_STRING,
        .eq_str = {
            .name = name,
            .actual = malloc(size),
            .expected = malloc(size),
            .length = size,
        }
    };

    memcpy(result.eq_str.actual, var, size);
    memcpy(result.eq_str.expected, value, size);

    result.success = memcmp(var, value, size) == 0;
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

void test_run(char* name, test_func func) {
    struct Test test = {
        .name = name,
        .res = func(),
    };
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

void test_end() {
    uint32_t failed = 0;

    size_t index;
    struct Test* test = vector_getFirst(&results, &index);
    while(test != NULL) {
        struct TestName name;
        test_parseName(test->name, &name);
        if(test->res.success) {
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

        struct TestResult result = test->res;

        switch(result.type) {
            case TEST_EQ:
                printf("\tBy equality test on %s %ld==%ld\n", result.eq.name, result.eq.actual, result.eq.expected);
                break;
            case TEST_EQ_FLOAT:
                printf("\tBy floating equality test on %s %f==%f\n", result.eq_flt.name, result.eq_flt.actual, result.eq_flt.expected);
                break;
            case TEST_EQ_PTR:
                printf("\tBy equality test on %s %p==%p\n", result.ptr_eq.name, result.ptr_eq.actual, result.ptr_eq.expected);
                break;
            case TEST_EQ_ARRAY:
                printf("\tBy array equality test on %s\n", result.eq_arr.name);
                break;
            case TEST_EQ_STRING:
                printf("\tBy string equality test on %s %.*s==%.*s\n",result.eq_str.name,
                        result.eq_str.length, result.eq_str.actual,
                        result.eq_str.length, result.eq_str.expected);
                // @LEAK: We just leak the actual and expected strings here.
                // It's a test script, so who cares?
                break;
        }
        test = vector_getNext(&results, &index);
    }

    printf("%d/%d tests failed\n", failed, vector_size(&results));
}
