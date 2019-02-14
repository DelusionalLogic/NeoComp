#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "vmath.h"
#include "swiss.h"
#include "timer.h"

#include "texture.h"
#include "buffer.h"

void draw_component_debug(Swiss* em, Vector2* rootSize);

struct DebugGraphState {
    struct BufferObject bo;
    struct Texture tex;

    double avg;
    double *data;

    size_t cursor;
    size_t width;
};

void init_debug_graph(struct DebugGraphState* state);
void draw_debug_graph(struct DebugGraphState* state, Vector2* pos);
void update_debug_graph(struct DebugGraphState* state, timestamp startTime);
