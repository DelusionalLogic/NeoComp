#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "vmath.h"
#include "swiss.h"
#include "timer.h"

#include "texture.h"
#include "buffer.h"

#include "xorg.h"

void draw_component_debug(Swiss* em, Vector2* rootSize);

#define GRAPHS 2
struct DebugGraphState {
    struct BufferObject bo[GRAPHS];
    struct Texture tex[GRAPHS];

    double avg[GRAPHS];
    double *data[GRAPHS];

    size_t cursor;
    size_t width;

    struct XResourceUsage xdata;
};

void init_debug_graph(struct DebugGraphState* state);
void draw_debug_graph(struct DebugGraphState* state, Vector2* pos);
void update_debug_graph(struct DebugGraphState* state, timestamp startTime, struct X11Context* xctx);
void debug_mark_draw();
