#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include <X11/extensions/Xrender.h>
#include <X11/extensions/sync.h>
#include <X11/extensions/Xdamage.h>

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
  /// Pointer to the next structure in the linked list.
  size_t next;
  /// Pointer to the next higher window to paint.
  struct _win *prev_trans;
  /// Pointer to the next lower window to paint.
  struct _win *next_trans;

  // Core members
  /// ID of the top-level frame window.
  Window id;
  /// Window attributes.
  XWindowAttributes a;
  float z;

  struct face* face;

  enum WindowState state;

  /// Xinerama screen this window is on.
  int xinerama_scr;

  /// Window visual pict format;
  XRenderPictFormat *pictfmt;
  /// Whether the window has been damaged at least once.
  bool damaged;
  /// X Sync fence of drawable.
  XSyncFence fence;
  /// Damage of the window.
  Damage damage;
  /// Bounding shape of the window.
  XserverRegion border_size;
  /// Window flags. Definitions above.
  int_fast16_t flags;
  /// Whether there's a pending <code>ConfigureNotify</code> happening
  /// when the window is unmapped.
  bool need_configure;
  /// Queued <code>ConfigureNotify</code> when the window is unmapped.
  XConfigureEvent queue_configure;
  /// Cached width/height of the window including border.
  int widthb, heightb;
  /// Whether the window has been destroyed.
  bool destroyed;
  /// Whether the window is bounding-shaped.
  bool bounding_shaped;
  /// Whether this window is to be painted.
  bool to_paint;
  /// Whether the window is painting excluded.
  bool paint_excluded;
  /// Whether the window is unredirect-if-possible excluded.
  bool unredir_if_possible_excluded;
  /// Whether this window is in open/close state.
  bool in_openclose;
  /// Is fullscreen
  bool fullscreen;
  /// Is solid;
  bool solid;

  /// Has frame
  bool has_frame;

  // Client window related members
  /// ID of the top-level client window of the window.
  Window client_win;
  /// Type of the window.
  wintype_t window_type;
  /// Whether it looks like a WM window. We consider a window WM window if
  /// it does not have a decedent with WM_STATE and it is not override-
  /// redirected itself.
  bool wmwin;
  /// Leader window ID of the window.
  Window leader;
  /// Cached topmost window ID of the window.
  Window cache_leader;

  // Focus-related members
  /// Whether the window is to be considered focused.
  bool focused;
  bool focus_changed;
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
  const c2_lptr_t *cache_sblst;
  const c2_lptr_t *cache_fblst;
  const c2_lptr_t *cache_fcblst;
  const c2_lptr_t *cache_ivclst;
  const c2_lptr_t *cache_bbblst;
  const c2_lptr_t *cache_oparule;
  const c2_lptr_t *cache_pblst;
  const c2_lptr_t *cache_uipblst;

  // Opacity-related members

  // Current window opacity.
  struct Fading opacity_fade;
  double opacity;

  bool skipFade;
  double fadeTime;
  double fadeDuration;

  // Fading-related members
  /// Do not fade if it's false. Change on window type change.
  /// Used by fading blacklist in the future.
  bool fade;
  /// Fade state on last paint.
  bool fade_last;
  /// Override value of window fade state. Set by D-Bus method calls.
  switch_t fade_force;
  /// Callback to be called after fading completed.
  void (*fade_callback) (struct _session_t *ps, struct _win *w);

  /// Frame extents. Acquired from _NET_FRAME_EXTENTS.
  margin_t frame_extents;

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

  /// Textures and FBO background blur use.
  glx_blur_cache_t glx_blur_cache;

  bool shadow_damaged;
  struct glx_shadow_cache shadow_cache;

  struct WindowDrawable drawable;
} win;

bool win_calculate_blur(struct blur* blur, struct _session_t* ps, win* w);

bool win_overlap(const win* w1, const win* w2);
bool win_covers(win* w);
bool win_is_solid(win* w);

void win_start_opacity(win* w, double opacity, double duration);

void win_draw(struct _session_t* ps, win* w, float z);
void win_postdraw(struct _session_t* ps, win* w, float z);
void win_update(struct _session_t* ps, win* w, double dt);

bool wd_init(struct WindowDrawable* drawable, struct X11Context* context, Window wid);
void wd_delete(struct WindowDrawable* drawable);

bool wd_bind(struct WindowDrawable* drawable);
bool wd_unbind(struct WindowDrawable* drawable);
