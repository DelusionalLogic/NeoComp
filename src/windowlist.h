#pragma once

#include "common.h"

void windowlist_draw(session_t* ps, win* head, float* z);
void windowlist_drawoverlap(session_t* ps, win* head, win* overlap, float* z);
