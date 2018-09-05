#pragma once

#include "common.h"
#include "swiss.h"

Vector2 X11_rectpos_to_gl(session_t *ps, const Vector2* xpos, const Vector2* size);

void windowlist_drawBackground(session_t* ps, Vector* opaque);
void windowlist_drawTransparent(session_t* ps, Vector* transparent);
void windowlist_drawTint(session_t* ps);
void windowlist_draw(session_t* ps, Vector* order);
void windowlist_drawShadow(session_t* ps, Vector* order);
void windowlist_updateStencil(session_t* ps, Vector* paints);
void windowlist_updateBlur(session_t* ps);
