#pragma once

#include "common.h"

#include "math.h"

#include <ft2build.h>
#include FT_FREETYPE_H

struct Character {
    struct Texture texture;
    Vector2 bearing;
    float advance;
};

struct Font {
    char* name;
    struct Character characters[128];
};

extern struct Font debug_font;

int font_load(struct Font* font, char* filename);
void text_debug_load(char* filename);
void text_draw(struct Font* font, char* text, Vector2* position, Vector2* size);
