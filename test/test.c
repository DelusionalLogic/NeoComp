#include "libtest.h"

#include "vector.h"
#include "compton.h"
#include "assets/face.h"

#include "systems/physical.h"
#include "windowlist.h"

#include <string.h>
#include <stdio.h>

#include <X11/Xlib-xcb.h>

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
    assertEq(value, NULL);
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

    assertEq(vector.data, (void*)0x42424242);
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
    swiss_enableAllAutoRemove(&swiss);
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

static struct TestResult swiss__iterate_elements__there_are_100_elements() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_MUD, sizeof(int));
    swiss_init(&swiss, 100);

    // Insert the string
    int check[101] = {0};
    for(int i = 0; i < 101; i++) {
        win_id id = swiss_allocate(&swiss);
        int* ch = swiss_addComponent(&swiss, COMPONENT_MUD, id);
        *ch = i;
        check[i] = i;
    }

    //Read it out to a char array
    int order[101];
    size_t count = 0;
    for_components(it, &swiss,
        COMPONENT_MUD, CQ_END) {
        order[count++] = *(int*)swiss_getComponent(&swiss, COMPONENT_MUD, it.id);
    }

    assertEqArray(order, check, 101);
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

struct TestResult physical_move__add_a_move__no_previous_move() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_PHYSICAL, sizeof(struct PhysicalComponent));
    swiss_setComponentSize(&swiss, COMPONENT_MOVE, sizeof(struct MoveComponent));
    swiss_setComponentSize(&swiss, COMPONENT_RESIZE, sizeof(struct ResizeComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    struct PhysicalComponent* p = swiss_addComponent(&swiss, COMPONENT_PHYSICAL, wid);
    p->position = (Vector2){{1, 1}};

    physics_move_window(&swiss, wid, &(Vector2){{0, 0}}, &(Vector2){{0, 0}});

    assertEq(swiss_hasComponent(&swiss, COMPONENT_MOVE, wid), true);
}

struct TestResult physical_move__not_add_a_move__no_previous_move_and_same_position() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_PHYSICAL, sizeof(struct PhysicalComponent));
    swiss_setComponentSize(&swiss, COMPONENT_MOVE, sizeof(struct MoveComponent));
    swiss_setComponentSize(&swiss, COMPONENT_RESIZE, sizeof(struct ResizeComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    struct PhysicalComponent* p = swiss_addComponent(&swiss, COMPONENT_PHYSICAL, wid);
    p->position = (Vector2){{0, 0}};

    physics_move_window(&swiss, wid, &(Vector2){{0, 0}}, &(Vector2){{0, 0}});

    assertEq(swiss_hasComponent(&swiss, COMPONENT_MOVE, wid), false);
}

struct TestResult physical_move__change_the_move__previous_move() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_PHYSICAL, sizeof(struct PhysicalComponent));
    swiss_setComponentSize(&swiss, COMPONENT_MOVE, sizeof(struct MoveComponent));
    swiss_setComponentSize(&swiss, COMPONENT_RESIZE, sizeof(struct ResizeComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    struct PhysicalComponent* p = swiss_addComponent(&swiss, COMPONENT_PHYSICAL, wid);
    p->position = (Vector2){{0, 0}};
    struct MoveComponent* m = swiss_addComponent(&swiss, COMPONENT_MOVE, wid);
    m->newPosition = (Vector2){{2, 2}};

    physics_move_window(&swiss, wid, &(Vector2){{1, 1}}, &(Vector2){{0, 0}});

    Vector2 v = (Vector2){{1, 1}};
    assertEqArray(&m->newPosition, &v, sizeof(Vector2));
}

struct TestResult physical_tick__change_size_of_window__window_was_resized() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_PHYSICAL, sizeof(struct PhysicalComponent));
    swiss_setComponentSize(&swiss, COMPONENT_RESIZE, sizeof(struct ResizeComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    struct PhysicalComponent* p = swiss_addComponent(&swiss, COMPONENT_PHYSICAL, wid);
    p->position = (Vector2){{0, 0}};
    struct ResizeComponent* r = swiss_addComponent(&swiss, COMPONENT_RESIZE, wid);
    r->newSize = (Vector2){{2, 2}};

    physics_tick(&swiss);

    Vector2 v = (Vector2){{2, 2}};
    assertEqArray(&p->size, &v, sizeof(Vector2));
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

struct TestResult commit_unmap__transition_state_to_destroying__has_destroy_event() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_STATEFUL, sizeof(struct StatefulComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    struct StatefulComponent* stateful = swiss_addComponent(&swiss, COMPONENT_STATEFUL, wid);
    stateful->state = STATE_HIDING;
    swiss_ensureComponent(&swiss, COMPONENT_DESTROY, wid);

    commit_destroy(&swiss);

    assertEq((uint64_t)stateful->state, (uint64_t)STATE_DESTROYING);
}

struct TestResult commit_unmap__not_transision__has_no_destroy_event() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_STATEFUL, sizeof(struct StatefulComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    struct StatefulComponent* stateful = swiss_addComponent(&swiss, COMPONENT_STATEFUL, wid);
    stateful->state = STATE_HIDING;
    enum WindowState beforeState = stateful->state;

    commit_destroy(&swiss);

    assertEq((uint64_t)stateful->state, (uint64_t)beforeState);
}

struct TestResult commit_unmap__transision_last_window__has_multiple_windows_and_last_has_destroy_event() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_STATEFUL, sizeof(struct StatefulComponent));
    swiss_init(&swiss, 2);

    win_id wid = swiss_allocate(&swiss);
    swiss_addComponent(&swiss, COMPONENT_STATEFUL, wid);

    wid = swiss_allocate(&swiss);
    struct StatefulComponent* stateful = swiss_addComponent(&swiss, COMPONENT_STATEFUL, wid);
    stateful->state = STATE_HIDING;
    swiss_ensureComponent(&swiss, COMPONENT_DESTROY, wid);

    commit_destroy(&swiss);

    assertEq((uint64_t)stateful->state, (uint64_t)STATE_DESTROYING);
}

struct TestResult commit_unmap__not_crash__window_with_destroy_event_has_no_state() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_STATEFUL, sizeof(struct StatefulComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    swiss_ensureComponent(&swiss, COMPONENT_DESTROY, wid);

    commit_destroy(&swiss);

    assertYes();
}

struct TestResult fullscreen_system__mark_window_fullscreen__window_is_extactly_monitor_size() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_MUD, sizeof(win));
    swiss_setComponentSize(&swiss, COMPONENT_PHYSICAL, sizeof(struct PhysicalComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    win* w = swiss_addComponent(&swiss, COMPONENT_MUD, wid);
    w->fullscreen = false;
    struct PhysicalComponent* physical = swiss_addComponent(&swiss, COMPONENT_PHYSICAL, wid);
    physical->position = (Vector2){{0, 0}};
    physical->size = (Vector2){{100, 100}};

    fullscreensystem_determine(&swiss, &(Vector2){{100, 100}});

    assertEq(w->fullscreen, true);
}

struct TestResult fullscreen_system__not_mark_window_fullscreen__window_is_small_on_monitor() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_MUD, sizeof(win));
    swiss_setComponentSize(&swiss, COMPONENT_PHYSICAL, sizeof(struct PhysicalComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    win* w = swiss_addComponent(&swiss, COMPONENT_MUD, wid);
    w->fullscreen = false;
    struct PhysicalComponent* physical = swiss_addComponent(&swiss, COMPONENT_PHYSICAL, wid);
    physical->position = (Vector2){{10, 10}};
    physical->size = (Vector2){{25, 25}};

    fullscreensystem_determine(&swiss, &(Vector2){{100, 100}});

    assertEq(w->fullscreen, false);
}

struct TestResult fullscreen_system__not_mark_window_fullscreen__window_is_large_off_monitor() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_MUD, sizeof(win));
    swiss_setComponentSize(&swiss, COMPONENT_PHYSICAL, sizeof(struct PhysicalComponent));
    swiss_init(&swiss, 1);

    win_id wid = swiss_allocate(&swiss);
    win* w = swiss_addComponent(&swiss, COMPONENT_MUD, wid);
    w->fullscreen = false;
    struct PhysicalComponent* physical = swiss_addComponent(&swiss, COMPONENT_PHYSICAL, wid);
    physical->position = (Vector2){{10, 10}};
    physical->size = (Vector2){{100, 100}};

    fullscreensystem_determine(&swiss, &(Vector2){{100, 100}});

    assertEq(w->fullscreen, false);
}

// This should be in a header instead of 10 different files
static void fetchSortedWindowsWithArr(Swiss* em, Vector* result, CType* query) {
    for_componentsArr(it, em, query) {
        vector_putBack(result, &it.id);
    }
    vector_qsort(result, window_zcmp, em);
}
#define fetchSortedWindowsWith(em, result, ...) \
    fetchSortedWindowsWithArr(em, result, (CType[]){ __VA_ARGS__ })

struct TestResult fetch_sorted_windows__fetch_2_windows_in_insert_order__increasing_z_order() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_Z, sizeof(struct ZComponent));
    swiss_init(&swiss, 2);

    win_id wids[2];
    struct ZComponent* z;

    wids[0] = swiss_allocate(&swiss);
    z = swiss_addComponent(&swiss, COMPONENT_Z, wids[0]);
    z->z = 1;
    swiss_addComponent(&swiss, COMPONENT_SHADOW_DAMAGED, wids[0]);
    wids[1] = swiss_allocate(&swiss);
    z = swiss_addComponent(&swiss, COMPONENT_Z, wids[1]);
    z->z = 2;
    swiss_addComponent(&swiss, COMPONENT_SHADOW_DAMAGED, wids[1]);

    Vector v;
    vector_init(&v, sizeof(win_id*), 2);
    fetchSortedWindowsWith(&swiss, &v, COMPONENT_SHADOW_DAMAGED, CQ_END);

    assertEqArray(vector_detach(&v), wids, 2);
}

struct TestResult fetch_sorted_windows__exclude_window__one_window_without_component() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_Z, sizeof(struct ZComponent));
    swiss_init(&swiss, 2);

    win_id wids[2];
    struct ZComponent* z;

    wids[0] = swiss_allocate(&swiss);
    z = swiss_addComponent(&swiss, COMPONENT_Z, wids[0]);
    z->z = 1;
    wids[1] = swiss_allocate(&swiss);
    z = swiss_addComponent(&swiss, COMPONENT_Z, wids[1]);
    z->z = 2;
    swiss_addComponent(&swiss, COMPONENT_SHADOW_DAMAGED, wids[1]);

    Vector v;
    vector_init(&v, sizeof(win_id*), 1);
    fetchSortedWindowsWith(&swiss, &v, COMPONENT_SHADOW_DAMAGED, CQ_END);

    assertEqArray(vector_detach(&v), &wids[1], 1);
}

struct TestResult fetch_sorted_windows__reorder_windows__windows_have_inverted_z_order() {
    Swiss swiss;
    swiss_clearComponentSizes(&swiss);
    swiss_setComponentSize(&swiss, COMPONENT_Z, sizeof(struct ZComponent));
    swiss_init(&swiss, 2);

    win_id wids[2];
    struct ZComponent* z;

    wids[1] = swiss_allocate(&swiss);
    z = swiss_addComponent(&swiss, COMPONENT_Z, wids[1]);
    z->z = 2;
    swiss_addComponent(&swiss, COMPONENT_SHADOW_DAMAGED, wids[1]);
    wids[0] = swiss_allocate(&swiss); // 0 inserted after 1
    z = swiss_addComponent(&swiss, COMPONENT_Z, wids[0]);
    z->z = 1;
    swiss_addComponent(&swiss, COMPONENT_SHADOW_DAMAGED, wids[0]);

    Vector v;
    vector_init(&v, sizeof(win_id*), 2);
    fetchSortedWindowsWith(&swiss, &v, COMPONENT_SHADOW_DAMAGED, CQ_END);

    assertEqArray(vector_detach(&v), wids, 2);
}

size_t qCursor = 0;
Vector eventQ;
void* windowAttrs;

Window RootWindowHook(Display* dpy, Screen scr) {
    return 0;
}

void XNextEventHook(Display* dpy, XEvent* ev) {
    *ev = *(XEvent*)vector_get(&eventQ, qCursor);
    qCursor++;
}

bool XGetWindowAttributesHook(Display* dpy, Window window, XWindowAttributes* attrs) {
    XWindowAttributes** value;
    JLG(value, windowAttrs, window);
    if(value != NULL) {
        *attrs = **value;
        return true;
    }

    return XGetWindowAttributes(dpy, window, attrs);
}

void setWindowAttr(Window window, XWindowAttributes* attrs) {
    XWindowAttributes** value;
    JLI(value, windowAttrs, window);
    *value = attrs;
}

void readAllEventsInto(struct X11Context* xctx, Vector* events) {
    struct Event ev;
    while(vector_size(&eventQ) > qCursor || vector_size(&xctx->eventBuf) > xctx->readCursor) {
        xorg_nextEvent(xctx, &ev);
        if(events != NULL && ev.type != ET_NONE)
            vector_putBack(events, &ev);
    }
}

Vector* readAllEvents(struct X11Context* xctx) {
    Vector* events = malloc(sizeof(Vector));
    vector_init(events, sizeof(struct Event), 1);
    readAllEventsInto(xctx, events);
    return events;
}

struct TestResult xorg__emit_add_win_event__new_window_is_created() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .serial = 1,
        .window = 1,
    });

    struct Event ev;
    xorg_nextEvent(&ctx, &ev);

    assertEq((uint64_t)ev.type, (uint64_t)ET_ADD);
}

struct TestResult xorg__emit_no_event__new_window_is_child_of_other() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(2, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .serial = 1,
        .parent = 10,
        .window = 2,
    });

    struct Event ev;
    xorg_nextEvent(&ctx, &ev);

    assertEq((uint64_t)ev.type, (uint64_t)ET_NONE);
}

struct TestResult xorg__emit_destroy_event__closed_window_was_created_as_active() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(2, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .serial = 1,
        .parent = 0,
        .window = 2,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XDestroyWindowEvent){
        .type = DestroyNotify,
        .serial = 1,
        .window = 2,
    });
    Vector* events = readAllEvents(&ctx);


    assertEvents(events,
        (struct Event){.type = ET_DESTROY},
    );
}

struct TestResult xorg__emit_no_event__closed_window_was_not_created_as_active() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(2, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .serial = 1,
        .parent = 10,
        .window = 2,
    });
    vector_putBack(&eventQ, &(XDestroyWindowEvent){
        .type = DestroyNotify,
        .serial = 1,
        .window = 2,
    });

    struct Event ev;
    xorg_nextEvent(&ctx, &ev);
    xorg_nextEvent(&ctx, &ev);

    assertEq((uint64_t)ev.type, (uint64_t)ET_NONE);
}

struct TestResult xorg__emit_add_win_event__inactive_window_gets_reparented_to_root() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(2, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .serial = 1,
        .parent = 10,
        .window = 2,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XReparentEvent){
        .type = ReparentNotify,
        .serial = 2,
        .parent = 0,
        .window = 2,
    });

    Vector* events = readAllEvents(&ctx);
    assertEvents(events,
        (struct Event){.type = ET_ADD},
    );
}

struct TestResult xorg__emit_destroy_win_event__active_window_gets_reparented_to_other_window() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(2, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .serial = 1,
        .window = 2,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XReparentEvent){
        .type = ReparentNotify,
        .serial = 2,
        .parent = 1,
        .window = 2,
    });

    Vector* events = readAllEvents(&ctx);
    assertEvents(events,
        (struct Event){.type = ET_DESTROY},
    );
}

struct TestResult xorg__emit_nothing__subwindow_gets_reparented_to_other_window() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    setWindowAttr(3, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .serial = 1,
        .parent = 0,
        .window = 2,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .serial = 1,
        .parent = 0,
        .window = 1,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .serial = 1,
        .parent = 1,
        .window = 3,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XReparentEvent){
        .type = ReparentNotify,
        .serial = 2,
        .parent = 2,
        .window = 3,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
    );
}

struct TestResult xorg__not_emit_get_client__frame_window_gets_client_atom() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .serial = 1,
        .window = 1,
        .parent = 0,
    });
    readAllEvents(&ctx);
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .serial = 2,
        .window = 1,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
    );
}

struct TestResult xorg__emit_get_client__child_window_gets_client_atom() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 2,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_CLIENT, .cli.xid = 1, .cli.client_xid = 2}
    );
}

struct TestResult xorg__emit_get_client__client_window_becomes_child_of_frame() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 2,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XReparentEvent){
        .type = ReparentNotify,
        .window = 2,
        .parent = 1,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_DESTROY},
        (struct Event){.type = ET_CLIENT, .cli.xid = 1, .cli.client_xid = 2}
    );
}

struct TestResult xorg__emit_get_client__subsubwindow_becomes_client() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    setWindowAttr(3, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 3,
        .parent = 2,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 3,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_CLIENT, .cli.xid = 1, .cli.client_xid = 3}
    );
}

struct TestResult xorg__emit_get_client__client_reparents_to_subwindow() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    setWindowAttr(3, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 3,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 3,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XReparentEvent){
        .type = ReparentNotify,
        .window = 3,
        .parent = 2,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_DESTROY},
        (struct Event){.type = ET_CLIENT, .cli.xid = 1, .cli.client_xid = 3}
    );
}

struct TestResult xorg__emit_get_client__parent_of_client_gets_reparented_to_frame() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    setWindowAttr(3, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 3,
        .parent = 2,
    });
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 3,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XReparentEvent){
        .type = ReparentNotify,
        .window = 2,
        .parent = 1,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_DESTROY},
        (struct Event){.type = ET_CLIENT, .cli.xid = 1, .cli.client_xid = 3}
    );
}

struct TestResult xorg__not_emit_get_client__window_becomes_client_under_frame_with_closer_client() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    setWindowAttr(3, &attr);
    setWindowAttr(4, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 3,
        .parent = 1,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 4,
        .parent = 3,
    });
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 2,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 4,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
    );
}

struct TestResult xorg__not_emit_get_client__client_reparents_under_frame_with_closer_client() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    setWindowAttr(3, &attr);
    setWindowAttr(4, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 3,
        .parent = 1,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 4,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 2,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 4,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XReparentEvent){
        .type = ReparentNotify,
        .window = 4,
        .parent = 3,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_DESTROY},
    );
}

struct TestResult xorg__emit_get_client__client_reparents_to_other_frame() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    setWindowAttr(3, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 3,
        .parent = 2,
    });
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 3,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XReparentEvent){
        .type = ReparentNotify,
        .window = 3,
        .parent = 1,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        // @TODO: Add expected remove client here
        (struct Event){.type = ET_CLIENT, .cli.xid = 1, .cli.client_xid = 3}
    );
}

struct TestResult xorg__emit_get_client__current_client_loses_atom() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    setWindowAttr(3, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 3,
        .parent = 2,
    });
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 2,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 3,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 2,
        .atom = atoms.atom_client,
        .state = PropertyDelete,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_CLIENT, .cli.xid = 1, .cli.client_xid = 3}
    );
}

struct TestResult xorg__not_emit_get_client__distant_client_loses_atom() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    setWindowAttr(3, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 3,
        .parent = 2,
    });
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 2,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 3,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 3,
        .atom = atoms.atom_client,
        .state = PropertyDelete,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
    );
}

struct TestResult xorg__emit_restack__window_placed_on_top() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XCirculateEvent){
        .type = CirculateNotify,
        .window = 1,
        .place = PlaceOnTop,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_RESTACK, .restack.xid = 1, .restack.loc = LOC_HIGHEST}
    );
}

struct TestResult xorg__emit_restack__window_placed_on_bottom() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XCirculateEvent){
        .type = CirculateNotify,
        .window = 1,
        .place = PlaceOnBottom,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_RESTACK, .restack.xid = 1, .restack.loc = LOC_LOWEST}
    );
}

struct TestResult xorg__not_emit_restack__restacked_window_is_not_frame() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XCirculateEvent){
        .type = CirculateNotify,
        .window = 2,
        .place = PlaceOnBottom,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
    );
}

struct TestResult xorg__emit_map__frame_is_mapped() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XMapEvent){
        .type = MapNotify,
        .window = 1,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_MAP, .map.xid = 1}
    );
}

struct TestResult xorg__not_emit_map__subwindow_is_mapped() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XMapEvent){
        .type = MapNotify,
        .window = 2,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
    );
}

struct TestResult xorg__not_emit_unmap__subwindow_is_unmapped() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XUnmapEvent){
        .type = UnmapNotify,
        .window = 2,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
    );
}

struct TestResult xorg__emit_map__mapped_subwindow_becomes_frame() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    vector_putBack(&eventQ, &(XMapEvent){
        .type = MapNotify,
        .window = 2,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XReparentEvent){
        .type = ReparentNotify,
        .serial = 2,
        .parent = 0,
        .window = 2,
    });

    Vector* events = readAllEvents(&ctx);
    assertEvents(events,
        (struct Event){.type = ET_ADD},
        (struct Event){.type = ET_MAP, .map.xid = 2}
    );
}

struct TestResult xorg__emit_wintype__name_atom_changes() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 1,
        .atom = atoms.atom_name,
        .state = PropertyNewValue,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_WINTYPE, .wintype.xid = 1}
    );
}

struct TestResult xorg__emit_wintype_and_winclass__class_atom_changes() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 1,
        .atom = atoms.atom_class,
        .state = PropertyNewValue,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_WINTYPE, .wintype.xid = 1},
        (struct Event){.type = ET_WINCLASS, .winclass.xid = 1}
    );
}

struct TestResult xorg__emit_wintype__name_atom_changes_on_client() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 2,
        .atom = atoms.atom_client,
        .state = PropertyNewValue,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 2,
        .atom = atoms.atom_name,
        .state = PropertyNewValue,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_WINTYPE, .wintype.xid = 1}
    );
}

struct TestResult xorg__not_emit_wintype__name_atom_changes_on_filler() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    setWindowAttr(2, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 2,
        .parent = 1,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XPropertyEvent){
        .type = PropertyNotify,
        .window = 2,
        .atom = atoms.atom_name,
        .state = PropertyNewValue,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
    );
}

struct TestResult xorg__emit_damage__window_is_damaged() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XDamageNotifyEvent){
        .type = ctx.capabilities.event[PROTO_DAMAGE] - XDamageNotify,
        .drawable = 1,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_DAMAGE, .damage.xid = 1}
    );
}

struct TestResult xorg__emit_damage__damaged_window_is_unmapped() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XDamageNotifyEvent){
        .type = ctx.capabilities.event[PROTO_DAMAGE] - XDamageNotify,
        .drawable = 1,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_DAMAGE, .damage.xid = 1}
    );
}

struct TestResult xorg__emit_damage__unmapped_window_is_damaged() {
    struct X11Context ctx;
    struct Atoms atoms;
    Display* dpy = XOpenDisplay(NULL);
    xorgContext_init(&ctx, dpy, DefaultScreen(dpy), &atoms);

    XWindowAttributes attr = {
        .class = InputOutput,
    };
    setWindowAttr(1, &attr);
    vector_putBack(&eventQ, &(XCreateWindowEvent){
        .type = CreateNotify,
        .window = 1,
        .parent = 0,
    });
    readAllEvents(&ctx);

    vector_putBack(&eventQ, &(XDamageNotifyEvent){
        .type = ctx.capabilities.event[PROTO_DAMAGE] - XDamageNotify,
        .drawable = 1,
    });
    Vector* events = readAllEvents(&ctx);

    assertEvents(events,
        (struct Event){.type = ET_DAMAGE, .damage.xid = 1}
    );
}

int main(int argc, char** argv) {
    test_select(argc, argv);

    vector_init(&eventQ, sizeof(XEvent), 8);

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
    TEST(swiss__iterate_elements__there_are_100_elements);

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

    TEST(physical_move__add_a_move__no_previous_move);
    TEST(physical_move__not_add_a_move__no_previous_move_and_same_position);
    TEST(physical_move__change_the_move__previous_move);

    TEST(physical_tick__change_size_of_window__window_was_resized);

    TEST(binaryZSearch__return_first_index_with_value_larger__finding_value_in_the_middle);
    TEST(binaryZSearch__return_an_index_larger_than_size__all_values_are_smaller);
    TEST(binaryZSearch__return_0__all_values_are_larger);
    TEST(binaryZSearch__return_rightmost_element__several_values_are_equal);
    TEST(binaryZSearch__return_smallest_value_larger_than_needle__needle_is_not_a_value);
    TEST(binaryZSearch__return_an_index_larger_than_size__last_value_is_equal);

    TEST(commit_unmap__transition_state_to_destroying__has_destroy_event);
    TEST(commit_unmap__not_transision__has_no_destroy_event);
    TEST(commit_unmap__transision_last_window__has_multiple_windows_and_last_has_destroy_event);
    TEST(commit_unmap__not_crash__window_with_destroy_event_has_no_state);

    TEST(fullscreen_system__mark_window_fullscreen__window_is_extactly_monitor_size);
    TEST(fullscreen_system__not_mark_window_fullscreen__window_is_small_on_monitor);
    TEST(fullscreen_system__not_mark_window_fullscreen__window_is_large_off_monitor);

    TEST(fetch_sorted_windows__fetch_2_windows_in_insert_order__increasing_z_order);
    TEST(fetch_sorted_windows__exclude_window__one_window_without_component);
    TEST(fetch_sorted_windows__reorder_windows__windows_have_inverted_z_order);

    TEST(xorg__emit_add_win_event__new_window_is_created);
    TEST(xorg__emit_no_event__new_window_is_child_of_other);
    TEST(xorg__emit_destroy_event__closed_window_was_created_as_active);
    TEST(xorg__emit_no_event__closed_window_was_not_created_as_active);

    TEST(xorg__emit_add_win_event__inactive_window_gets_reparented_to_root);
    TEST(xorg__emit_destroy_win_event__active_window_gets_reparented_to_other_window);
    TEST(xorg__not_emit_get_client__frame_window_gets_client_atom);
    TEST(xorg__emit_get_client__child_window_gets_client_atom);
    TEST(xorg__emit_get_client__client_window_becomes_child_of_frame);

    TEST(xorg__emit_nothing__subwindow_gets_reparented_to_other_window);
    TEST(xorg__emit_get_client__client_reparents_to_subwindow);
    TEST(xorg__emit_get_client__subsubwindow_becomes_client);
    TEST(xorg__emit_get_client__parent_of_client_gets_reparented_to_frame);
    TEST(xorg__not_emit_get_client__window_becomes_client_under_frame_with_closer_client);
    TEST(xorg__not_emit_get_client__client_reparents_under_frame_with_closer_client);
    TEST(xorg__emit_get_client__client_reparents_to_other_frame);

    TEST(xorg__emit_get_client__current_client_loses_atom);
    TEST(xorg__not_emit_get_client__distant_client_loses_atom);

    TEST(xorg__emit_restack__window_placed_on_top);
    TEST(xorg__emit_restack__window_placed_on_bottom);
    TEST(xorg__not_emit_restack__restacked_window_is_not_frame);

    TEST(xorg__emit_map__frame_is_mapped);

    // These don't work yet
    /* TEST(xorg__not_emit_map__subwindow_is_mapped); */
    /* TEST(xorg__not_emit_unmap__subwindow_is_unmapped); */
    TEST(xorg__emit_map__mapped_subwindow_becomes_frame);

    TEST(xorg__emit_wintype__name_atom_changes);
    TEST(xorg__emit_wintype_and_winclass__class_atom_changes);
    TEST(xorg__emit_wintype__name_atom_changes_on_client);
    TEST(xorg__not_emit_wintype__name_atom_changes_on_filler);
    TEST(xorg__emit_damage__window_is_damaged);
    TEST(xorg__emit_damage__damaged_window_is_unmapped);
    return test_end();
}
