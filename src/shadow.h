#pragma once

#include "common.h"

#include "texture.h"
#include "framebuffer.h"

void window_shadow(session_t* ps, win* w, const Vector2* pos, const Vector2* size);

void shadow_cache_delete(struct glx_shadow_cache* cache);
