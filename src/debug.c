#include "debug.h"

#include "text.h"
#include "window.h"

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
    "[Dim]",
    "[Fades Dim]",
    "[Redirected]",
    "[Shaped]",
    "[Stateful]",
    "[Debugged]",

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

typedef void (*debug_component_renderer)(Swiss* em, enum ComponentType ctype);
debug_component_renderer component_renderer[NUM_COMPONENT_TYPES] = {
    0,
    [COMPONENT_STATEFUL] = draw_state_component,
};

void draw_component_debug(Swiss* em, Vector2* rootSize) {
    for_components(it, em,
            COMPONENT_DEBUGGED, COMPONENT_PHYSICAL, CQ_END) {
        struct DebuggedComponent* debug = swiss_getComponent(em, COMPONENT_DEBUGGED, it.id);
        struct PhysicalComponent* physical = swiss_getComponent(em, COMPONENT_PHYSICAL, it.id);
        Vector2 winPos = X11_rectpos_to_gl(rootSize, &physical->position, &physical->size);
        debug->pen = (Vector2){{winPos.x, winPos.y + physical->size.y - 20}};
    }

    for(int i = 0; i < NUM_COMPONENT_TYPES; i++) {
        debug_component_renderer renderer = component_renderer[i];
        if(renderer == NULL)
            draw_generic_component(em, i);
        else
            renderer(em, i);
    }
}
