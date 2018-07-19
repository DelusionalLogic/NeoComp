#include "vector.h"

#include <string.h>
#include <stdio.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_WHITE   "\x1b[90m"
#define ANSI_COLOR_RESET   "\x1b[0m"

struct TestResultEq {
    char* name;
    uint64_t actual;
    uint64_t expected;
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
    char* actual;
    char* expected;
    int length;
};

enum TestResultType {
    TEST_EQ,
    TEST_EQ_PTR,
    TEST_EQ_ARRAY,
    TEST_EQ_STRING,
};

struct TestResult {
    enum TestResultType type;
    bool success;
    union {
        struct TestResultEq eq;
        struct TestResultEqPtr ptr_eq;
        struct TestResultEqArr eq_arr;
        struct TestResultEqStr eq_str;
    };
};

struct Test {
    char* name;
    struct TestResult res;
};

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

#define assertEq(var, val)               \
    return _Generic((var),               \
            void*: assertEqPtr_internal, \
            char*: assertEqPtr_internal, \
            uint64_t: assertEq_internal, \
            char: assertEq_internal      \
            )(#var, var, val)

#define assertEqArray(var, val, len) \
    return assertEqArray_internal(#var, var, val, len)

#define assertEqString(var, val, len) \
    return assertEqString_internal(#var, var, val, len)

#define TEST(f)                          \
    do{                                  \
        struct Test test = {             \
            .name = #f,                  \
            .res = f(),                  \
        };                               \
        vector_putBack(&results, &test); \
        f();                             \
    }while(0)

struct TestName {
    char* thing;
    char* will;
    char* when;
};

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

static struct TestResult vector__be_empty__initialized() {
    Vector vector;

    vector_init(&vector, sizeof(char), 2);

    assertEq(vector.size, 0);
}

static struct TestResult vector__grow__storing_elements() {
    Vector vector;
    vector_init(&vector, sizeof(char), 2);

    vector_putListBack(&vector, "\0", 1);

    assertEq(vector.size, 1);
}

static struct TestResult vector__shrink__deleting_elements() {
    Vector vector;
    vector_init(&vector, sizeof(char), 2);
    vector_putListBack(&vector, "\0", 1);

    vector_remove(&vector, 0);

    assertEq(vector.size, 0);
}

static struct TestResult vector__return_a_pointer_to_the_value__getting_an_existing_index() {
    Vector vector;
    vector_init(&vector, sizeof(char), 2);
    vector_putListBack(&vector, "\0", 1);

    char* value = vector_get(&vector, 0);
    assertEq(*value, 0);
}

static struct TestResult vector__return_a_pointer_to_the_first_value__getting_index_0() {
    Vector vector;
    vector_init(&vector, sizeof(char), 2);
    vector_putListBack(&vector, "\0\2", 2);

    char* value = vector_get(&vector, 0);
    assertEq(*value, 0);
}

static struct TestResult vector__return_a_pointer_to_the_second_value__getting_index_1() {
    Vector vector;
    vector_init(&vector, sizeof(char), 2);
    vector_putListBack(&vector, "\0\2", 2);

    char* value = vector_get(&vector, 1);
    assertEq(*value, 2);
}

static struct TestResult vector__return_null__getting_out_of_bounds() {
    Vector vector;
    vector_init(&vector, sizeof(char), 2);
    vector_putListBack(&vector, "\0\2", 2);

    char* value = vector_get(&vector, 2);
    assertEq(value, NULL);
}

static struct TestResult vector__maintain_size__inserting_2_elements_into_size_2() {
    Vector vector;
    vector_init(&vector, sizeof(char), 2);
    vector_putListBack(&vector, "\0\2", 2);

    assertEq(vector.maxSize, 2);
}

static struct TestResult vector__resize_to_4__inserting_3_elements_into_size_2() {
    Vector vector;
    vector_init(&vector, sizeof(char), 2);
    vector_putListBack(&vector, "\0\2\0", 3);

    assertEq(vector.maxSize, 4);
}

static struct TestResult vector__be_empty__cleared() {
    Vector vector;
    vector_init(&vector, sizeof(char), 2);
    vector_putListBack(&vector, "\0\2", 2);

    vector_clear(&vector);

    assertEq(vector.size, 0);
}

static struct TestResult vector__have_no_data__killed() {
    Vector vector;
    vector_init(&vector, sizeof(char), 2);
    vector_putListBack(&vector, "\0\2", 2);

    vector_kill(&vector);

    assertEq(vector.data, NULL);
}

static struct TestResult vector__keep_data_linearly__storing() {
    Vector vector;
    vector_init(&vector, sizeof(char), 2);
    vector_putListBack(&vector, "\0\2", 2);

    assertEqArray(vector.data, "\0\2", 2);
}

static struct TestResult vector__keep_stored_data__growing() {
    Vector vector;
    vector_init(&vector, sizeof(char), 2);
    vector_putListBack(&vector, "\0\2", 2);
    vector_putListBack(&vector, "\0\2", 2);

    assertEqArray(vector.data, "\0\2\0\2", 4);
}

static struct TestResult vector__iterate_6_times__iterating_forward_over_6_elements() {
    Vector vector;
    vector_init(&vector, sizeof(char), 6);
    vector_putListBack(&vector, "abcdef", 6);

    size_t count = 0;

    size_t index;
    char* elem = vector_getFirst(&vector, &index);
    while(elem != NULL) {
        count++;
        elem = vector_getNext(&vector, &index);
    }

    assertEq(count, 6);
}

static struct TestResult vector__iterate_elements_in_order_abcdef__iterating_forward_over_abcdef() {
    Vector vector;
    vector_init(&vector, sizeof(char), 6);
    vector_putListBack(&vector, "abcdef", 6);

    char order[6];

    size_t count = 0;
    size_t index;
    char* elem = vector_getFirst(&vector, &index);
    while(elem != NULL) {
        order[count++] = *elem;
        elem = vector_getNext(&vector, &index);
    }

    assertEqString(order, "abcdef", 6);
}

static struct TestResult vector__iterate_6_times__iterating_backward_over_6_elements() {
    Vector vector;
    vector_init(&vector, sizeof(char), 6);
    vector_putListBack(&vector, "abcdef", 6);

    size_t count = 0;

    size_t index;
    char* elem = vector_getLast(&vector, &index);
    while(elem != NULL) {
        count++;
        elem = vector_getPrev(&vector, &index);
    }

    assertEq(count, 6);
}

static struct TestResult vector__iterate_elements_in_order_fedcba__iterating_backward_over_abcdef() {
    Vector vector;
    vector_init(&vector, sizeof(char), 6);
    vector_putListBack(&vector, "abcdef", 6);

    char order[6];

    size_t count = 0;
    size_t index;
    char* elem = vector_getLast(&vector, &index);
    while(elem != NULL) {
        order[count++] = *elem;
        elem = vector_getPrev(&vector, &index);
    }

    assertEqString(order, "fedcba", 6);
}

static struct TestResult vector__iterate_0_times__iterating_forward_over_empty() {
    Vector vector;
    vector_init(&vector, sizeof(char), 6);

    size_t count = 0;

    size_t index;
    char* elem = vector_getFirst(&vector, &index);
    while(elem != NULL) {
        count++;
        elem = vector_getNext(&vector, &index);
    }

    assertEq(count, 0);
}

static struct TestResult vector__iterate_0_times__iterating_backward_over_empty() {
    Vector vector;
    vector_init(&vector, sizeof(char), 6);

    size_t count = 0;

    size_t index;
    char* elem = vector_getLast(&vector, &index);
    while(elem != NULL) {
        count++;
        elem = vector_getPrev(&vector, &index);
    }

    assertEq(count, 0);
}

static struct TestResult vector__maintain_size__circulating() {
    Vector vector;
    vector_init(&vector, sizeof(char), 6);
    vector_putListBack(&vector, "bcadef", 6);

    vector_circulate(&vector, 2, 0);

    assertEq(vector.size, 6);
}

static struct TestResult vector__move_element_to_new_position__circulating() {
    Vector vector;
    vector_init(&vector, sizeof(char), 6);
    vector_putListBack(&vector, "abcdef", 6);

    vector_circulate(&vector, 0, 2);

    assertEq(*(char*)vector_get(&vector, 2), 'a');
}

static struct TestResult vector__shift_elements_between_positions_left__circulating_forward() {
    Vector vector;
    vector_init(&vector, sizeof(char), 6);
    vector_putListBack(&vector, "abcdef", 6);

    vector_circulate(&vector, 0, 2);

    char* substr = vector_get(&vector, 0);
    assertEqString(substr, "bc", 2);
}

static struct TestResult vector__shift_elements_between_positions_right__circulating_backward() {
    Vector vector;
    vector_init(&vector, sizeof(char), 6);
    vector_putListBack(&vector, "bcadef", 6);

    vector_circulate(&vector, 2, 0);

    char* substr = vector_get(&vector, 1);
    assertEqString(substr, "bc", 2);
}

static struct TestResult vector__keep_elements_after_old__circulating_backward() {
    Vector vector;
    vector_init(&vector, sizeof(char), 6);
    vector_putListBack(&vector, "bcadef", 6);

    vector_circulate(&vector, 2, 0);

    char* substr = vector_get(&vector, 3);
    assertEqString(substr, "def", 3);
}

static struct TestResult vector__keep_elements_after_new__circulating_forward() {
    Vector vector;
    vector_init(&vector, sizeof(char), 6);
    vector_putListBack(&vector, "abcdef", 6);

    vector_circulate(&vector, 0, 2);

    char* substr = vector_get(&vector, 3);
    assertEqString(substr, "def", 3);
}

int main(int argc, char** argv) {
    vector_init(&results, sizeof(struct Test), 128);

    TEST(vector__be_empty__initialized);
    TEST(vector__grow__storing_elements);
    TEST(vector__shrink__deleting_elements);

    TEST(vector__return_a_pointer_to_the_value__getting_an_existing_index);
    TEST(vector__return_a_pointer_to_the_first_value__getting_index_0);
    TEST(vector__return_a_pointer_to_the_second_value__getting_index_1);
    TEST(vector__return_null__getting_out_of_bounds);
    TEST(vector__keep_data_linearly__storing);

    TEST(vector__maintain_size__inserting_2_elements_into_size_2);
    TEST(vector__resize_to_4__inserting_3_elements_into_size_2);
    TEST(vector__keep_stored_data__growing);

    TEST(vector__be_empty__cleared);
    TEST(vector__have_no_data__killed);

    TEST(vector__iterate_0_times__iterating_forward_over_empty);
    TEST(vector__iterate_0_times__iterating_backward_over_empty);
    TEST(vector__iterate_6_times__iterating_forward_over_6_elements);
    TEST(vector__iterate_elements_in_order_abcdef__iterating_forward_over_abcdef);
    TEST(vector__iterate_6_times__iterating_backward_over_6_elements);
    TEST(vector__iterate_elements_in_order_fedcba__iterating_backward_over_abcdef);

    TEST(vector__maintain_size__circulating);
    TEST(vector__move_element_to_new_position__circulating);
    TEST(vector__shift_elements_between_positions_left__circulating_forward);
    TEST(vector__keep_elements_after_new__circulating_forward);
    TEST(vector__shift_elements_between_positions_right__circulating_backward);
    TEST(vector__keep_elements_after_old__circulating_backward);

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

    return 0;
}
