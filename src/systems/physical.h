#pragma once

#include "swiss.h"
#include "vmath.h"
#include "window.h"

void physics_move_window(Swiss* em, win_id wid, Vector2* pos, Vector2* size);
void physics_tick(Swiss* em);
