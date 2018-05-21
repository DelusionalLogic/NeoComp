#pragma once

#include "common.h"

#include "texture.h"
#include "framebuffer.h"

void win_calc_shadow(session_t* ps, win* w);
void win_paint_shadow(session_t* ps, win* w, const Vector2* pos, const Vector2* size, float z);

int shadow_cache_init(struct glx_shadow_cache* cache);
void shadow_cache_delete(struct glx_shadow_cache* cache);
