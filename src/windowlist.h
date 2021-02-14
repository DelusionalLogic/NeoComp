#pragma once

#include "common.h"
#include "swiss.h"

void windowlist_drawBackground(session_t* ps, Vector* opaque);
void windowlist_drawTransparent(session_t* ps, Vector* transparent);
void windowlist_drawTint(session_t* ps);
void windowlist_draw(session_t* ps, Vector* order);
void windowlist_updateStencil(session_t* ps, Vector* paints);
void windowlist_updateBlur(session_t* ps);

size_t binaryZSearch(Swiss* em, const Vector* candidates, double needle);
void windowlist_findbehind(Swiss* win_list, const Vector* windows, const win_id overlap, Vector* overlaps);

void windowlist_drawDebug(Swiss* em, session_t* ps);
