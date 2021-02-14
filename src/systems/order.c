#include "order.h"

#include "window.h"

void ordersystem_init(struct Order* order) {
    vector_init(&order->order, sizeof(win_id), 512);
}

void ordersystem_delete(struct Order* order) {
    vector_kill(&order->order);
}

// @CLEANUP: Should be deffered until the event loop, but we might get a restack
// event before that which would fuck up if this didn't run before. To defer
// this we also need to defer restacking
void ordersystem_add(struct Order* order, win_id wid) {
    vector_putBack(&order->order, &wid);
}

void ordersystem_restack(struct Order* order, enum RestackLocation loc, win_id w_id, win_id above_id) {
    size_t w_loc;
    size_t new_loc;
    if(loc == LOC_BELOW) {
        if(above_id == -1)
            return;

        size_t above_loc = 0;

        size_t index;
        win_id* t = vector_getFirst(&order->order, &index);
        while(t != NULL) {
            if(*t == w_id)
                w_loc = index;

            if(*t == above_id)
                above_loc = index;
            t = vector_getNext(&order->order, &index);
        }

        // Circulate moves the windows between the src and target, so we
        // have to move one after the target when we are moving backwards
        if(above_loc < w_loc) {
            new_loc = above_loc + 1;
        } else {
            new_loc = above_loc;
        }
    } else {
        w_loc = vector_find_uint64(&order->order, w_id);

        if (loc == LOC_HIGHEST) {
            new_loc = vector_size(&order->order) - 1;
        } else {
            new_loc = 0;
        }
    }

    if(w_loc == new_loc)
        return;

    vector_circulate(&order->order, w_loc, new_loc);
}

void ordersystem_tick(Swiss* em, struct Order* order) {
    for_components(it, em,
            COMPONENT_STATEFUL, CQ_END) {
        struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        if(stateful->state == STATE_DESTROYED) {
            size_t order_index = vector_find_uint64(&order->order, it.id);
            vector_remove(&order->order, order_index);
        }
    }
}
