#include "debug.h"

#include "renderutil.h"
#include "text.h"
#include "window.h"
#include "shaders/shaderinfo.h"
#include "assets/assets.h"
#include "assets/shader.h"

#include "intercept/xorg.h"

const Vector4 header_bg = {{ 0.1, 0.1, 0.12, .7 }};
const Vector4 content_bg = {{ 0.2, 0.2, 0.24, .6 }};

static Vector2 X11_rectpos_to_gl(const Vector2* rootSize, const Vector2* xpos, const Vector2* size) {
    Vector2 glpos = {{
        xpos->x, rootSize->y - xpos->y - size->y
    }};
    return glpos;
}

char *component_names[NUM_COMPONENT_TYPES] = {
    "[Meta]",
    "[Mud]",
    "[Physical]",
    "[Z]",
    "[Binds Texture]",
    "[Textured]",
    "[Tracks Window]",
    "[Has Client]",
    "[Shadow]",
    "[Blur]",
    "[Tint]",
    "[Opacity]",
    "[Fades Opacity]",
    "[BG Opacity]",
    "[Fades BG Opacity]",
    "[Dim]",
    "[Fades Dim]",
    "[Redirected]",
    "[Shaped]",
    "[Stateful]",
    "[Debugged]",
    "[Transitioning]",

    "[New]",
    "[Map]",
    "[Unmap]",
    "[Bypass]",
    "[Destroy]",
    "[Move]",
    "[Resize]",
    "[Blur Damaged]",
    "[Content Damaged]",
    "[Shadow Damaged]",
    "[Shape Damaged]",
    "[Focus Changed]",
    "[Wintype Changed]",
    "[Class Changed]",
};

static void draw_generic_component(Swiss* em, enum ComponentType ctype) {
    Vector2 scale = {{1, 1}};
    char buffer[128];

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        snprintf(buffer, 128, "%s", component_names[ctype]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
}

static void draw_state_component(Swiss* em, enum ComponentType ctype) {
    Vector2 scale = {{1, 1}};
    char buffer[128];

    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_STATEFUL, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        snprintf(buffer, 128, "%s", component_names[ctype]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_STATEFUL, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct StatefulComponent* state = swiss_getComponent(em, COMPONENT_STATEFUL, it.id);

        snprintf(buffer, 128, "    %s", StateNames[state->state]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
}

static void draw_dim_component(Swiss* em, enum ComponentType ctype) {
    Vector2 scale = {{1, 1}};
    char buffer[128];

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        snprintf(buffer, 128, "%s", component_names[ctype]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct DimComponent* dim = swiss_getComponent(em, ctype, it.id);

        snprintf(buffer, 128, "    %f", dim->dim);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
}

static void draw_opacity_component(Swiss* em, enum ComponentType ctype) {
    Vector2 scale = {{1, 1}};
    char buffer[128];

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        snprintf(buffer, 128, "%s", component_names[ctype]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct OpacityComponent* opacity = swiss_getComponent(em, ctype, it.id);

        snprintf(buffer, 128, "    %f", opacity->opacity);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
}

static void draw_mud_component(Swiss* em, enum ComponentType ctype) {
    Vector2 scale = {{1, 1}};
    char buffer[128];

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        snprintf(buffer, 128, "%s", component_names[ctype]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct _win* mud = swiss_getComponent(em, ctype, it.id);

        snprintf(buffer, 128, "    Type: %s", WINTYPES[mud->window_type]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
}

static void draw_transitioning_component(Swiss* em, enum ComponentType ctype) {
    Vector2 scale = {{1, 1}};
    char buffer[128];

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        snprintf(buffer, 128, "%s", component_names[ctype]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct TransitioningComponent* t = swiss_getComponent(em, ctype, it.id);

        snprintf(buffer, 128, "    Time: %f", t->time);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct TransitioningComponent* t = swiss_getComponent(em, ctype, it.id);

        snprintf(buffer, 128, "    Duration: %f", t->duration);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
}

static void draw_focus_change_component(Swiss* em, enum ComponentType ctype) {
    Vector2 scale = {{1, 1}};
    char buffer[128];

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        snprintf(buffer, 128, "%s", component_names[ctype]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct FocusChangedComponent* f = swiss_getComponent(em, ctype, it.id);

        snprintf(buffer, 128, "    NewOpacity: %f", f->newOpacity);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
}

static void draw_physical_component(Swiss* em, enum ComponentType ctype) {
    Vector2 scale = {{1, 1}};
    char buffer[128];

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        snprintf(buffer, 128, "%s", component_names[ctype]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct PhysicalComponent* p = swiss_getComponent(em, ctype, it.id);

        snprintf(buffer, 128, "    Geom: %fx%f %f+%f", p->size.x, p->size.y, p->position.x, p->position.y);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }
}

static void draw_shaped_component(Swiss* em, enum ComponentType ctype) {
    Vector2 scale = {{1, 1}};
    char buffer[128];

    // @CLEANUP @PERFORMANCE: We are doing snprintf twice here for the same
    // strings
    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        snprintf(buffer, 128, "%s", component_names[ctype]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);

        debug->currentHeight = size.y + 10;
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        Vector2 size = {{200, debug->currentHeight}};
        Vector3 pos = {{debug->pen.x, debug->pen.y - size.y, 0}};

        struct face* face = assets_load("window.face");
        draw_colored_rect(face, &pos, &size, &header_bg);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        snprintf(buffer, 128, "%s", component_names[ctype]);

        debug->pen.y -= debug->currentHeight;

        text_draw(&debug_font, buffer, &(Vector2){{debug->pen.x, debug->pen.y + 5}}, &scale);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        debug->currentHeight = 0;
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct ShapedComponent* s = swiss_getComponent(em, ctype, it.id);

        snprintf(buffer, 128, "    verts: %d", vector_size(&s->face->vertex_buffer));

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->currentHeight += size.y;
    }

    float maxWidth = 200;
    float maxHeight = 200;

    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_PHYSICAL, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct PhysicalComponent* p = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

        // @CLEANUP: For whatever reason, I've implemented these debug things
        // to draw from the bottom left instead of the top left.
        Vector2 size = {{p->size.x, p->size.y}};
        float wRatio = maxWidth / p->size.x;
        float hRatio = maxHeight / p->size.y;
        // Screens are generally wider, so we are probably more likely to have
        // a wide window than a tall one. Therefore placing the equal height
        // window in the wide branch probably helps the branch predictor. What
        // an speedup!
        if(wRatio <= hRatio) {
            vec2_imul(&size, wRatio);
        } else {
            vec2_imul(&size, hRatio);
        }
        debug->currentHeight += size.y + 20;
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        Vector2 size = {{200, debug->currentHeight}};
        Vector3 pos = {{debug->pen.x, debug->pen.y - size.y, 0}};

        struct face* face = assets_load("window.face");
        draw_colored_rect(face, &pos, &size, &content_bg);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct ShapedComponent* s = swiss_getComponent(em, ctype, it.id);

        snprintf(buffer, 128, "    verts: %d", vector_size(&s->face->vertex_buffer));

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);
        debug->pen.y -= size.y;

        text_draw(&debug_font, buffer, &debug->pen, &scale);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_PHYSICAL, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct PhysicalComponent* p = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);
        struct ShapedComponent* s = swiss_getComponent(em, ctype, it.id);

        debug->pen.y -= 10;

        // @CLEANUP: For whatever reason, I've implemented these debug things
        // to draw from the bottom left instead of the top left.
        Vector2 size = {{p->size.x, p->size.y}};
        float wRatio = maxWidth / p->size.x;
        float hRatio = maxHeight / p->size.y;
        // Screens are generally wider, so we are probably more likely to have
        // a wide window than a tall one. Therefore placing the equal height
        // window in the wide branch probably helps the branch predictor. What
        // an speedup!
        if(wRatio <= hRatio) {
            vec2_imul(&size, wRatio);
        } else {
            vec2_imul(&size, hRatio);
        }
        Vector3 pos = {{debug->pen.x, debug->pen.y - size.y, 0.1}};


        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        draw_colored_rect(s->face, &pos, &size, &(Vector4){{1, 1, 1, 1}});
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        debug->pen.y -= size.y + 10;
    }
}

static void draw_shadow_component(Swiss* em, enum ComponentType ctype) {
    Vector2 scale = {{1, 1}};
    char buffer[128];

    // @CLEANUP @PERFORMANCE: We are doing snprintf twice here for the same
    // strings
    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        snprintf(buffer, 128, "%s", component_names[ctype]);

        Vector2 size = {{0}};
        text_size(&debug_font, buffer, &scale, &size);

        debug->currentHeight = size.y + 10;
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        Vector2 size = {{200, debug->currentHeight}};
        Vector3 pos = {{debug->pen.x, debug->pen.y - size.y, 0}};

        struct face* face = assets_load("window.face");
        draw_colored_rect(face, &pos, &size, &header_bg);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        snprintf(buffer, 128, "%s", component_names[ctype]);

        debug->pen.y -= debug->currentHeight;

        text_draw(&debug_font, buffer, &(Vector2){{debug->pen.x, debug->pen.y + 5}}, &scale);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        debug->currentHeight = 0;
    }

    float maxWidth = 200;
    float maxHeight = 200;

    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_PHYSICAL, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct PhysicalComponent* p = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);

        // @CLEANUP: For whatever reason, I've implemented these debug things
        // to draw from the bottom left instead of the top left.
        Vector2 size = {{p->size.x, p->size.y}};
        float wRatio = maxWidth / p->size.x;
        float hRatio = maxHeight / p->size.y;
        // Screens are generally wider, so we are probably more likely to have
        // a wide window than a tall one. Therefore placing the equal height
        // window in the wide branch probably helps the branch predictor. What
        // an speedup!
        if(wRatio <= hRatio) {
            vec2_imul(&size, wRatio);
        } else {
            vec2_imul(&size, hRatio);
        }
        debug->currentHeight += size.y + 20;
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

        Vector2 size = {{200, debug->currentHeight}};
        Vector3 pos = {{debug->pen.x, debug->pen.y - size.y, 0}};

        struct face* face = assets_load("window.face");
        draw_colored_rect(face, &pos, &size, &content_bg);
    }

    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_PHYSICAL, ctype, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct PhysicalComponent* p = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);
        struct ShapedComponent* shape = swiss_getComponent(em, COMPONENT_SHAPED, it.id);
        struct glx_shadow_cache* s = swiss_getComponent(em, ctype, it.id);

        debug->pen.y -= 10;

        // @CLEANUP: For whatever reason, I've implemented these debug things
        // to draw from the bottom left instead of the top left.
        Vector2 size = {{p->size.x, p->size.y}};
        float wRatio = maxWidth / p->size.x;
        float hRatio = maxHeight / p->size.y;
        // Screens are generally wider, so we are probably more likely to have
        // a wide window than a tall one. Therefore placing the equal height
        // window in the wide branch probably helps the branch predictor. What
        // an speedup!
        if(wRatio <= hRatio) {
            vec2_imul(&size, wRatio);
        } else {
            vec2_imul(&size, hRatio);
        }
        Vector3 pos = {{debug->pen.x, debug->pen.y - size.y, 0}};


        struct shader_program* program = assets_load("passthough.shader");
        if(program->shader_type_info != &passthough_info) {
            printf_errf("Shader was not a passthrough shader");
            return;
        }
        struct Passthough* shader_type = program->shader_type;

        shader_set_future_uniform_bool(shader_type->flip, s->effect.flipped);
        shader_set_future_uniform_sampler(shader_type->tex_scr, 0);
        shader_set_future_uniform_float(shader_type->opacity, 1.0);
        shader_use(program);

        texture_bind(&s->effect, GL_TEXTURE0);

        draw_rect(shape->face, shader_type->mvp, pos, size);
        debug->pen.y -= size.y + 10;
    }
}
typedef void (*debug_component_renderer)(Swiss* em, enum ComponentType ctype);
debug_component_renderer component_renderer[NUM_COMPONENT_TYPES] = {
    0,
    [COMPONENT_MUD] = draw_mud_component,
    [COMPONENT_STATEFUL] = draw_state_component,
    [COMPONENT_DIM] = draw_dim_component,
    [COMPONENT_OPACITY] = draw_opacity_component,
    [COMPONENT_BGOPACITY] = draw_opacity_component,
    [COMPONENT_TRANSITIONING] = draw_transitioning_component,
    [COMPONENT_FOCUS_CHANGE] = draw_focus_change_component,
    [COMPONENT_PHYSICAL] = draw_physical_component,
    [COMPONENT_SHAPED] = draw_shaped_component,
    [COMPONENT_SHADOW] = draw_shadow_component,
};

void draw_component_debug(Swiss* em, Vector2* rootSize) {
    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_PHYSICAL, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);
        Vector2 winPos = X11_rectpos_to_gl(rootSize, &physical->position, &physical->size);
        debug->pen = (Vector2){{winPos.x + 10, winPos.y + physical->size.y - 20}};
    }

    {
        Vector2 scale = {{1, 1}};
        char buffer[128];

        for_components(it, em,
                COMPONENT_DEBUGGED, CQ_END) {
            struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);

            snprintf(buffer, 128, "ID: %ld", it.id);

            Vector2 size = {{0}};
            text_size(&debug_font, buffer, &scale, &size);
            debug->pen.y -= size.y;

            text_draw(&debug_font, buffer, &debug->pen, &scale);
        }
    }

    for(int i = 0; i < NUM_COMPONENT_TYPES; i++) {
        debug_component_renderer renderer = component_renderer[i];
        if(renderer == NULL)
            draw_generic_component(em, i);
        else
            renderer(em, i);
    }
}

void init_debug_graph(struct DebugGraphState* state) {
    state->width = 256;
    for(int i = 0; i < GRAPHS; i++) {
        if(bo_init(&state->bo[i], state->width) != 0) {
            printf_errf("Failed initializing debug graph buffer");
            return;
        }
        if(texture_init_buffer(&state->tex[i], state->width, &state->bo[i], GL_R8)) {
            printf_errf("Failed initializing debug graph texture");
            return;
        }
        state->data[i] = calloc(1, state->width * sizeof(double));
        state->avg[i] = 0;
    }

    state->cursor = 0;
    vector_init(&state->xdata.values, sizeof(uint64_t), 16);
}

void draw_debug_graph(struct DebugGraphState* state, Vector2* pos) {
    const static float winWidth = 200;
    Vector2 bigSize = {{winWidth, 65}};
    Vector2 smallSize = {{winWidth, 20}};

    Vector2 winSize = {{winWidth, 0}};
    winSize.y += bigSize.y;
    winSize.y += smallSize.y * vector_size(&state->xdata.values);


    Vector3 winPos = {{pos->x, pos->y - winSize.y, 1.0}};

    Vector3 fgColors[] = {
        {{0.337255, 0.737255, 0.631373}},
        {{.369, .537, .737}},
    };
    Vector4 bgColor = {{.1, .1, .1, .5}};
    struct face* face = assets_load("window.face");

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);

    Vector3 winBorderPos = {{winPos.x - 5, winPos.y - 5, winPos.z}};
    Vector2 winExtents = {{winSize.x + 10, winSize.y + 10}};
    draw_colored_rect(face, &winBorderPos, &winExtents, &bgColor);

    Vector2 pen = {{winPos.x, winPos.y + winSize.y}};
    Vector2 scale = {{1, 1}};
    Vector2 size = {{0}};
    {
        float savedY = pen.y;
        {
            static char* buffer = "Frametime";

            text_size(&debug_font, buffer, &scale, &size);
            Vector2 pos = {{pen.x, pen.y - size.y}};

            text_draw_colored(&debug_font, buffer, &pos, &scale, &(Vector3){{1.0, 1.0, 1.0}});
        }
        {
            static char buffer[128];
            snprintf(buffer, 128, "%.1f ms", state->avg[0]);

            text_size(&debug_font, buffer, &scale, &size);
            Vector2 pos = {{winPos.x + winSize.x - size.x, pen.y - size.y}};

            text_draw_colored(&debug_font, buffer, &pos, &scale, &fgColors[1]);
        }
        pen.y -= size.y;
        {
            static char* buffer = "Rectangles";

            text_size(&debug_font, buffer, &scale, &size);
            Vector2 pos = {{pen.x, pen.y - size.y}};

            text_draw_colored(&debug_font, buffer, &pos, &scale, &(Vector3){{1.0, 1.0, 1.0}});
        }
        {
            static char buffer[128];
            snprintf(buffer, 128, "%.0f", state->avg[1]);

            text_size(&debug_font, buffer, &scale, &size);
            Vector2 pos = {{winPos.x + winSize.x - size.x, pen.y - size.y}};

            text_draw_colored(&debug_font, buffer, &pos, &scale, &fgColors[0]);
        }
        pen.y = savedY - bigSize.y;
    }

    for(size_t i = 0; i < vector_size(&state->xdata.values); i++) {
        char* buffer = state->xdata.names[i];

        text_size(&debug_font, buffer, &scale, &size);
        Vector2 pos = {{pen.x, (pen.y - (i + 0.5) * smallSize.y) - (0.5 * size.y)}};

        text_draw_colored(&debug_font, buffer, &pos, &scale, &(Vector3){{1.0, 1.0, 1.0}});
    }

    for(size_t i = 0; i < vector_size(&state->xdata.values); i++) {
        static char buffer[128];
        snprintf(buffer, 128, "%ld", *((uint64_t*)vector_get(&state->xdata.values, i)));

        text_size(&debug_font, buffer, &scale, &size);
        Vector2 pos = {{pen.x + winSize.x - size.x, (pen.y - (i + 0.5) * smallSize.y) - (0.5 * size.y)}};

        text_draw_colored(&debug_font, buffer, &pos, &scale, &(Vector3){{1.0, 1.0, 1.0}});
    }

    struct shader_program* program = assets_load("graph.shader");
    if(program->shader_type_info != &graph_info) {
        printf_errf("Shader was not a graph shader\n");
        return;
    }

    struct Graph* type = program->shader_type;
    shader_set_future_uniform_sampler(type->sampler, 0);
    shader_set_future_uniform_vec3(type->color, &VEC3_ZERO);
    shader_set_future_uniform_int(type->width, state->width);
    shader_set_future_uniform_int(type->cursor, state->cursor);

    shader_use(program);

    Vector3 graphPos = {{0, winSize.y - bigSize.y, 0}};
    vec3_add(&graphPos, &winPos);

    shader_set_uniform_vec3(type->color, &fgColors[0]);
    texture_bind(&state->tex[0], GL_TEXTURE0);
    draw_rect(face, type->mvp, graphPos, bigSize);

    shader_set_uniform_vec3(type->color, &fgColors[1]);
    texture_bind(&state->tex[1], GL_TEXTURE0);
    draw_rect(face, type->mvp, graphPos, bigSize);

    glDisable(GL_BLEND);
}

static int draws = 0;
void update_debug_graph(struct DebugGraphState* state, timestamp startTime, struct X11Context* xctx) {
    timestamp currentTime;
    if(!getTime(&currentTime)) {
        printf_errf("Failed getting time");
        exit(1);
    }
    double dt = timeDiff(&startTime, &currentTime);

    {
        uint8_t data = (uint8_t)((dt / 8.0) * 255.0);
        bo_update(&state->bo[0], state->cursor, 1, &data);
        state->avg[0] += (dt / state->width) - (state->data[0][state->cursor] / state->width);
        state->data[0][state->cursor] = dt;
    }

    {
        uint8_t data = (uint8_t)((draws / 200.0) * 255.0);
        bo_update(&state->bo[1], state->cursor, 1, &data);
        state->avg[1] += (draws / (double)state->width) - (state->data[1][state->cursor] / state->width);
        state->data[1][state->cursor] = draws;
        draws = 0;
    }

    xorg_resource(xctx, &state->xdata);

    state->cursor++;
    if(state->cursor >= state->width)
        state->cursor = 0;
}

void debug_mark_draw() {
    draws++;
}
