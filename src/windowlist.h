#pragma once

#include "common.h"

void windowlist_drawBackground(session_t* ps, Vector* order);
void windowlist_draw(session_t* ps, Vector* order);
void windowlist_updateStencil(session_t* ps, Vector* paints);
void windowlist_updateBlur(session_t* ps, Vector* paints);
