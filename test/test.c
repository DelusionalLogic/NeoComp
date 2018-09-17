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

static struct TestResult vector__assert__getting_out_of_bounds() {
    test_shouldAssert();

    Vector vector;
    vector_init(&vector, sizeof(char), 2);
    vector_putListBack(&vector, "\0\2", 2);

    char* value = vector_get(&vector, 2);

    // Should never happen;
    assertNo();
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

    convert_xrects_to_relative_rect(rects, 2, &extents, &VEC2_ZERO, &mrects);

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

    convert_xrects_to_relative_rect(rects, 1, &extents, &VEC2_ZERO, &mrects);

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

    convert_xrects_to_relative_rect(rects, 1, &extents, &VEC2_ZERO, &mrects);

    struct Rect* rect = vector_get(&mrects, 0);
    // Y is also converted to relative coordinates 10/10 = 1
    assertEq(rect->pos.y, 1);
}

static struct TestResult swiss__be_empty__initialized() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_init(&swiss, 1);

    assertEq(swiss.size, 0);
}

static struct TestResult swiss__grow__allocating() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_init(&swiss, 1);

    swiss_allocate(&swiss);

    assertEq(swiss.size, 1);
}

static struct TestResult swiss__shrink__removing_item() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_init(&swiss, 1);
    win_id id = swiss_allocate(&swiss);

    swiss_remove(&swiss, id);

    assertEq(swiss.size, 0);
}

static struct TestResult swiss__save__adding_a_component() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_MUD, sizeof(char));
    swiss_init(&swiss, 1);
    win_id id = swiss_allocate(&swiss);

    char* component = swiss_addComponent(&swiss, COMPONENT_MUD, id);

    assertNotEq(component, 0);
}

static struct TestResult swiss__find_the_component__getting_existing_component() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_MUD, sizeof(char));
    swiss_init(&swiss, 1);
    win_id id = swiss_allocate(&swiss);
    char* component = swiss_addComponent(&swiss, COMPONENT_MUD, id);
    *component = 'a';

    component = swiss_getComponent(&swiss, COMPONENT_MUD, id);

    assertEq(*component, 'a');
}

static struct TestResult swiss__say_there_isnt_a_component__checking_an_entity_without_component() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_MUD, sizeof(char));
    swiss_init(&swiss, 1);
    win_id id = swiss_allocate(&swiss);

    // @CLEANUP: We can't assert on bools right now
    uint64_t has = swiss_hasComponent(&swiss, COMPONENT_MUD, id);

    assertEq(has, false);
}

static struct TestResult swiss__say_there_is_a_component__checking_an_entity_with_component() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_MUD, sizeof(char));
    swiss_init(&swiss, 1);
    win_id id = swiss_allocate(&swiss);
    char* component = swiss_addComponent(&swiss, COMPONENT_MUD, id);

    // @CLEANUP: We can't assert on bools right now
    uint64_t has = swiss_hasComponent(&swiss, COMPONENT_MUD, id);

    assertEq(has, true);
}

static struct TestResult swiss__say_there_isnt_a_component__checking_a_removed_component() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_MUD, sizeof(char));
    swiss_init(&swiss, 1);
    win_id id = swiss_allocate(&swiss);

    char* component = swiss_addComponent(&swiss, COMPONENT_MUD, id);
    swiss_removeComponent(&swiss, COMPONENT_MUD, id);

    // @CLEANUP: We can't assert on bools right now
    uint64_t has = swiss_hasComponent(&swiss, COMPONENT_MUD, id);

    assertEq(has, false);
}

static struct TestResult swiss__double_capacity__allocating_past_end() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_init(&swiss, 2);

    swiss_allocate(&swiss);
    swiss_allocate(&swiss);
    swiss_allocate(&swiss);

    assertEq(swiss.capacity, 4);
}

static struct TestResult swiss__iterate_components_in_order_abcdef__iterating_forward_over_abcdef() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_MUD, sizeof(char));
    swiss_init(&swiss, 6);

    // Insert the string
    char* str = "abcdef";
    for(char* c = str; *c != '\0'; c++) {
        win_id id = swiss_allocate(&swiss);
        char* ch = swiss_addComponent(&swiss, COMPONENT_MUD, id);
        *ch = *c;
    }

    //Read it out to a char array
    char order[6];
    size_t count = 0;
    for_components(it, &swiss,
        COMPONENT_MUD, CQ_END) {
        order[count++] = *(char*)swiss_getComponent(&swiss, COMPONENT_MUD, it.id);
    }

    assertEqString(order, "abcdef", 6);
}

static struct TestResult swiss__skip_entities_missing_components__iterating_forward() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_MUD, sizeof(char));
    swiss_setComponentSize(&swiss, COMPONENT_SHADOW, sizeof(char));
    swiss_init(&swiss, 9);

    // Insert the string
    char* str = "a_b_cde_f";
    for(char* c = str; *c != '\0'; c++) {
        win_id id = swiss_allocate(&swiss);

        char* ch = swiss_addComponent(&swiss, COMPONENT_MUD, id);
        *ch = *c;

        if(*c != '_') {
            // It doesn't matter what's in this component
            swiss_addComponent(&swiss, COMPONENT_SHADOW, id);
        }
    }

    //Read it out to a char array, hopefully skipping all the _'s
    char order[6];
    size_t count = 0;
    for_components(it, &swiss,
        COMPONENT_MUD, COMPONENT_SHADOW, CQ_END) {
        order[count++] = *(char*)swiss_getComponent(&swiss, COMPONENT_MUD, it.id);
    }

    assertEqString(order, "abcdef", 6);
}

static struct TestResult swiss__include_entities_with_components_not_required__iterating_forward() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_MUD, sizeof(char));
    swiss_setComponentSize(&swiss, COMPONENT_SHADOW, sizeof(char));
    swiss_init(&swiss, 9);

    // Insert the string
    char* str = "a_b_cde_f";
    for(char* c = str; *c != '\0'; c++) {
        win_id id = swiss_allocate(&swiss);

        char* ch = swiss_addComponent(&swiss, COMPONENT_MUD, id);
        *ch = *c;

        if(*c != '_') {
            // It doesn't matter what's in this component
            swiss_addComponent(&swiss, COMPONENT_SHADOW, id);
        }
    }

    //Read it out to a char array
    char order[9];
    size_t count = 0;
    for_components(it, &swiss,
        COMPONENT_MUD, CQ_END) {
        order[count++] = *(char*)swiss_getComponent(&swiss, COMPONENT_MUD, it.id);
    }

    assertEqString(order, "a_b_cde_f", 9);
}

struct TestResult bezier__not_crash__initializing_bezier_curve() {
    struct Bezier b;

    bezier_init(&b, 0, 1, 0, 1);

    assertYes();
}

#define NUMSAMPLES 10
struct TestResult bezier__get_identical_y_for_x__querying_on_linear_curve() {
    struct Bezier b;
    bezier_init(&b, .2, .2, .8, .8);

    const double samples[NUMSAMPLES] = {.1, .2, .3, .4, .5, .6, .7, .9, 1.0};
    double results[NUMSAMPLES];

    for(int i = 0; i < 10; i++) {
        results[i] = bezier_getSplineValue(&b, samples[i]);
    }

    assertEqArray(results, samples, NUMSAMPLES);
}
#undef NUMSAMPLES

struct TestResult bezier__get_0_for_0__querying_on_nonlinear_curve() {
    struct Bezier b;
    bezier_init(&b, .1, .5, .2, 1.2);

    double v = bezier_getSplineValue(&b, .0);

    assertEq(v, .0);
}

struct TestResult bezier__get_1_for_1__querying_on_nonlinear_curve() {
    struct Bezier b;
    bezier_init(&b, .1, .5, .2, 1.2);

    double v = bezier_getSplineValue(&b, 1.0);

    assertEq(v, 1.0);
}

uint64_t fade_size(struct Fading* fade) {
    int64_t diff = fade->tail - fade->head;
    uint64_t size;
    if(diff < 0)
        size = FADE_KEYFRAMES + diff;
    else
        size = diff;
    return size;
}

struct TestResult fading__be_empty__initialized() {
    struct Fading fade;

    fade_init(&fade, 100.0);

    assertEq(fade_size(&fade), 0);
}

struct TestResult fading__have_initial_value__initialized() {
    struct Fading fade;

    fade_init(&fade, 100.0);

    assertEq(fade.value, 100.0);
}

struct TestResult fading__be_done__initialized() {
    struct Fading fade;
    fade_init(&fade, 100.0);

    assertEq(fade_done(&fade), true);
}

struct TestResult fading__grow__adding_a_keyframe() {
    struct Fading fade;
    fade_init(&fade, 100.0);

    fade_keyframe(&fade, 10, 100);

    assertEq(fade_size(&fade), 1);
}

struct TestResult fading__maintain_size__adding_with_filled_up_buffer() {
    struct Fading fade;
    fade_init(&fade, 100.0);

    for(int i = 0; i < FADE_KEYFRAMES - 1; i++) {
        fade_keyframe(&fade, 10, 100);
    }
    uint64_t oldSize = fade_size(&fade);

    fade_keyframe(&fade, 10, 100);

    assertEq(fade_size(&fade), oldSize);
}

struct TestResult fading__set_new_head_duration__adding_with_filled_up_buffer() {
    struct Fading fade;
    fade_init(&fade, 100.0);

    for(int i = 0; i < FADE_KEYFRAMES - 1; i++) {
        fade_keyframe(&fade, 10, 100);
    }

    fade_keyframe(&fade, 10, 100);

    assertEq(fade.keyframes[fade.head].duration, -1);
}

struct TestResult fading__use_the_first_keyframe_slot__adding_the_first_keyframe() {
    struct Fading fade;
    fade_init(&fade, 100.0);

    fade_keyframe(&fade, 10, 100);

    assertEq(fade.tail, 1);
}

struct TestResult fading__set_keyframe_ignore__adding_a_keyframe() {
    struct Fading fade;
    fade_init(&fade, 100.0);

    fade_keyframe(&fade, 10, 100);

    assertEq(fade.keyframes[1].ignore, true);
}

struct TestResult fading__not_be_done__keyframe_remaining() {
    struct Fading fade;
    fade_init(&fade, 100.0);
    fade_keyframe(&fade, 10, 100);

    assertEq(fade_done(&fade), false);
}

struct TestResult win_fade__return_false__theres_nothing_to_update() {
    struct Bezier b;
    bezier_init(&b, .1, .5, .2, 1.2);
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_FADES_OPACITY, sizeof(struct FadesOpacityComponent));
    swiss_init(&swiss, 2);

    bool skip_poll = do_win_fade(&b, .1, &swiss);

    assertEq(skip_poll, false);
}

struct TestResult win_fade__return_false__all_fades_are_done() {
    struct Bezier b;
    bezier_init(&b, .1, .5, .2, 1.2);
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_FADES_OPACITY, sizeof(struct FadesOpacityComponent));
    swiss_init(&swiss, 2);
    {
        win_id wid = swiss_allocate(&swiss);
        struct FadesOpacityComponent* fo = swiss_addComponent(&swiss, COMPONENT_FADES_OPACITY, wid);
        fade_init(&fo->fade, 100.0);
    }

    bool skip_poll = do_win_fade(&b, .1, &swiss);

    assertEq(skip_poll, false);
}

struct TestResult win_fade__unignore_keyframes__a_fade_is_running() {
    struct Bezier b;
    bezier_init(&b, .1, .5, .2, 1.2);
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_FADES_OPACITY, sizeof(struct FadesOpacityComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    struct FadesOpacityComponent* fo = swiss_addComponent(&swiss, COMPONENT_FADES_OPACITY, wid);
    fade_init(&fo->fade, 100.0);
    fade_keyframe(&fo->fade, 50.0, 100.0);
    fo->fade.keyframes[1].ignore = true;

    do_win_fade(&b, 10.0, &swiss);

    assertEq(fo->fade.keyframes[1].ignore, false);
}

struct TestResult win_fade__progress_unignored_keyframes__a_fade_is_running() {
    struct Bezier b;
    bezier_init(&b, .1, .5, .2, 1.2);
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_FADES_OPACITY, sizeof(struct FadesOpacityComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    struct FadesOpacityComponent* fo = swiss_addComponent(&swiss, COMPONENT_FADES_OPACITY, wid);
    fade_init(&fo->fade, 100.0);
    fade_keyframe(&fo->fade, 50.0, 100.0);
    fo->fade.keyframes[1].ignore = false;

    do_win_fade(&b, 10.0, &swiss);

    assertEq(fo->fade.keyframes[1].time, 10.0);
}

struct TestResult win_fade__remove_completed_keyframes__a_fade_is_running() {
    struct Bezier b;
    bezier_init(&b, .1, .5, .2, 1.2);
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_FADES_OPACITY, sizeof(struct FadesOpacityComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    struct FadesOpacityComponent* fo = swiss_addComponent(&swiss, COMPONENT_FADES_OPACITY, wid);
    fade_init(&fo->fade, 100.0);
    fade_keyframe(&fo->fade, 50.0, 100.0);
    fo->fade.keyframes[1].ignore = false;

    do_win_fade(&b, 100.0, &swiss);

    assertEq(fade_size(&fo->fade), 0);
}

struct TestResult win_fade__set_a_keyframe_duration__keyframe_becomes_head() {
    struct Bezier b;
    bezier_init(&b, .1, .5, .2, 1.2);
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_FADES_OPACITY, sizeof(struct FadesOpacityComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    struct FadesOpacityComponent* fo = swiss_addComponent(&swiss, COMPONENT_FADES_OPACITY, wid);
    fade_init(&fo->fade, 100.0);
    fade_keyframe(&fo->fade, 50.0, 100.0);
    fo->fade.keyframes[1].ignore = false;

    do_win_fade(&b, 50.0, &swiss);

    assertEq(fo->fade.keyframes[fo->fade.head].duration, -1);
}

struct TestResult win_fade__skip_keyframes_superseded_by_others__a_fade_is_running() {
    struct Bezier b;
    bezier_init(&b, .1, .5, .2, 1.2);
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_FADES_OPACITY, sizeof(struct FadesOpacityComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    struct FadesOpacityComponent* fo = swiss_addComponent(&swiss, COMPONENT_FADES_OPACITY, wid);
    fade_init(&fo->fade, 100.0);
    fade_keyframe(&fo->fade, 50.0, 100.0);
    fade_keyframe(&fo->fade, 75.0, 50.0);
    fo->fade.keyframes[1].ignore = false;
    fo->fade.keyframes[2].ignore = false;

    do_win_fade(&b, 50.0, &swiss);

    assertEq(fade_done(&fo->fade), true);
}

static make_z(Swiss* swiss, Vector* wids, double* vals, int cnt) {
    for(int i = 0; i < cnt; i++) {
        win_id wid = swiss_allocate(swiss);
        vector_putBack(wids, &wid);
        struct ZComponent* z = swiss_addComponent(swiss, COMPONENT_Z, wid);
        z->z = vals[i];
    }
}

struct TestResult binaryZSearch__return_0__finding_in_empty_haystack() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_Z, sizeof(struct ZComponent));
    swiss_init(&swiss, 2);
    Vector wids;
    vector_init(&wids, sizeof(win_id), 2);

    make_z(&swiss, &wids, (double[]){}, 0);

    size_t res = binaryZSearch(&swiss, &wids, 3);

    assertEq(res, 0);
}

struct TestResult binaryZSearch__return_first_index_with_value_larger__finding_value_in_the_middle() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_Z, sizeof(struct ZComponent));
    swiss_init(&swiss, 2);
    Vector wids;
    vector_init(&wids, sizeof(win_id), 2);

    make_z(&swiss, &wids, (double[]){2., 3., 4., 5., 6.}, 5);

    size_t res = binaryZSearch(&swiss, &wids, 3.0);

    assertEq(res, 2);
}

struct TestResult binaryZSearch__return_an_index_larger_than_size__all_values_are_smaller() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_Z, sizeof(struct ZComponent));
    swiss_init(&swiss, 2);
    Vector wids;
    vector_init(&wids, sizeof(win_id), 2);

    make_z(&swiss, &wids, (double[]){5., 6., 7., 8., 9.}, 5);

    size_t res = binaryZSearch(&swiss, &wids, 100.0);

    assertEq(res, 5);
}

struct TestResult binaryZSearch__return_an_index_larger_than_size__last_value_is_equal() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_Z, sizeof(struct ZComponent));
    swiss_init(&swiss, 2);
    Vector wids;
    vector_init(&wids, sizeof(win_id), 2);

    make_z(&swiss, &wids, (double[]){5., 6., 7., 8., 9.}, 5);

    size_t res = binaryZSearch(&swiss, &wids, 9.0);

    assertEq(res, 5);
}

struct TestResult binaryZSearch__return_0__all_values_are_larger() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_Z, sizeof(struct ZComponent));
    swiss_init(&swiss, 2);
    Vector wids;
    vector_init(&wids, sizeof(win_id), 2);

    make_z(&swiss, &wids, (double[]){0., 1., 2., 3.}, 4);

    size_t res = binaryZSearch(&swiss, &wids, -1.0);

    assertEq(res, 0);
}

struct TestResult binaryZSearch__return_rightmost_element__several_values_are_equal() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_Z, sizeof(struct ZComponent));
    swiss_init(&swiss, 2);
    Vector wids;
    vector_init(&wids, sizeof(win_id), 2);

    make_z(&swiss, &wids, (double[]){2., 3., 3., 7.}, 4);

    size_t res = binaryZSearch(&swiss, &wids, 3.0);

    assertEq(res, 3);
}

struct TestResult binaryZSearch__return_smallest_value_larger_than_needle__needle_is_not_a_value() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_Z, sizeof(struct ZComponent));
    swiss_init(&swiss, 2);
    Vector wids;
    vector_init(&wids, sizeof(win_id), 2);

    make_z(&swiss, &wids, (double[]){0., 1., 2., 2.2, 3., 3., 3., 3., 7., 8., 9.}, 11);

    size_t res = binaryZSearch(&swiss, &wids, 2.3);

    assertEq(res, 4);
}

int main(int argc, char** argv) {
    vector_init(&results, sizeof(struct Test), 128);

    TEST(vector__be_empty__initialized);
    TEST(vector__grow__storing_elements);
    TEST(vector__shrink__deleting_elements);

    TEST(vector__return_a_pointer_to_the_value__getting_an_existing_index);
    TEST(vector__return_a_pointer_to_the_first_value__getting_index_0);
    TEST(vector__return_a_pointer_to_the_second_value__getting_index_1);
    TEST(vector__keep_data_linearly__storing);

#ifdef NDEBUG
    TEST(vector__return_null__getting_out_of_bounds);
#else
    TEST(vector__assert__getting_out_of_bounds);
#endif

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

    TEST(swiss__be_empty__initialized);
    TEST(swiss__grow__allocating);
    TEST(swiss__shrink__removing_item);

    TEST(swiss__save__adding_a_component);
    TEST(swiss__find_the_component__getting_existing_component);

    TEST(swiss__say_there_isnt_a_component__checking_an_entity_without_component);
    TEST(swiss__say_there_is_a_component__checking_an_entity_with_component);
    TEST(swiss__say_there_isnt_a_component__checking_a_removed_component);

    TEST(swiss__double_capacity__allocating_past_end);

    TEST(swiss__iterate_components_in_order_abcdef__iterating_forward_over_abcdef);
    TEST(swiss__skip_entities_missing_components__iterating_forward);
    TEST(swiss__include_entities_with_components_not_required__iterating_forward);

    TEST(bezier__not_crash__initializing_bezier_curve);
    TEST(bezier__get_identical_y_for_x__querying_on_linear_curve);
    TEST(bezier__get_0_for_0__querying_on_nonlinear_curve);
    TEST(bezier__get_1_for_1__querying_on_nonlinear_curve);

    TEST(fading__be_empty__initialized);
    TEST(fading__have_initial_value__initialized);
    TEST(fading__grow__adding_a_keyframe);
    TEST(fading__maintain_size__adding_with_filled_up_buffer);
    TEST(fading__set_new_head_duration__adding_with_filled_up_buffer);
    TEST(fading__be_done__initialized);
    TEST(fading__not_be_done__keyframe_remaining);
    TEST(fading__use_the_first_keyframe_slot__adding_the_first_keyframe);
    TEST(fading__set_keyframe_ignore__adding_a_keyframe);

    TEST(win_fade__return_false__theres_nothing_to_update);
    TEST(win_fade__return_false__all_fades_are_done);
    TEST(win_fade__unignore_keyframes__a_fade_is_running);
    TEST(win_fade__progress_unignored_keyframes__a_fade_is_running);
    TEST(win_fade__remove_completed_keyframes__a_fade_is_running);
    TEST(win_fade__skip_keyframes_superseded_by_others__a_fade_is_running);
    TEST(win_fade__set_a_keyframe_duration__keyframe_becomes_head);

    TEST(binaryZSearch__return_first_index_with_value_larger__finding_value_in_the_middle);
    TEST(binaryZSearch__return_an_index_larger_than_size__all_values_are_smaller);
    TEST(binaryZSearch__return_0__all_values_are_larger);
    TEST(binaryZSearch__return_rightmost_element__several_values_are_equal);
    TEST(binaryZSearch__return_smallest_value_larger_than_needle__needle_is_not_a_value);
    TEST(binaryZSearch__return_an_index_larger_than_size__last_value_is_equal);

    return test_end();
}
