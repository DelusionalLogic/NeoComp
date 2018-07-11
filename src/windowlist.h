#pragma once

#include "common.h"

void windowlist_draw(session_t* ps, Vector* paints, float* z);
void windowlist_drawoverlap(session_t* ps, win* head, win* overlap, float* z);
void windowlist_updateStencil(session_t* ps, Vector* paints);
void windowlist_updateShadow(session_t* ps, Vector* paints);
void windowlist_updateBlur(session_t* ps, Vector* paints);
