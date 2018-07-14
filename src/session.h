#pragma once

#include "window.h"
#include "atoms.h"
#include "bezier.h"
#include "xorg.h"
#include "xtexture.h"
#include "blur.h"
#include "framebuffer.h"
#include "renderbuffer.h"
#include "swiss.h"
#include "vector.h"
#include "dbus.h"
#include "winprop.h"

#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xdbe.h>

typedef long time_ms_t;
struct _c2l_ptr;
typedef struct _c2_lptr c2_lptr_t;

typedef void (*f_DebugMessageCallback) (GLDEBUGPROC, void *userParam);

typedef int (*f_WaitVideoSync) (int, int, unsigned *);
typedef int (*f_GetVideoSync) (unsigned *);

typedef Bool (*f_GetSyncValuesOML) (Display* dpy, GLXDrawable drawable, int64_t* ust, int64_t* msc, int64_t* sbc);
typedef Bool (*f_WaitForMscOML) (Display* dpy, GLXDrawable drawable, int64_t target_msc, int64_t divisor, int64_t remainder, int64_t* ust, int64_t* msc, int64_t* sbc);

typedef int (*f_SwapIntervalSGI) (int interval);
typedef int (*f_SwapIntervalMESA) (unsigned int interval);

typedef void (*f_BindTexImageEXT) (Display *display, GLXDrawable drawable, int buffer, const int *attrib_list);
typedef void (*f_ReleaseTexImageEXT) (Display *display, GLXDrawable drawable, int buffer);

typedef void (*f_CopySubBuffer) (Display *dpy, GLXDrawable drawable, int x, int y, int width, int height);

winprop_t
wid_get_prop_adv(const session_t *ps, Window w, Atom atom, long offset,
    long length, Atom rtype, int rformat);


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

/// VSync modes.
typedef enum {
  VSYNC_NONE,
  VSYNC_DRM,
  VSYNC_OPENGL,
  VSYNC_OPENGL_OML,
  VSYNC_OPENGL_SWC,
  VSYNC_OPENGL_MSWC,
  NUM_VSYNC,
} vsync_t;

/// @brief Wrapper of a GLX FBConfig.
typedef struct {
  GLXFBConfig cfg;
  GLint texture_fmt;
  GLint texture_tgts;
  bool y_inverted;
} glx_fbconfig_t;

/// Structure representing all options.
typedef struct _options_t {
  // === General ===
  /// The configuration file we used.
  char *config_file;
  /// Path to write PID to.
  char *write_pid_path;
  /// The display name we used. NULL means we are using the value of the
  /// <code>DISPLAY</code> environment variable.
  char *display;
  /// Safe representation of display name.
  char *display_repr;
  /// Whether to sync X drawing to avoid certain delay issues with
  /// GLX backend.
  bool xrender_sync;
  /// Whether to sync X drawing with X Sync fence.
  bool xrender_sync_fence;
  /// Whether to avoid using stencil buffer under GLX backend. Might be
  /// unsafe.
  bool glx_no_stencil;
  /// Whether to copy unmodified regions from front buffer.
  bool glx_copy_from_front;
  /// Whether to use glXCopySubBufferMESA() to update screen.
  bool glx_use_copysubbuffermesa;
  /// Whether to avoid rebinding pixmap on window damage.
  bool glx_no_rebind_pixmap;
  /// GLX swap method we assume OpenGL uses.
  int glx_swap_method;
  /// Whether to use GL_EXT_gpu_shader4 to (hopefully) accelerates blurring.
  bool glx_use_gpushader4;
  /// Custom fragment shader for painting windows, as a string.
  char *glx_fshader_win_str;
  /// Whether to fork to background.
  bool fork_after_register;
  /// Whether to paint on X Composite overlay window instead of root
  /// window.
  bool paint_on_overlay;
  /// Force painting of window content with blending.
  bool force_win_blend;
  /// Blur Level
  int blur_level;
  /// Whether to unredirect all windows if a full-screen opaque window
  /// is detected.
  bool unredir_if_possible;
  /// List of conditions of windows to ignore as a full-screen window
  /// when determining if a window could be unredirected.
  c2_lptr_t *unredir_if_possible_blacklist;
  /// Delay before unredirecting screen.
  time_ms_t unredir_if_possible_delay;
  /// Forced redirection setting through D-Bus.
  switch_t redirected_force;
  /// Whether to stop painting. Controlled through D-Bus.
  switch_t stoppaint_force;
  /// Whether to re-redirect screen on root size change.
  bool reredir_on_root_change;
  /// Whether to reinitialize GLX on root size change.
  bool glx_reinit_on_root_change;
  /// Whether to enable D-Bus support.
  bool dbus;
  /// Path to log file.
  char *logpath;
  /// Number of cycles to paint in benchmark mode. 0 for disabled.
  int benchmark;
  /// Window to constantly repaint in benchmark mode. 0 for full-screen.
  Window benchmark_wid;
  /// A list of conditions of windows not to paint.
  c2_lptr_t *paint_blacklist;
  /// Whether to avoid using XCompositeNameWindowPixmap(), for debugging.
  bool no_name_pixmap;
  /// Whether to work under synchronized mode for debugging.
  bool synchronize;
  /// Whether to show all X errors.
  bool show_all_xerrors;
  /// Whether to avoid acquiring X Selection.
  bool no_x_selection;

  // === VSync & software optimization ===
  /// VSync method to use;
  vsync_t vsync;
  /// Whether to enable double buffer.
  bool dbe;
  /// Whether to do VSync aggressively.
  bool vsync_aggressive;
  /// Whether to use glFinish() instead of glFlush() for (possibly) better
  /// VSync yet probably higher CPU usage.
  bool vsync_use_glfinish;

  // === Shadow ===
  /// Enable/disable shadow for specific window types.
  bool wintype_shadow[NUM_WINTYPES];
  /// Red, green and blue tone of the shadow.
  double shadow_red, shadow_green, shadow_blue;
  int shadow_offset_x, shadow_offset_y;
  double shadow_opacity;
  bool clear_shadow;
  /// Geometry of a region in which shadow is not painted on.
  geometry_t shadow_exclude_reg_geom;
  /// Shadow blacklist. A linked list of conditions.
  c2_lptr_t *shadow_blacklist;
  /// Whether bounding-shaped window should be ignored.
  bool shadow_ignore_shaped;
  /// Whether to respect _COMPTON_SHADOW.
  bool respect_prop_shadow;
  /// Whether to crop shadow to the very Xinerama screen.
  bool xinerama_shadow_crop;

  // === Fading ===
  /// Enable/disable fading for specific window types.
  bool wintype_fade[NUM_WINTYPES];
  /// Fading time delta. In milliseconds.
  time_ms_t fade_delta;
  /// Whether to disable fading on window open/close.
  bool no_fading_openclose;
  /// Whether to disable fading on ARGB managed destroyed windows.
  bool no_fading_destroyed_argb;
  /// Fading blacklist. A linked list of conditions.
  c2_lptr_t *fade_blacklist;

  // === Opacity ===
  /// Default opacity for specific window types
  double wintype_opacity[NUM_WINTYPES];
  /// Default opacity for inactive windows.
  /// 32-bit integer with the format of _NET_WM_OPACITY. 0 stands for
  /// not enabled, default.
  double inactive_opacity;
  /// Default opacity for inactive windows.
  double active_opacity;

  double opacity_fade_time;
  /// Whether inactive_opacity overrides the opacity set by window
  /// attributes.
  bool inactive_opacity_override;
  /// Whether to detect _NET_WM_OPACITY on client windows. Used on window
  /// managers that don't pass _NET_WM_OPACITY to frame windows.
  bool detect_client_opacity;
  /// Step for pregenerating alpha pictures. 0.01 - 1.0.
  double alpha_step;

  // === Other window processing ===
  /// Whether to blur background of semi-transparent / ARGB windows.
  bool blur_background;
  /// Whether to blur background when the window frame is not opaque.
  /// Implies blur_background.
  bool blur_background_frame;
  /// Whether to use fixed blur strength instead of adjusting according
  /// to window opacity.
  bool blur_background_fixed;
  /// Background blur blacklist. A linked list of conditions.
  c2_lptr_t *blur_background_blacklist;
  /// How much to dim an inactive window. 0.0 - 1.0, 0 to disable.
  double inactive_dim;
  /// Whether to use fixed inactive dim opacity, instead of deciding
  /// based on window opacity.
  bool inactive_dim_fixed;
  /// Conditions of windows to have inverted colors.
  c2_lptr_t *invert_color_list;
  /// Rules to change window opacity.
  c2_lptr_t *opacity_rules;

  // === Focus related ===
  /// Consider windows of specific types to be always focused.
  bool wintype_focus[NUM_WINTYPES];
  /// Whether to try to detect WM windows and mark them as focused.
  bool mark_wmwin_focused;
  /// Whether to mark override-redirect windows as focused.
  bool mark_ovredir_focused;
  /// Whether to use EWMH _NET_ACTIVE_WINDOW to find active window.
  bool use_ewmh_active_win;
  /// A list of windows always to be considered focused.
  c2_lptr_t *focus_blacklist;
  /// Whether to do window grouping with <code>WM_TRANSIENT_FOR</code>.
  bool detect_transient;
  /// Whether to do window grouping with <code>WM_CLIENT_LEADER</code>.
  bool detect_client_leader;

  // === Calculated ===
  /// Whether compton needs to track focus changes.
  bool track_focus;
  /// Whether compton needs to track window name and class.
  bool track_wdata;
  /// Whether compton needs to track window leaders.
  bool track_leader;
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
  /// Pointer to glXSwapIntervalSGI function.
  f_SwapIntervalSGI glXSwapIntervalProc;
  /// Pointer to glXSwapIntervalMESA function.
  f_SwapIntervalMESA glXSwapIntervalMESAProc;
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
#ifdef DEBUG_GLX_MARK
  /// Pointer to StringMarkerGREMEDY function.
  f_StringMarkerGREMEDY glStringMarkerGREMEDY;
  /// Pointer to FrameTerminatorGREMEDY function.
  f_FrameTerminatorGREMEDY glFrameTerminatorGREMEDY;
#endif
  struct blur blur;
  /// Current GLX Z value.
  int z;
  // Standard view matrix
  Matrix view;
  /// FBConfig-s for GLX pixmap of different depths.
  glx_fbconfig_t *fbconfigs[OPENGL_MAX_DEPTH + 1];

  struct X11Context xcontext;

  // @MEMORY @PERFORMANCE: We don't need a dedicated FBO for just stencil, but
  // for right now I don't want to bother with that
  struct Framebuffer stencil_fbo;
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
    /// Height of root window.
    int root_height;
    /// Width of root window.
    int root_width;
    // Damage of root window.
    // Damage root_damage;
    /// X Composite overlay window. Used if <code>--paint-on-overlay</code>.
    Window overlay;

    /// The root tile, but better
    struct XTexture root_texture;

    /// A region of the size of the screen.
    XserverRegion screen_reg;
#ifdef CONFIG_XSYNC
    XSyncFence tgt_buffer_fence;
#endif
    /// DBE back buffer for root window. Used in DBE painting mode.
    XdbeBackBuffer root_dbe;
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
    /// Linked list of all timeouts.
    struct _timeout_t *tmout_lst;
    /// Timeout for delayed unredirection.
    struct _timeout_t *tmout_unredir;
    /// Whether we have hit unredirection timeout.
    bool tmout_unredir_hit;
    /// Whether we have received an event in this cycle.
    bool skip_poll;
    /// Whether the program is idling. I.e. no fading, no potential window
    /// changes.
    bool idling;
    /// Program start time.
    struct timeval time_start;
    /// Whether all windows are currently redirected.
    bool redirected;
    /// Whether all reg_ignore of windows should expire in this paint.
    bool reg_ignore_expire;
    /// Time of last fading. In milliseconds.
    time_ms_t fade_time;
    /// Head pointer of the error ignore linked list.
    ignore_t *ignore_head;
    /// Pointer to the <code>next</code> member of tail element of the error
    /// ignore linked list.
    ignore_t **ignore_tail;
    /// Reset program after next paint.
    bool reset;

    // === Expose event related ===
    /// Pointer to an array of <code>XRectangle</code>-s of exposed region.
    XRectangle *expose_rects;
    /// Number of <code>XRectangle</code>-s in <code>expose_rects</code>.
    int size_expose;
    /// Index of the next free slot in <code>expose_rects</code>.
    int n_expose;

    // === Window related ===
    // Swiss of windows
    Swiss win_list;
    // Window order vector. Since most activity involves the topmost window, the
    // vector will be ordered with the topmost window last
    Vector order;
    /// Pointer to <code>win</code> of current active window. Used by
    /// EWMH <code>_NET_ACTIVE_WINDOW</code> focus detection. In theory,
    /// it's more reliable to store the window ID directly here, just in
    /// case the WM does something extraordinary, but caching the pointer
    /// means another layer of complexity.
    struct _win *active_win;
    /// Window ID of leader window of currently active window. Used for
    /// subsidiary window detection.
    Window active_leader;

    // === Shadow/dimming related ===
    // for shadow precomputation
    /// Shadow depth on one side.
    int cgsize;
    /// A region in which shadow is not painted on.
    XserverRegion shadow_exclude_reg;

#ifdef CONFIG_VSYNC_DRM
    // === DRM VSync related ===
    /// File descriptor of DRI device file. Used for DRM VSync.
    int drm_fd;
#endif

    // === X extension related ===
    /// Event base number for X Fixes extension.
    int xfixes_event;
    /// Error base number for X Fixes extension.
    int xfixes_error;
    /// Event base number for X Damage extension.
    int damage_event;
    /// Error base number for X Damage extension.
    int damage_error;
    /// Event base number for X Render extension.
    int render_event;
    /// Error base number for X Render extension.
    int render_error;
    /// Event base number for X Composite extension.
    int composite_event;
    /// Error base number for X Composite extension.
    int composite_error;
    /// Major opcode for X Composite extension.
    int composite_opcode;
    /// Whether X Composite NameWindowPixmap is available. Aka if X
    /// Composite version >= 0.2.
    bool has_name_pixmap;
    /// Whether X Shape extension exists. @CLEANUP: Should be in xorg.h
    bool shape_exists;
    /// Event base number for X Shape extension.
    int shape_event;
    /// Error base number for X Shape extension.
    int shape_error;
    /// Whether X RandR extension exists.
    bool randr_exists;
    /// Event base number for X RandR extension.
    int randr_event;
    /// Error base number for X RandR extension.
    int randr_error;
    /// Whether X GLX extension exists.
    bool glx_exists;
    /// Event base number for X GLX extension.
    int glx_event;
    /// Error base number for X GLX extension.
    int glx_error;
    /// Whether X DBE extension exists.
    bool dbe_exists;
#ifdef CONFIG_XINERAMA
    /// Whether X Xinerama extension exists.
    bool xinerama_exists;
    /// Xinerama screen info.
    XineramaScreenInfo *xinerama_scrs;
    /// Xinerama screen regions.
    XserverRegion *xinerama_scr_regs;
    /// Number of Xinerama screens.
    int xinerama_nscrs;
#endif
#ifdef CONFIG_XSYNC
    /// Whether X Sync extension exists.
    bool xsync_exists;
    /// Event base number for X Sync extension.
    int xsync_event;
    /// Error base number for X Sync extension.
    int xsync_error;
#endif
    /// Whether X Render convolution filter exists.
    bool xrfilter_convolution_exists;

    // === Atoms ===
    struct Atoms atoms;
    /// Linked list of additional atoms to track.
    latom_t *track_atom_lst;

#ifdef CONFIG_DBUS
    // === DBus related ===
    // DBus connection.
    DBusConnection *dbus_conn;
    // DBus service name.
    char *dbus_service;
#endif
} session_t;
