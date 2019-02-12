#include "debug.h"

#include "text.h"
#include "window.h"
#include "shaders/shaderinfo.h"
#include "assets/assets.h"
#include "assets/shader.h"

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

    "[Map]",
    "[Unmap]",
    "[Destroy]",
    "[Move]",
    "[Resize]",
    "[Blur Damaged]",
    "[Content Damaged]",
    "[Shadow Damaged]",
    "[Shape Damaged]",
    "[Focus Changed]",
    "[Wintype Changed]",
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

        snprintf(buffer, 128, "    Type: %d", mud->window_type);

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
};

void draw_component_debug(Swiss* em, Vector2* rootSize) {
    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_PHYSICAL, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);
        Vector2 winPos = X11_rectpos_to_gl(rootSize, &physical->position, &physical->size);
        debug->pen = (Vector2){{winPos.x, winPos.y + physical->size.y - 20}};
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
    state->width = 512;
    if(bo_init(&state->bo, state->width) != 0) {
        printf_errf("Failed initializing debug graph buffer");
        return;
    }
    if(texture_init_buffer(&state->tex, state->width, &state->bo, GL_R8)) {
        printf_errf("Failed initializing debug graph texture");
        return;
    }

    state->cursor = 0;
}

void draw_debug_graph(struct DebugGraphState* state) {
    char data[] = {
        (100 + (rand() % 10))
    };
    bo_update(&state->bo, state->cursor, 1, data);
    state->cursor++;
    if(state->cursor >= state->width)
        state->cursor = 0;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    struct shader_program* program = assets_load("graph.shader");
    if(program->shader_type_info != &graph_info) {
        printf_errf("Shader was not a graph shader\n");
        return;
    }

    struct Graph* type = program->shader_type;
    shader_set_future_uniform_sampler(type->sampler, 0);
    shader_set_future_uniform_vec3(type->color, &(Vector3){{0.337255, 0.737255, 0.631373}});
    shader_set_future_uniform_float(type->width, state->width);

    shader_use(program);

    texture_bind(&state->tex, GL_TEXTURE0);

    struct face* face = assets_load("window.face");
    draw_rect(face, type->mvp, (Vector3){{100, 100, 1.0}}, (Vector2){{state->width * 4, 512}});
    glDisable(GL_BLEND);
}
