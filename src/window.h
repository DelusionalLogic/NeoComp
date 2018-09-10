#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include <X11/extensions/Xrender.h>
#include <X11/extensions/sync.h>
#include <X11/extensions/Xdamage.h>

#include "swiss.h"
#include "switch.h"
#include "shadow.h"
#include "xtexture.h"
#include "session.h"
#include "renderbuffer.h"
#include "blur.h"
#include "c2.h"
#include "wintypes.h"

struct _session_t;

struct WindowDrawable {
    Window wid;
    GLXFBConfig* fbconfig;

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
};

struct WintypeChangedComponent {
    wintype_t newType;
};

struct TracksWindowComponent {
    Window id;
};

struct HasClientComponent {
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
    Vector2 position;
    Vector2 size;
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

/// A structure representing margins around a rectangle.
typedef struct {
  int top;
  int left;
  int bottom;
  int right;
} margin_t;

#define FADE_KEYFRAMES 10

struct FadeKeyframe {
    double target;

    // The time to fade out the previous keyframe, -1 if the fade is done
    double duration;
    double time; // How long we are in the current fade

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

struct OpacityComponent {
    double opacity;
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

/// Structure representing a top-level window compton manages.
typedef struct _win {
  // Core members
  /// Window attributes.
  XWindowAttributes a;

  struct face* face;

  enum WindowState state;

  /// Xinerama screen this window is on.
  int xinerama_scr;

  /// Damage of the window.
  Damage damage;

  /// Whether there's a pending <code>ConfigureNotify</code> happening
  /// when the window is unmapped.
  bool need_configure;
  /// Queued <code>ConfigureNotify</code> when the window is unmapped.
  XConfigureEvent queue_configure;
  /// Whether this window is to be painted.
  bool to_paint;
  /// Whether the window is painting excluded.
  bool paint_excluded;
  /// Whether the window is unredirect-if-possible excluded.
  bool unredir_if_possible_excluded;

  // @CLEANUP: This should be replaced by a check on state
  /// Whether this window is in open/close state.
  bool in_openclose;

  /// Is fullscreen
  bool fullscreen;
  /// Is solid;
  bool solid;

  // Client window related members
  /// Type of the window.
  wintype_t window_type;
  /// Whether it looks like a WM window. We consider a window WM window if
  /// it does not have a decedent with WM_STATE and it is not override-
  /// redirected itself.
  bool wmwin;

  // Focus-related members
  /// Override value of window focus state. Set by D-Bus method calls.
  switch_t focused_force;

  // Blacklist related members
  /// Name of the window.
  char *name;
  /// Window instance class of the window.
  char *class_instance;
  /// Window general class of the window.
  char *class_general;
  /// <code>WM_WINDOW_ROLE</code> value of the window.
  char *role;

  // Current window opacity.
  double opacity;

  /// Do not fade if it's false. Change on window type change.
  bool fade;
  /// Fade state on last paint.
  bool fade_last;
  /// Override value of window fade state. Set by D-Bus method calls.
  switch_t fade_force;

  // Shadow-related members
  /// Whether a window has shadow. Calculated.
  bool shadow;

  // Dim-related members
  /// Whether the window is to be dimmed.
  bool dim;

  /// Whether to invert window color.
  bool invert_color;
  /// Color inversion state on last paint.
  bool invert_color_last;
  /// Override value of window color inversion state. Set by D-Bus method
  /// calls.
  switch_t invert_color_force;

  /// Whether to blur window background.
  bool blur_background;
  /// Background state on last paint.
  bool blur_background_last;

  bool stencil_damaged;
  struct RenderBuffer stencil;
} win;

int window_zcmp(const void* a, const void* b, void* userdata);
bool win_calculate_blur(struct blur* blur, struct _session_t* ps, win* w);

bool win_overlap(Swiss* em, win_id w1, win_id w2);
bool win_mapped(win* w);
bool win_covers(win* w);
bool win_is_solid(win* w);

void fade_keyframe(struct Fading* fade, double opacity, double duration);

void fade_init(struct Fading* fade, double value);
// @CLEANUP: Should this be here?
bool fade_done(struct Fading* fade);

void win_draw(struct _session_t* ps, win* w, float z);
void win_postdraw(struct _session_t* ps, win* w);
void win_update(struct _session_t* ps, win* w, double dt);

bool wd_init(struct WindowDrawable* drawable, struct X11Context* context, Window wid);
void wd_delete(struct WindowDrawable* drawable);

bool wd_bind(struct WindowDrawable* drawable);
bool wd_unbind(struct WindowDrawable* drawable);
