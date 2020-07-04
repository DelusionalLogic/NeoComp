#include "physical.h"

#include "profiler/zone.h"

DECLARE_ZONE(physics_move);
DECLARE_ZONE(physics_tick);

void physics_move_window(Swiss* em, win_id wid, Vector2* pos, Vector2* size) {
    zone_scope(&ZONE_physics_move);
    struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, wid);
    if(swiss_hasComponent(em, COMPONENT_MOVE, wid)) {
        // If we already have a move, just override it
        // @SPEED: We might want to deduplicate before we do this
        struct MoveComponent* move = swiss_getComponent(em, COMPONENT_MOVE, wid);
        move->newPosition = *pos;
    } else if(!vec2_eq(&physical->position, pos)) {
        // Only add a move if the reconfigure has a new position
        struct MoveComponent* move = swiss_addComponent(em, COMPONENT_MOVE, wid);
        move->newPosition = *pos;
    }

    if(swiss_hasComponent(em, COMPONENT_RESIZE, wid)) {
        // If we already have a resize, just override it
        // @SPEED: We might want to deduplicate before we do this
        struct ResizeComponent* resize = swiss_getComponent(em, COMPONENT_RESIZE, wid);
        resize->newSize = *size;
    } else if(!vec2_eq(&physical->size, size)) {
        // Only add a resize if the reconfigure has a size
        struct ResizeComponent* resize = swiss_addComponent(em, COMPONENT_RESIZE, wid);
        resize->newSize = *size;
    }
}

void physics_tick(Swiss* em) {
    zone_scope(&ZONE_physics_tick);
    for_components(it, em,
            COMPONENT_MOVE, COMPONENT_PHYSICAL, CQ_END) {
        struct MoveComponent* move = swiss_getComponent(em, COMPONENT_MOVE, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

        physical->position = move->newPosition;
    }

    for_components(it, em,
            COMPONENT_RESIZE, COMPONENT_PHYSICAL, CQ_END) {
        struct ResizeComponent* resize = swiss_getComponent(em, COMPONENT_RESIZE, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

        physical->size = resize->newSize;
    }
}
