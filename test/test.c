#include "libtest.h"

#include "vector.h"
#include "compton.h"
#include "assets/face.h"

#include <string.h>
#include <stdio.h>

#include <X11/Xlib.h>

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

static struct TestResult convert_xrects_to_relative_rect__keep_all_rects__converting() {
    XRectangle rects[2] = {
        {
            .x = 0,
            .y = 0,
            .width = 100,
            .height = 10,
        },
        {
            .x = 0,
            .y = 10,
            .width = 100,
            .height = 10,
        },
    };
    Vector2 extents = {{ 100, 20 }};

    Vector mrects;
    vector_init(&mrects, sizeof(struct Rect), 2);

    convert_xrects_to_relative_rect(rects, 2, &extents, &mrects);

    assertEq(mrects.size, 2);
}

static struct TestResult convert_xrects_to_relative_rect__keep_x_coordinate__converting() {
    XRectangle rects[2] = {
        {
            .x = 0,
            .y = 0,
            .width = 100,
            .height = 10,
        },
    };
    Vector2 extents = {{ 100, 10 }};

    Vector mrects;
    vector_init(&mrects, sizeof(struct Rect), 1);

    convert_xrects_to_relative_rect(rects, 1, &extents, &mrects);

    struct Rect* rect = vector_get(&mrects, 0);
    assertEq(rect->pos.x, 0);
}

static struct TestResult convert_xrects_to_relative_rect__translate_y_coordinate__converting() {
    XRectangle rects[2] = {
        {
            .x = 0,
            .y = 0,
            .width = 100,
            .height = 10,
        },
    };
    Vector2 extents = {{ 100, 10 }};

    Vector mrects;
    vector_init(&mrects, sizeof(struct Rect), 1);

    convert_xrects_to_relative_rect(rects, 1, &extents, &mrects);

    struct Rect* rect = vector_get(&mrects, 0);
    // Y is also converted to relative coordinates 10/10 = 1
    assertEq(rect->pos.y, 1);
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

    TEST(convert_xrects_to_relative_rect__keep_all_rects__converting);
    TEST(convert_xrects_to_relative_rect__keep_x_coordinate__converting);
    TEST(convert_xrects_to_relative_rect__translate_y_coordinate__converting);

    test_end();

    return 0;
}
