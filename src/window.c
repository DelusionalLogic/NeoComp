#define _GNU_SOURCE
#include "window.h"

#include "X11/Xlib-xcb.h"
#include "xcb/composite.h"

#include "vmath.h"
#include "logging.h"
#include "bezier.h"
#include "windowlist.h"
#include "blur.h"
#include "assets/assets.h"
#include "profiler/zone.h"
#include "assets/shader.h"
#include "shaders/shaderinfo.h"
#include "xtexture.h"
#include "textureeffects.h"
#include "renderutil.h"
#include "shadow.h"

DECLARE_ZONE(name_pixmap);
DECLARE_ZONE(bind_pixmap);

int window_zcmp(const void* a, const void* b, void* userdata) {
    const win_id *a_wid = a;
    const win_id *b_wid = b;
    const Swiss* em = userdata;
    struct ZComponent* a_z = swiss_getComponent(em, COMPONENT_Z, *a_wid);
    struct ZComponent* b_z = swiss_getComponent(em, COMPONENT_Z, *b_wid);

    if(a_z->z > b_z->z)
        return 1;

    if(a_z->z < b_z->z)
        return -1;

    // Must be equal
    return 0;
}

bool win_overlap(Swiss* em, win_id w1, win_id w2) {
    struct PhysicalComponent* w1p = swiss_getComponent(em, COMPONENT_PHYSICAL, w1);
    struct PhysicalComponent* w2p = swiss_getComponent(em, COMPONENT_PHYSICAL, w2);

    const Vector2 w1lpos = w1p->position;
    Vector2 w1rpos = w1p->position;
    vec2_add(&w1rpos, &w1p->size);

    const Vector2 w2lpos = w2p->position;
    Vector2 w2rpos = w2p->position;
    vec2_add(&w2rpos, &w2p->size);

    // Horizontal collision
    if (w1lpos.x > w2rpos.x || w2lpos.x > w1rpos.x)
        return false;

    // Vertical collision
    if (w1lpos.y > w2rpos.y || w2lpos.y > w1rpos.y)
        return false;

    return true;
}

bool win_mapped(Swiss* em, win_id wid) {
    struct StatefulComponent* stateful = swiss_getComponent(em, COMPONENT_STATEFUL, wid);
    return stateful->state == STATE_ACTIVATING || stateful->state == STATE_ACTIVE
        || stateful->state == STATE_DEACTIVATING || stateful->state == STATE_INACTIVE
        || stateful->state == STATE_WAITING;
}

void fade_init(struct Fading* fade, double value) {
    fade->head = 0;
    fade->tail = 0;
    fade->keyframes[0].target = value;
    fade->keyframes[0].time = 0;
    fade->keyframes[0].duration = -1;

    fade->value = value;
}

void fade_keyframe(struct Fading* fade, double opacity, double duration) {
    // Fast path for skipping fading
    if(duration == 0) {
        fade->head = 0;
        fade->tail = 0;
        fade->keyframes[0].target = opacity;
        fade->keyframes[0].time = 0;
        fade->keyframes[0].duration = -1;
        return;
    }

    size_t nextIndex = (fade->tail + 1) % FADE_KEYFRAMES;
    if(nextIndex == fade->head) {
        printf_dbgf("Warning: Shoving something off the opacity animation");
        fade->head = (fade->head + 1) % FADE_KEYFRAMES;
        //The head has nothing to be blended into.
        fade->keyframes[fade->head].duration = -1;
        fade->keyframes[fade->head].time = 0;
    }

    struct FadeKeyframe* keyframe = &fade->keyframes[nextIndex];
    keyframe->target = opacity;
    keyframe->duration = duration;
    keyframe->time = 0;
    keyframe->ignore = true;
    fade->tail = nextIndex;
}

bool fade_done(struct Fading* fade) {
    return fade->tail == fade->head;
}

#if 0
static void win_draw_debug(session_t* ps, win* w) {
    win_id wid = swiss_indexOfPointer(&ps->win_list, COMPONENT_MUD, w);
    struct PhysicalComponent* physical = swiss_getComponent(&ps->win_list, COMPONENT_PHYSICAL, wid);
    struct face* face = assets_load("window.face");
    Vector2 scale = {{1, 1}};

    glDisable(GL_DEPTH_TEST);
    Vector2 winPos;
    Vector2 pen;
    {
        Vector2 xPen = physical->position;
        Vector2 size = physical->size;
        winPos = X11_rectpos_to_gl(ps, &xPen, &size);
        pen = winPos;
    }

    {
        // @HACK @CLEANUP: we need a better way to debugdraw components that
        // might not be there. Maybe query if they are there and add some
        // custom panel or something?
        struct FadesOpacityComponent* fo = swiss_getComponent(&ps->win_list, COMPONENT_FADES_OPACITY, wid);
        Vector2 barSize = {{200, 25}};
        Vector4 fgColor = {{0.0, 0.4, 0.5, 1.0}};
        Vector4 bgColor = {{.1, .1, .1, .4}};
        for(size_t i = fo->fade.head; i != fo->fade.tail; ) {
            // Increment before the body to skip head and process tail
            i = (i+1) % FADE_KEYFRAMES;

            struct FadeKeyframe* keyframe = &fo->fade.keyframes[i];

            double x = keyframe->time / keyframe->duration;

            Vector3 pos3 = vec3_from_vec2(&pen, 1.0);
            draw_colored_rect(face, &pos3, &barSize, &bgColor);
            Vector3 filledPos3 = pos3;
            filledPos3.x += 5;
            filledPos3.y += 5;
            Vector2 filledSize = barSize;
            filledSize.x -= 10;
            filledSize.y -= 10;
            filledSize.x *= x;
            draw_colored_rect(face, &filledPos3, &filledSize, &fgColor);

            pen.y += 10;
            pen.x += 10;

            char* text;
            asprintf(&text, "slot %zu Target: %f", i, keyframe->target);
            text_draw(&debug_font, text, &pen, &scale);
            free(text);

            pen.x -= 10;
            pen.y += barSize.y - 10;
        }

    }

    {
        char* text;
        asprintf(&text, "Opacity : %f", w->opacity);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y += size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        pen.x = winPos.x;
        pen.y = winPos.y + physical->size.y - 20;
    }

    {
        char* text;
        asprintf(&text, "WID: %#010lx", wid);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        char* text;
        asprintf(&text, "State: %s", StateNames[w->state]);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        char* text;
        asprintf(&text, "blur-background: %d", w->blur_background);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        struct FadesOpacityComponent* fo = swiss_getComponent(&ps->win_list, COMPONENT_FADES_OPACITY, wid);
        char* text;
        asprintf(&text, "fade-status: %d", fade_done(&fo->fade));

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    if(swiss_hasComponent(&ps->win_list, COMPONENT_BINDS_TEXTURE, wid)) {
        struct BindsTextureComponent* bindsTexture = swiss_getComponent(&ps->win_list, COMPONENT_BINDS_TEXTURE, wid);
        char* text;
        asprintf(&text, "depth: %d", bindsTexture->drawable.xtexture.depth);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    if(swiss_hasComponent(&ps->win_list, COMPONENT_SHADOW, wid)) {
        struct ZComponent* z = swiss_getComponent(&ps->win_list, COMPONENT_Z, wid);
        char* text;
        asprintf(&text, "z: %f", z->z);

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    if(swiss_hasComponent(&ps->win_list, COMPONENT_SHADOW, wid)) {
        char* text;
        asprintf(&text, "Has shadow");

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw(&debug_font, text, &pen, &scale);

        free(text);
    }

    {
        char* text;
        Vector3 color = {{.2, .9, .3}};
        if(swiss_hasComponent(&ps->win_list, COMPONENT_CONTENTS_DAMAGED, wid)) {
            asprintf(&text, "REDRAW");
            color = (Vector3){{.9, .1, .2}};
        } else {
            asprintf(&text, "STABLE");
        }

        Vector2 size = {{0}};
        text_size(&debug_font, text, &scale, &size);
        pen.y -= size.y;

        text_draw_colored(&debug_font, text, &pen, &scale, &color);

        free(text);
    }

    glEnable(GL_DEPTH_TEST);
}
#endif

bool wd_init(struct WindowDrawable* drawable, struct X11Context* context, Window wid) {
    assert(drawable != NULL);

    XWindowAttributes attribs;
    XGetWindowAttributes(context->display, wid, &attribs);

    drawable->wid = wid;
    GLXFBConfig* fbconfig = xorgContext_selectConfig(context, XVisualIDFromVisual(attribs.visual));
    xtexinfo_init(&drawable->texinfo, context, fbconfig);

    return xtexture_init(&drawable->xtexture, context);
}

bool wd_bind(struct WindowDrawable* drawables[], size_t cnt) {
    assert(drawables != NULL);

    xcb_connection_t* xcb = XGetXCBConnection(drawables[0]->context->display);

    // These pixmaps are handed over to the xtextures, so they are never freed here.
    xcb_pixmap_t *pixmaps = malloc(sizeof(xcb_pixmap_t) * cnt);
    zone_enter(&ZONE_name_pixmap);
    for(size_t i = 0; i < cnt; i++) {
        pixmaps[i] = xcb_generate_id(xcb);
    }

    for(size_t i = 0; i < cnt; i++) {
        xcb_void_cookie_t cookie = xcb_composite_name_window_pixmap_checked(xcb, drawables[i]->wid, pixmaps[i]);
        if (xcb_request_check(xcb, cookie)) {
            printf_dbgf("Can't name window pixmap. We will try to unbind the texture and just not render the window. It should be fixed if the window remaps");
            pixmaps[i] = 0;
            continue;
        }
    }
    zone_leave(&ZONE_name_pixmap);

    struct XTexture** texs = malloc(sizeof(struct XTexture*) * cnt);
    for(size_t i = 0; i < cnt; i++) {
        texs[i] = &drawables[i]->xtexture;
    }
    struct XTextureInformation** texinfos = malloc(sizeof(struct XTextureInformation*) * cnt);
    for(size_t i = 0; i < cnt; i++) {
        texinfos[i] = &drawables[i]->texinfo;
    }

    bool success = true;
    zone_enter(&ZONE_bind_pixmap);
    success = xtexture_bind(texs, texinfos, pixmaps, cnt);
    zone_leave(&ZONE_bind_pixmap);

    free(pixmaps);
    free(texs);
    free(texinfos);
    return success;
}

bool wd_unbind(struct WindowDrawable* drawable) {
    assert(drawable != NULL);

    xtexture_unbind(&drawable->xtexture);
    return true;
}

void wd_delete(struct WindowDrawable* drawable) {
    assert(drawable != NULL);
    // In debug mode we want to crash if we do this.
    assert(!drawable->bound);
    if(drawable->bound) {
        wd_unbind(drawable);
    }
    texture_delete(&drawable->texture);
}
