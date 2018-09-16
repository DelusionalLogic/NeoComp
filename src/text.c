#include "text.h"

#include "assets/assets.h"
#include "assets/shader.h"

#include "shaders/shaderinfo.h"

#include "renderutil.h"

struct Font debug_font;

int font_load(struct Font* font, char* filename) {
    FT_Library ft;
    if(FT_Init_FreeType(&ft)) {
        printf("Failed initializing freetype\n");
        return 1;
    }

    FT_Face face;
    if(FT_New_Face(ft, filename, 0, &face)) {
        printf("Failed loading font face %s\n", filename);
        FT_Done_FreeType(ft);
        return 1;
    }
    // @CLEANUP: We should allow the caller to set the fontsize somehow. But
    // for now this is fine
    font->size = 12;
    FT_Set_Pixel_Sizes(face, 0, font->size);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for(uint8_t i = 0; i < 128; i++) {
        if(FT_Load_Char(face, i, FT_LOAD_RENDER)) {
            printf("Failed loading char '%c'\n", i);
            FT_Done_FreeType(ft);
            FT_Done_Face(face);
            return 1;
        }
        struct Character* chara = &font->characters[i];

        // Initialize texture with no storage, and initialize the storage
        // afterwards, since we are GL_RED
        if(texture_init(&chara->texture, GL_TEXTURE_2D, NULL) != 0) {
            // @LEAK: Lets just leak the whole font here for now. It should
            // never happend right?
            printf("Failed initializing texture for letter %c\n", i);
            return 1;
        }
        chara->texture.size.x = face->glyph->bitmap.width;
        chara->texture.size.y = face->glyph->bitmap.rows;
        chara->bearing.x = face->glyph->bitmap_left;
        chara->bearing.y = face->glyph->bitmap_top;
        chara->advance = face->glyph->advance.x >> 6;

        glTexImage2D(
            chara->texture.target,
            0,
            GL_RED,
            chara->texture.size.x,
            chara->texture.size.y,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            face->glyph->bitmap.buffer
        );
    }
    return 0;
}

void font_unload(struct Font* font) {
    for(uint8_t i = 0; i < 128; i++) {
        struct Character* chara = &font->characters[i];
        texture_delete(&chara->texture);
    }
}

void text_debug_load(char* filename) {
    font_load(&debug_font, filename);
}

void text_debug_unload() {
    font_unload(&debug_font);
}

void text_size(const struct Font* font, const char* text, const Vector2* scale, Vector2* size) {
    const float line_height = scale->y * font->size;
    size->y = line_height;

    size_t text_len = strlen(text);
    for(int i = 0; i < text_len; i++) {
        const struct Character* ch = &font->characters[text[i]];
        size->x += ch->advance;
    }
}

void text_draw(const struct Font* font, const char* text, const Vector2* position, const Vector2* scale) {
    text_draw_colored(font, text, position, scale, &(Vector3){{1.0, 1.0, 1.0}});
}

void text_draw_colored(const struct Font* font, const char* text, const Vector2* position, const Vector2* scale, const Vector3* color) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
    struct face* face = assets_load("window.face");

    struct shader_program* text_program = assets_load("text.shader");
    if(text_program->shader_type_info != &text_info) {
        printf_errf("Shader was not a text shader\n");
        return;
    }

    struct Text* text_type = text_program->shader_type;

    shader_set_future_uniform_bool(text_type->flip, true);
    shader_set_future_uniform_float(text_type->opacity, (float)1.0);
    shader_set_future_uniform_sampler(text_type->tex_scr, 0);
    shader_set_future_uniform_vec3(text_type->color, color);

    shader_use(text_program);

    Vector2 pen = *position;
    size_t text_len = strlen(text);
    for(int i = 0; i < text_len; i++) {
        const struct Character* letter = &font->characters[text[i]];

        texture_bind(&letter->texture, GL_TEXTURE0);

        {
            Vector3 pos = vec3_from_vec2(&pen, 0.0);
            pos.x += letter->bearing.x * scale->x;
            pos.y -= (letter->texture.size.y - letter->bearing.y) * scale->y;
            Vector2 size = letter->texture.size;
            vec2_mul(&size, scale);
            draw_rect(face, text_type->mvp, pos, size);
        }
        pen.x += letter->advance * scale->x;
    }
}
