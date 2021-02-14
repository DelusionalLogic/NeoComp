#pragma once

#include "swiss.h"
#include "xorg.h"

#include "vector.h"

struct Order {
    Vector order;
};

void ordersystem_init(struct Order* order);
void ordersystem_delete(struct Order* order);
void ordersystem_add(struct Order* order, win_id wid);
void ordersystem_restack(struct Order* order, enum RestackLocation loc, win_id w_id, win_id above_id);
void ordersystem_tick(Swiss* em, struct Order* order);
