#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include <X11/extensions/Xrender.h>
#include <X11/extensions/sync.h>
#include <X11/extensions/Xdamage.h>

#include "swiss.h"
#include "switch.h"
#include "xtexture.h"
#include "session.h"
#include "renderbuffer.h"
#include "wintypes.h"

#include "systems/blur.h"
#include "systems/shadow.h"

struct _session_t;

struct WindowDrawable {
    Window wid;
    struct XTextureInformation texinfo;

    // This is a bit of magic. In C11 we can have anonymous struct members, but
    // they have to be untagged. We'd prefer to be able to use a tagged one,
    // luckily the "ms-extensions" allow us to do just that. The union here
    // just means that we can also access the struct explicitly. Let me just
    // give an early "I'm sorry".
    union {
        struct XTexture xtexture;
        struct XTexture;
    };
};

struct FocusChangedComponent {
    double newOpacity;
    double newDim;
    bool isFadeIn;
};

struct WintypeChangedComponent {
    wintype_t newType;
};

struct ClassChangedComponent {
    char* instance;
    char* general;
};

struct TracksWindowComponent {
    float border_size;
    Window id;
};

struct TintComponent {
    Vector4 color;
};

struct MoveComponent {
    Vector2 newPosition;
};

struct ResizeComponent {
    Vector2 newSize;
};

struct MapComponent {
};

struct TexturedComponent {
    struct Texture texture;
    struct RenderBuffer stencil;
};

struct BindsTextureComponent {
    // @CLEANUP: I don't think i need this extra complication. I could just
    // manage the xtexture and fbconfig myself
    struct WindowDrawable drawable;
};

struct PhysicalComponent {
    Vector2 position;
    Vector2 size;
};

struct ZComponent {
    double z;
};

// A structure representing margins around a rectangle.
typedef struct {
    int top;
    int left;
    int bottom;
    int right;
} margin_t;

#define FADE_KEYFRAMES 10

struct FadeKeyframe {
    double target;

    double duration; // The time to fade out the previous keyframe, -1 if the fade is done
    double time; // How long we are in the current fade
    double lead; // leadtime before the keyframe starts fading

    // @HACK @CLEANUP: for now we have this bool that signifies if keyframe
    // should ignore the next time update
    bool ignore;
};

struct Fading {
    struct FadeKeyframe keyframes[FADE_KEYFRAMES];
    size_t head; // The current "active" keyframe
    size_t tail; // The last fading keyframe

    // The current animated value
    double value;
};


struct FadesOpacityComponent {
    struct Fading fade;
};

struct FadesBgOpacityComponent {
    struct Fading fade;
};

struct FadesDimComponent {
    struct Fading fade;
};

struct OpacityComponent {
    double opacity;
};

struct TransitioningComponent {
    double time;
    double duration;
};

struct BgOpacityComponent {
    double opacity;
};

struct DimComponent {
    double dim;
};

struct ShapedComponent {
    struct face* face;
};

struct ShapeDamagedEvent {
    Vector rects;
};

extern const char* const StateNames[];

enum WindowState {
    STATE_HIDING,
    STATE_INVISIBLE,
    STATE_WAITING, // Waiting for focus assignment
    STATE_ACTIVATING,
    STATE_ACTIVE,
    STATE_DEACTIVATING,
    STATE_INACTIVE,
    STATE_DESTROYING,
    STATE_DESTROYED,
};

struct StatefulComponent { 
    enum WindowState state;
};

struct DebuggedComponent {
    Vector2 pen;
    float currentHeight;
};

/// Structure representing a top-level window compton manages.
typedef struct _win {
    bool override_redirect;

    // Client window related members
    /// Type of the window.
    wintype_t window_type;
} win;

int window_zcmp(const void* a, const void* b, void* userdata);
bool win_calculate_blur(struct blur* blur, struct _session_t* ps, win* w);

bool win_overlap(Swiss* em, win_id w1, win_id w2);
bool win_mapped(Swiss* em, win_id wid);
bool win_is_solid(win* w);

void fade_keyframe(struct Fading* fade, double opacity, double duration);
void fade_keyframe_lead(struct Fading* fade, double opacity, double duration, double lead);

void fade_init(struct Fading* fade, double value);
// @CLEANUP: Should this be here?
bool fade_done(struct Fading* fade);

void win_draw(struct _session_t* ps, win* w, float z);
void win_postdraw(struct _session_t* ps, win* w);
void win_update(struct _session_t* ps, win* w, double dt);

bool wd_init(struct WindowDrawable* drawable, struct X11Context* context, Window wid);
void wd_delete(struct WindowDrawable* drawable);

bool wd_bind(struct X11Context* xctx, struct WindowDrawable* drawables[], size_t cnt);
bool wd_unbind(struct WindowDrawable* drawable);
