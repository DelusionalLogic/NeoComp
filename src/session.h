#pragma once

#include "window.h"
#include "atoms.h"
#include "bezier.h"
#include "xorg.h"
#include "xtexture.h"
#include "debug.h"
#include "framebuffer.h"
#include "renderbuffer.h"
#include "swiss.h"
#include "vector.h"
#include "winprop.h"

#include "systems/blur.h"
#include "systems/order.h"

#include <X11/extensions/Xinerama.h>
#include <stdbool.h>

typedef long time_ms_t;

typedef void (*f_DebugMessageCallback) (GLDEBUGPROC, void *userParam);

typedef int (*f_WaitVideoSync) (int, int, unsigned *);
typedef int (*f_GetVideoSync) (unsigned *);

typedef Bool (*f_GetSyncValuesOML) (Display* dpy, GLXDrawable drawable, int64_t* ust, int64_t* msc, int64_t* sbc);
typedef Bool (*f_WaitForMscOML) (Display* dpy, GLXDrawable drawable, int64_t target_msc, int64_t divisor, int64_t remainder, int64_t* ust, int64_t* msc, int64_t* sbc);

typedef int (*f_SwapIntervalEXT) (Display* dpy, GLXDrawable drawable, int interval);

typedef void (*f_BindTexImageEXT) (Display *display, GLXDrawable drawable, int buffer, const int *attrib_list);
typedef void (*f_ReleaseTexImageEXT) (Display *display, GLXDrawable drawable, int buffer);

typedef void (*f_CopySubBuffer) (Display *dpy, GLXDrawable drawable, int x, int y, int width, int height);


#define CGLX_SESSION_INIT { .context = NULL }

/// @brief Maximum OpenGL FBConfig depth.
#define OPENGL_MAX_DEPTH 32

/// Linked list type of atoms.
typedef struct _latom {
  Atom atom;
  struct _latom *next;
} latom_t;

typedef struct _ignore {
  struct _ignore *next;
  unsigned long sequence;
} ignore_t;

/// Structure representing a X geometry.
typedef struct {
  int wid;
  int hei;
  int x;
  int y;
} geometry_t;

/// @brief Wrapper of a GLX FBConfig.
typedef struct {
  GLXFBConfig cfg;
  GLint texture_fmt;
  GLint texture_tgts;
  bool y_inverted;
} glx_fbconfig_t;

/// Temporary structure used for communication between
/// <code>get_cfg()</code> and <code>parse_config()</code>.
struct options_tmp {
};

/// Structure representing all options.
typedef struct _options_t {
  // === General ===
  /// The configuration file we used.
  char *config_file;
  /// Path to write PID to.
  char *write_pid_path;
  /// Blur Level
  int blur_level;
  /// Number of cycles to paint in benchmark mode. 0 for disabled.
  int benchmark;

  /// Shadow setting for window types
  bool wintype_shadow[NUM_WINTYPES];

  // === Opacity ===
  /// Default opacity for specific window types
  double wintype_opacity[NUM_WINTYPES];
  /// Default opacity for inactive windows.
  double inactive_opacity;
  /// Default opacity for inactive windows.
  double active_opacity;

  double opacity_fade_time;
  double bg_opacity_fade_time;

  // === Other window processing ===
  /// Whether to blur background of semi-transparent / ARGB windows.
  bool blur_background;
  /// Whether to blur background when the window frame is not opaque.
  /// Implies blur_background.
  bool blur_background_frame;
  /// How much to dim an inactive window. 0.0 - 100.0.
  double inactive_dim;
  double dim_fade_time;
  /// Whether to use fixed inactive dim opacity, instead of deciding
  /// based on window opacity.
  bool inactive_dim_fixed;

  // === Focus related ===
  /// Consider windows of specific types to be always focused.
  bool wintype_focus[NUM_WINTYPES];
} options_t;


/// Structure containing GLX-dependent data for a compton session.
typedef struct {
  // === OpenGL related ===
  /// GLX context.
  GLXContext context;
  /// Whether we have GL_ARB_texture_non_power_of_two.
  bool has_texture_non_power_of_two;
  /// Pointer to glXGetVideoSyncSGI function.
  f_GetVideoSync glXGetVideoSyncSGI;
  /// Pointer to glXWaitVideoSyncSGI function.
  f_WaitVideoSync glXWaitVideoSyncSGI;
   /// Pointer to glXGetSyncValuesOML function.
  f_GetSyncValuesOML glXGetSyncValuesOML;
  /// Pointer to glXWaitForMscOML function.
  f_WaitForMscOML glXWaitForMscOML;

  f_SwapIntervalEXT glXSwapIntervalProc;
  /// Pointer to glXBindTexImageEXT function.
  f_BindTexImageEXT glXBindTexImageProc;
  /// Pointer to glXReleaseTexImageEXT function.
  f_ReleaseTexImageEXT glXReleaseTexImageProc;
  /// Pointer to glXCopySubBufferMESA function.
  f_CopySubBuffer glXCopySubBufferProc;
#ifdef CONFIG_GLX_SYNC
  /// Pointer to the glFenceSync() function.
  f_FenceSync glFenceSyncProc;
  /// Pointer to the glIsSync() function.
  f_IsSync glIsSyncProc;
  /// Pointer to the glDeleteSync() function.
  f_DeleteSync glDeleteSyncProc;
  /// Pointer to the glClientWaitSync() function.
  f_ClientWaitSync glClientWaitSyncProc;
  /// Pointer to the glWaitSync() function.
  f_WaitSync glWaitSyncProc;
  /// Pointer to the glImportSyncEXT() function.
  f_ImportSyncEXT glImportSyncEXT;
#endif
  /// Current GLX Z value.
  int z;
  // Standard view matrix
  Matrix view;
  /// FBConfig-s for GLX pixmap of different depths.
  glx_fbconfig_t *fbconfigs[OPENGL_MAX_DEPTH + 1];

  // @MEMORY @PERFORMANCE: We don't need a dedicated FBO for just stencil, but
  // for right now I don't want to bother with that
} glx_session_t;

/// Structure containing all necessary data for a compton session.
typedef struct _session_t {
    // === Display related ===
    /// Display in use.
    Display *dpy;
    /// Default screen.
    int scr;
    /// Default visual.
    Visual *vis;
    /// Default depth.
    int depth;

    /// Root window.
    Window root;
    Vector2 root_size;
    /// X Composite overlay window.
    Window overlay;

    /// The root tile, but better
    struct XTexture root_texture;

    XSyncFence tgt_buffer_fence;
    /// Window ID of the window we register as a symbol.
    Window reg_win;
    /// Pointer to GLX data.
    glx_session_t *psglx;

    struct Bezier curve;

    // === Operation related ===
    /// Program options.
    options_t o;
    /// File descriptors to check for reading.
    fd_set *pfds_read;
    /// File descriptors to check for writing.
    fd_set *pfds_write;
    /// File descriptors to check for exceptions.
    fd_set *pfds_except;
    /// Largest file descriptor in fd_set-s above.
    int nfds_max;
    /// Whether we have received an event in this cycle.
    bool skip_poll;
    /// Whether the program is idling. I.e. no fading, no potential window
    /// changes.
    bool idling;
    /// Program start time.
    struct timeval time_start;
    /// Head pointer of the error ignore linked list.
    ignore_t *ignore_head;
    /// Pointer to the <code>next</code> member of tail element of the error
    /// ignore linked list.
    ignore_t **ignore_tail;
    /// Reset program after next paint.
    bool reset;

    // === Window related ===
    // Swiss of windows
    Swiss win_list;
    struct Order order;
    /// Pointer to <code>win</code> of current active window. Used by
    /// EWMH <code>_NET_ACTIVE_WINDOW</code> focus detection. In theory,
    /// it's more reliable to store the window ID directly here, just in
    /// case the WM does something extraordinary, but caching the pointer
    /// means another layer of complexity.
    struct _win *active_win;

    XserverRegion root_region;

    // === Atoms ===
    struct Atoms atoms;

    struct X11Context xcontext;
    struct DebugGraphState debug_graph;
} session_t;

void usage(int ret);
void parse_config(session_t *ps, struct options_tmp *pcfgtmp);
