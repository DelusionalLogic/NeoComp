#pragma once

#include "vector.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

extern Vector results;

struct TestResultEq {
    char* name;
    uint64_t actual;
    uint64_t expected;
};

struct TestResultEqFlt {
    char* name;
    double actual;
    double expected;
};

struct TestResultEqPtr {
    char* name;
    void* actual;
    void* expected;
};

struct TestResultEqArr {
    char* name;
};

struct TestResultEqStr {
    char* name;
    size_t actual;
    size_t expected;
    int length;
};

enum TestResultType {
    TEST_EQ,
    TEST_EQ_FLOAT,
    TEST_EQ_PTR,
    TEST_EQ_ARRAY,
    TEST_EQ_STRING,
};

struct TestResult {
    enum TestResultType type;
    void* extra;
    size_t extra_len;
    bool success;
    union {
        struct TestResultEq eq;
        struct TestResultEqFlt eq_flt;
        struct TestResultEqPtr ptr_eq;
        struct TestResultEqArr eq_arr;
        struct TestResultEqStr eq_str;
    };
};

enum TestOutcome {
    OUTCOME_SUCCESS,
    OUTCOME_ASSERT,
    OUTCOME_INTERNAL_FAILURE,
};

struct Test {
    char* name;
    enum TestOutcome outcome;
    struct TestResult res;
};

struct TestResult assertEqPtr_internal(char* name, void* value, void* expected);
struct TestResult assertEq_internal(char* name, uint64_t value, uint64_t expected);
struct TestResult assertEqFloat_internal(char* name, double value, double expected);
struct TestResult assertEqArray_internal(char* name, void* var, void* value, size_t size);
struct TestResult assertEqString_internal(char* name, char* var, char* value, size_t size);

#define assertEq(var, val)                  \
    return _Generic((var),                  \
            void*: assertEqPtr_internal,    \
            char*: assertEqPtr_internal,    \
            uint64_t: assertEq_internal,    \
            char: assertEq_internal,        \
            float: assertEqFloat_internal,  \
            double: assertEqFloat_internal  \
            )(#var, var, val)

#define assertEqArray(var, val, len) \
    return assertEqArray_internal(#var, var, val, len)

#define assertEqString(var, val, len) \
    return assertEqString_internal(#var, var, val, len)

typedef struct TestResult (*test_func)();

void test_run(char* name, test_func func);

#define TEST(f)                          \
    test_run(#f, f)

struct TestName {
    char* thing;
    char* will;
    char* when;
};

void test_parseName(char* name, struct TestName* res);

uint32_t test_end();
