#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "zone.h"
#include "../vmath.h"

void profiler_render(struct ProgramZone* root, const Vector2* rootSize);
