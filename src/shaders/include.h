#pragma once

#include <GL/glx.h>

struct shader_uniform_info {
    char* name;
    size_t offset;
};

struct shader_type_info {
    char* name;
    size_t size;
    size_t member_count;
    struct shader_uniform_info members[];
};

#define HEADER "downsample.h"
#include "shader.h"
#undef HEADER

#define HEADER "upsample.h"
#include "shader.h"
#undef HEADER
