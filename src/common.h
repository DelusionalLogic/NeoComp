/*
 * Compton - a compositor for X11
 *
 * Based on `xcompmgr` - Copyright (c) 2003, Keith Packard
 *
 * Copyright (c) 2011-2013, Christopher Jeffrey
 * See LICENSE for more information.
 *
 */

#ifndef COMPTON_COMMON_H
#define COMPTON_COMMON_H

// === Options ===

// Debug options, enable them using -D in CFLAGS
// #define DEBUG_BACKTRACE  1
// #define DEBUG_REPAINT    1
// #define DEBUG_EVENTS     1
// #define DEBUG_RESTACK    1
// #define DEBUG_WINTYPE    1
// #define DEBUG_CLIENTWIN  1
// #define DEBUG_WINDATA    1
// #define DEBUG_WINMATCH   1
// #define DEBUG_REDIR      1
// #define DEBUG_ALLOC_REG  1
// #define DEBUG_FRAME      1
// #define DEBUG_LEADER     1
// #define DEBUG_GLX        1
// #define DEBUG_GLX_GLSL   1
// #define DEBUG_GLX_ERR    1
// #define DEBUG_GLX_MARK   1
// #define DEBUG_GLX_PAINTREG 1

// Whether to enable parsing of configuration files using libconfig.
// #define CONFIG_LIBCONFIG 1
// Whether we are using a legacy version of libconfig (1.3.x).
// #define CONFIG_LIBCONFIG_LEGACY 1
// Whether to enable DRM VSync support
// #define CONFIG_VSYNC_DRM 1
// Whether to enable condition support.
// #define CONFIG_C2 1
// Whether to enable GLX Sync support.
// #define CONFIG_GLX_XSYNC 1

#ifndef COMPTON_VERSION
#define COMPTON_VERSION "unknown"
#endif

#if defined(DEBUG_ALLOC_REG)
#define DEBUG_BACKTRACE 1
#endif

// === Includes ===

// For some special functions
#define _GNU_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/poll.h>
#include <assert.h>
#include <time.h>
#include <ctype.h>
#include <sys/time.h>

#include <X11/Xlib-xcb.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xdbe.h>
#include <X11/extensions/sync.h>

#include <X11/extensions/Xinerama.h>

// libconfig
#ifdef CONFIG_LIBCONFIG
#include <libgen.h>
#include <libconfig.h>
#endif

// libGL
#define GL_GLEXT_PROTOTYPES

#include <GL/glx.h>

#include "logging.h"
#include "vmath.h"
#include "wintypes.h"
#include "session.h"
#include "atoms.h"
#include "swiss.h"
#include "vector.h"
#include "bezier.h"
#include "texture.h"
#include "framebuffer.h"
#include "xorg.h"
#include "text.h"

typedef uint64_t win_id;

// Workarounds for missing definitions in some broken GL drivers, thanks to
// douglasp and consolers for reporting
#ifndef GL_TEXTURE_RECTANGLE
#define GL_TEXTURE_RECTANGLE 0x84F5
#endif

#ifndef GLX_BACK_BUFFER_AGE_EXT
#define GLX_BACK_BUFFER_AGE_EXT 0x20F4
#endif

// === Macros ===

#define MSTR_(s)        #s
#define MSTR(s)         MSTR_(s)

/// @brief Wrapper for gcc branch prediction builtin, for likely branch.
#define likely(x)    __builtin_expect(!!(x), 1)

/// @brief Wrapper for gcc branch prediction builtin, for unlikely branch.
#define unlikely(x)  __builtin_expect(!!(x), 0)

// Use #s here to prevent macro expansion
/// Macro used for shortening some debugging code.
#define CASESTRRET(s)   case s: return #s

// X resource checker
#ifdef DEBUG_XRC
#include "xrescheck.h"
#endif

// === Constants ===
#if !(COMPOSITE_MAJOR > 0 || COMPOSITE_MINOR >= 2)
#error libXcomposite version unsupported
#endif

/// @brief Length of generic buffers.
#define BUF_LEN 80

#define OPAQUE 0xffffffff
#define REGISTER_PROP "_NET_WM_CM_S"

#define TIME_MS_MAX LONG_MAX
#define FADE_DELTA_TOLERANCE 0.2
#define TIMEOUT_RUN_TOLERANCE 0.05
#define WIN_GET_LEADER_MAX_RECURSION 20

#define SEC_WRAP (15L * 24L * 60L * 60L)

#define NS_PER_SEC 1000000000L
#define US_PER_SEC 1000000L
#define MS_PER_SEC 1000

#define XRFILTER_CONVOLUTION  "convolution"
#define XRFILTER_GAUSSIAN     "gaussian"
#define XRFILTER_BINOMIAL     "binomial"

/// @brief Maximum OpenGL buffer age.
#define CGLX_MAX_BUFFER_AGE 5

// Window flags

// Window size is changed
#define WFLAG_SIZE_CHANGE   0x0001
// Window size/position is changed
#define WFLAG_POS_CHANGE    0x0002

// === Types ===

typedef uint32_t opacity_t;

// Or use cmemzero().
#define MARGIN_INIT { 0, 0, 0, 0 }

/// Structure representing needed window updates.
typedef struct {
  bool shadow       : 1;
  bool fade         : 1;
  bool focus        : 1;
  bool invert_color : 1;
} win_upd_t;

enum wincond_target {
  CONDTGT_NAME,
  CONDTGT_CLASSI,
  CONDTGT_CLASSG,
  CONDTGT_ROLE,
};

enum wincond_type {
  CONDTP_EXACT,
  CONDTP_ANYWHERE,
  CONDTP_FROMSTART,
  CONDTP_WILDCARD,
  CONDTP_REGEX_PCRE,
};

#define CONDF_IGNORECASE 0x0001

/// @brief Possible swap methods.
enum {
  SWAPM_BUFFER_AGE = -1,
  SWAPM_UNDEFINED = 0,
  SWAPM_COPY = 1,
  SWAPM_EXCHANGE = 2,
};

typedef struct _glx_texture glx_texture_t;

typedef GLXContext (*f_glXCreateContextAttribsARB) (Display *dpy,
    GLXFBConfig config, GLXContext share_context, Bool direct,
    const int *attrib_list);
/* typedef void (*GLDEBUGPROC) (GLenum source, GLenum type, */
/*     GLuint id, GLenum severity, GLsizei length, const GLchar* message, */
/*     GLvoid* userParam); */
#ifdef CONFIG_GLX_SYNC
// Looks like duplicate typedef of the same type is safe?
typedef int64_t GLint64;
typedef uint64_t GLuint64;
typedef struct __GLsync *GLsync;

#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif

#ifndef GL_TIMEOUT_IGNORED
#define GL_TIMEOUT_IGNORED 0xFFFFFFFFFFFFFFFFull
#endif

#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED 0x911A
#endif

#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED 0x911B
#endif

#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED 0x911C
#endif

#ifndef GL_WAIT_FAILED
#define GL_WAIT_FAILED 0x911D
#endif

typedef GLsync (*f_FenceSync) (GLenum condition, GLbitfield flags);
typedef GLboolean (*f_IsSync) (GLsync sync);
typedef void (*f_DeleteSync) (GLsync sync);
typedef GLenum (*f_ClientWaitSync) (GLsync sync, GLbitfield flags,
    GLuint64 timeout);
typedef void (*f_WaitSync) (GLsync sync, GLbitfield flags,
    GLuint64 timeout);
typedef GLsync (*f_ImportSyncEXT) (GLenum external_sync_type,
    GLintptr external_sync, GLbitfield flags);
#endif

#ifdef DEBUG_GLX_MARK
typedef void (*f_StringMarkerGREMEDY) (GLsizei len, const void *string);
typedef void (*f_FrameTerminatorGREMEDY) (void);
#endif

/// @brief Wrapper of a binded GLX texture.
struct _glx_texture {
  GLuint texture;
  GLXPixmap glpixmap;
  Pixmap pixmap;
  GLenum target;
  unsigned width;
  unsigned height;
  unsigned depth;
  bool y_inverted;
};

typedef struct {
  Pixmap pixmap;
  Picture pict;
  glx_texture_t *ptex;
} paint_t;

#define PAINT_INIT { .pixmap = None, .pict = None }

typedef struct {
  int size;
  double *data;
} conv;

/// A representation of raw region data
typedef struct {
  XRectangle *rects;
  int nrects;
} reg_data_t;

#define REG_DATA_INIT { NULL, 0 }

struct _win;

/// Enumeration for window event hints.
typedef enum {
  WIN_EVMODE_UNKNOWN,
  WIN_EVMODE_FRAME,
  WIN_EVMODE_CLIENT
} win_evmode_t;

extern const char * const VSYNC_STRS[NUM_VSYNC + 1];
extern session_t *ps_g;

// == Debugging code ==
static inline void
print_timestamp(session_t *ps);

#ifdef DEBUG_BACKTRACE

#include <execinfo.h>
#define BACKTRACE_SIZE  25

/**
 * Print current backtrace.
 *
 * Stolen from glibc manual.
 */
static inline void
print_backtrace(void) {
  void *array[BACKTRACE_SIZE];
  size_t size;
  char **strings;

  size = backtrace(array, BACKTRACE_SIZE);
  strings = backtrace_symbols(array, size);

  for (size_t i = 0; i < size; i++)
     printf ("%s\n", strings[i]);

  free(strings);
}

#ifdef DEBUG_ALLOC_REG

/**
 * Wrapper of <code>XFixesCreateRegion</code>, for debugging.
 */
static inline XserverRegion
XFixesCreateRegion_(Display *dpy, XRectangle *p, int n,
    const char *func, int line) {
  XserverRegion reg = XFixesCreateRegion(dpy, p, n);
  print_timestamp(ps_g);
  printf("%#010lx: XFixesCreateRegion() in %s():%d\n", reg, func, line);
  print_backtrace();
  fflush(stdout);
  return reg;
}

/**
 * Wrapper of <code>XFixesDestroyRegion</code>, for debugging.
 */
static inline void
XFixesDestroyRegion_(Display *dpy, XserverRegion reg,
    const char *func, int line) {
  XFixesDestroyRegion(dpy, reg);
  print_timestamp(ps_g);
  printf("%#010lx: XFixesDestroyRegion() in %s():%d\n", reg, func, line);
  fflush(stdout);
}

#define XFixesCreateRegion(dpy, p, n) XFixesCreateRegion_(dpy, p, n, __func__, __LINE__)
#define XFixesDestroyRegion(dpy, reg) XFixesDestroyRegion_(dpy, reg, __func__, __LINE__)
#endif

#endif

// === Functions ===

/**
 * @brief Quit if the passed-in pointer is empty.
 */
static inline void *
allocchk_(const char *func_name, void *ptr) {
  if (!ptr) {
    printf_err("%s(): Failed to allocate memory.", func_name);
    exit(1);
  }
  return ptr;
}

/// @brief Wrapper of allocchk_().
#define allocchk(ptr) allocchk_(__func__, ptr)

/// @brief Wrapper of malloc().
#define cmalloc(nmemb, type) ((type *) allocchk(malloc((nmemb) * sizeof(type))))

/// @brief Wrapper of calloc().
#define ccalloc(nmemb, type) ((type *) allocchk(calloc((nmemb), sizeof(type))))

/// @brief Wrapper of ealloc().
#define crealloc(ptr, nmemb, type) ((type *) allocchk(realloc((ptr), (nmemb) * sizeof(type))))

/// @brief Zero out the given memory block.
#define cmemzero(ptr, size) memset((ptr), 0, (size))

/// @brief Wrapper of cmemzero() that handles a pointer to a single item, for
///        convenience.
#define cmemzero_one(ptr) cmemzero((ptr), sizeof(*(ptr)))

/**
 * Return whether a struct timeval value is empty.
 */
static inline bool
timeval_isempty(struct timeval *ptv) {
  if (!ptv)
    return false;

  return ptv->tv_sec <= 0 && ptv->tv_usec <= 0;
}

/**
 * Compare a struct timeval with a time in milliseconds.
 *
 * @return > 0 if ptv > ms, 0 if ptv == 0, -1 if ptv < ms
 */
static inline int
timeval_ms_cmp(struct timeval *ptv, time_ms_t ms) {
  assert(ptv);

  // We use those if statement instead of a - expression because of possible
  // truncation problem from long to int.
  {
    long sec = ms / MS_PER_SEC;
    if (ptv->tv_sec > sec)
      return 1;
    if (ptv->tv_sec < sec)
      return -1;
  }

  {
    long usec = ms % MS_PER_SEC * (US_PER_SEC / MS_PER_SEC);
    if (ptv->tv_usec > usec)
      return 1;
    if (ptv->tv_usec < usec)
      return -1;
  }

  return 0;
}

/**
 * Subtracting two struct timeval values.
 *
 * Taken from glibc manual.
 *
 * Subtract the `struct timeval' values X and Y,
 * storing the result in RESULT.
 * Return 1 if the difference is negative, otherwise 0.
 */
static inline int
timeval_subtract(struct timeval *result,
                 struct timeval *x,
                 struct timeval *y) {
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    long nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }

  if (x->tv_usec - y->tv_usec > 1000000) {
    long nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}

/**
 * Subtracting two struct timespec values.
 *
 * Taken from glibc manual.
 *
 * Subtract the `struct timespec' values X and Y,
 * storing the result in RESULT.
 * Return 1 if the difference is negative, otherwise 0.
 */
static inline int
timespec_subtract(struct timespec *result,
                 const struct timespec *x,
                 const struct timespec *y) {
    struct timespec tmp = *y;
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_nsec < tmp.tv_nsec) {
    long nsec = (tmp.tv_nsec - x->tv_nsec) / NS_PER_SEC + 1;
    tmp.tv_nsec -= NS_PER_SEC * nsec;
    tmp.tv_sec += nsec;
  }

  if (x->tv_nsec - tmp.tv_nsec > NS_PER_SEC) {
    long nsec = (x->tv_nsec - tmp.tv_nsec) / NS_PER_SEC;
    tmp.tv_nsec += NS_PER_SEC * nsec;
    tmp.tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_nsec is certainly positive. */
  result->tv_sec = x->tv_sec - tmp.tv_sec;
  result->tv_nsec = x->tv_nsec - tmp.tv_nsec;

  /* Return 1 if result is negative. */
  return x->tv_sec < tmp.tv_sec;
}

/**
 * Get current time in struct timeval.
 */
static inline struct timeval
get_time_timeval(void) {
  struct timeval tv = { 0, 0 };

  gettimeofday(&tv, NULL);

  // Return a time of all 0 if the call fails
  return tv;
}

/**
 * Get current time in struct timespec.
 *
 * Note its starting time is unspecified.
 */
static inline struct timespec
get_time_timespec(void) {
  struct timespec tm = { 0, 0 };

  clock_gettime(CLOCK_MONOTONIC, &tm);

  // Return a time of all 0 if the call fails
  return tm;
}


/**
 * Print time passed since program starts execution.
 *
 * Used for debugging.
 */
static inline void
print_timestamp(session_t *ps) {
  struct timeval tm, diff;

  if (gettimeofday(&tm, NULL)) return;

  timeval_subtract(&diff, &tm, &ps->time_start);
  printf("[ %5ld.%02ld ] ", diff.tv_sec, diff.tv_usec / 10000);
}

/**
 * Allocate the space and copy a string.
 */
static inline char *
mstrcpy(const char *src) {
  char *str = cmalloc(strlen(src) + 1, char);

  strcpy(str, src);

  return str;
}

/**
 * Allocate the space and copy a string.
 */
static inline char *
mstrncpy(const char *src, unsigned len) {
  char *str = cmalloc(len + 1, char);

  strncpy(str, src, len);
  str[len] = '\0';

  return str;
}

/**
 * Allocate the space and join two strings.
 */
static inline char *
mstrjoin(const char *src1, const char *src2) {
  char *str = cmalloc(strlen(src1) + strlen(src2) + 1, char);

  strcpy(str, src1);
  strcat(str, src2);

  return str;
}

/**
 * Allocate the space and join two strings;
 */
static inline char *
mstrjoin3(const char *src1, const char *src2, const char *src3) {
  char *str = cmalloc(strlen(src1) + strlen(src2)
        + strlen(src3) + 1, char);

  strcpy(str, src1);
  strcat(str, src2);
  strcat(str, src3);

  return str;
}

/**
 * Concatenate a string on heap with another string.
 */
static inline void
mstrextend(char **psrc1, const char *src2) {
  *psrc1 = crealloc(*psrc1, (*psrc1 ? strlen(*psrc1): 0) + strlen(src2) + 1,
      char);

  strcat(*psrc1, src2);
}

/**
 * Normalize an int value to a specific range.
 *
 * @param i int value to normalize
 * @param min minimal value
 * @param max maximum value
 * @return normalized value
 */
static inline int __attribute__((const))
normalize_i_range(int i, int min, int max) {
  if (i > max) return max;
  if (i < min) return min;
  return i;
}

/**
 * Select the larger integer of two.
 */
static inline int __attribute__((const))
max_i(int a, int b) {
  return (a > b ? a : b);
}

/**
 * Select the smaller integer of two.
 */
static inline int __attribute__((const))
min_i(int a, int b) {
  return (a > b ? b : a);
}

/**
 * Select the larger long integer of two.
 */
static inline long __attribute__((const))
max_l(long a, long b) {
  return (a > b ? a : b);
}

/**
 * Select the smaller long integer of two.
 */
static inline long __attribute__((const))
min_l(long a, long b) {
  return (a > b ? b : a);
}

/**
 * Normalize a double value to a specific range.
 *
 * @param d double value to normalize
 * @param min minimal value
 * @param max maximum value
 * @return normalized value
 */
static inline double __attribute__((const))
normalize_d_range(double d, double min, double max) {
  if (d > max) return max;
  if (d < min) return min;
  return d;
}

/**
 * Normalize a double value to 0.\ 0 - 1.\ 0.
 *
 * @param d double value to normalize
 * @return normalized value
 */
static inline double __attribute__((const))
normalize_d(double d) {
  return normalize_d_range(d, 0.0, 1.0);
}

/**
 * Add a file descriptor to a select() fd_set.
 */
static inline bool
fds_insert_select(fd_set **ppfds, int fd) {
  assert(fd <= FD_SETSIZE);

  if (!*ppfds) {
    if ((*ppfds = malloc(sizeof(fd_set)))) {
      FD_ZERO(*ppfds);
    }
    else {
      fprintf(stderr, "Failed to allocate memory for select() fdset.\n");
      exit(1);
    }
  }

  FD_SET(fd, *ppfds);

  return true;
}

/**
 * Add a new file descriptor to wait for.
 */
static inline bool
fds_insert(session_t *ps, int fd, short events) {
  bool result = true;

  ps->nfds_max = max_i(fd + 1, ps->nfds_max);

  if (POLLIN & events)
    result = fds_insert_select(&ps->pfds_read, fd) && result;
  if (POLLOUT & events)
    result = fds_insert_select(&ps->pfds_write, fd) && result;
  if (POLLPRI & events)
    result = fds_insert_select(&ps->pfds_except, fd) && result;

  return result;
}

/**
 * Delete a file descriptor to wait for.
 */
static inline void
fds_drop(session_t *ps, int fd, short events) {
  // Drop fd from respective fd_set-s
  if (POLLIN & events && ps->pfds_read)
    FD_CLR(fd, ps->pfds_read);
  if (POLLOUT & events && ps->pfds_write)
    FD_CLR(fd, ps->pfds_write);
  if (POLLPRI & events && ps->pfds_except)
    FD_CLR(fd, ps->pfds_except);
}

#define CPY_FDS(key) \
  fd_set * key = NULL; \
  if (ps->key) { \
    key = malloc(sizeof(fd_set)); \
    memcpy(key, ps->key, sizeof(fd_set)); \
    if (!key) { \
      fprintf(stderr, "Failed to allocate memory for copying select() fdset.\n"); \
      exit(1); \
    } \
  } \

/**
 * Poll for changes.
 *
 * poll() is much better than select(), but ppoll() does not exist on
 * *BSD.
 */
static inline int
fds_poll(session_t *ps, struct timeval *ptv) {
  // Copy fds
  CPY_FDS(pfds_read);
  CPY_FDS(pfds_write);
  CPY_FDS(pfds_except);

  int ret = select(ps->nfds_max, pfds_read, pfds_write, pfds_except, ptv);

  free(pfds_read);
  free(pfds_write);
  free(pfds_except);

  return ret;
}
#undef CPY_FDS

/**
 * Wrapper of XFree() for convenience.
 *
 * Because a NULL pointer cannot be passed to XFree(), its man page says.
 */
static inline void
cxfree(void *data) {
  if (data)
    XFree(data);
}

/**
 * Return the painting target window.
 */
static inline Window
get_tgt_window(session_t *ps) {
  return ps->overlay;
}

/**
 * Check if there's a GLX context.
 */
static inline bool
glx_has_context(session_t *ps) {
  return ps->psglx && ps->psglx->context;
}

/**
 * Copies a region.
 */
static inline XserverRegion
copy_region(const session_t *ps, XserverRegion oldregion) {
  if (!oldregion)
    return None;

  XserverRegion region = XFixesCreateRegion(ps->dpy, NULL, 0);

  XFixesCopyRegion(ps->dpy, region, oldregion);

  return region;
}

/**
 * Destroy a <code>XserverRegion</code>.
 */
static inline void
free_region(session_t *ps, XserverRegion *p) {
  if (*p) {
    XFixesDestroyRegion(ps->dpy, *p);
    *p = None;
  }
}

/**
 * Free a XSync fence.
 */
static inline void
free_fence(session_t *ps, XSyncFence *pfence) {
  if (*pfence)
    XSyncDestroyFence(ps->dpy, *pfence);
  *pfence = None;
}

/**
 * Crop a rectangle by another rectangle.
 *
 * psrc and pdst cannot be the same.
 */
static inline void
rect_crop(XRectangle *pdst, const XRectangle *psrc, const XRectangle *pbound) {
  assert(psrc != pdst);
  pdst->x = max_i(psrc->x, pbound->x);
  pdst->y = max_i(psrc->y, pbound->y);
  pdst->width = max_i(0, min_i(psrc->x + psrc->width, pbound->x + pbound->width) - pdst->x);
  pdst->height = max_i(0, min_i(psrc->y + psrc->height, pbound->y + pbound->height) - pdst->y);
}

/**
 * Determine if a window has a specific property.
 *
 * @param ps current session
 * @param w window to check
 * @param atom atom of property to check
 * @return 1 if it has the attribute, 0 otherwise
 */
static inline bool
wid_has_prop(const session_t *ps, Window w, Atom atom) {
  Atom type = None;
  int format;
  unsigned long nitems, after;
  unsigned char *data;

  if (Success == XGetWindowProperty(ps->dpy, w, atom, 0, 0, False,
        AnyPropertyType, &type, &format, &nitems, &after, &data)) {
    cxfree(data);
    if (type) return true;
  }

  return false;
}

/**
 * Wrapper of wid_get_prop_adv().
 */
static inline winprop_t
wid_get_prop(struct X11Context* xcontext, Window wid, Atom atom, long length, Atom rtype, int rformat) {
    return wid_get_prop_adv(xcontext, wid, atom, 0L, length, rtype, rformat);
}

/**
 * Get the numeric property value from a win_prop_t.
 */
static inline long
winprop_get_int(winprop_t prop) {
  long tgt = 0;

  if (!prop.nitems)
    return 0;

  switch (prop.format) {
    case 8:   tgt = *(prop.data.p8);    break;
    case 16:  tgt = *(prop.data.p16);   break;
    case 32:  tgt = *(prop.data.p32);   break;
    default:  assert(0);
              break;
  }

  return tgt;
}

bool
wid_get_text_prop(session_t *ps, Window wid, Atom prop,
    char ***pstrlst, int *pnstr);

/**
 * Free a <code>winprop_t</code>.
 *
 * @param pprop pointer to the <code>winprop_t</code> to free.
 */
static inline void
free_winprop(winprop_t *pprop) {
  // Empty the whole structure to avoid possible issues
  if (pprop->data.p8) {
    cxfree(pprop->data.p8);
    pprop->data.p8 = NULL;
  }
  pprop->nitems = 0;
}

void
force_repaint(session_t *ps);

bool
vsync_init(session_t *ps);

void
vsync_deinit(session_t *ps);

/** @name GLX
 */
///@{

#ifdef CONFIG_GLX_SYNC
void
xr_glx_sync(session_t *ps, Drawable d, XSyncFence *pfence);
#endif

bool
glx_init(session_t *ps, bool need_render);

void
glx_destroy(session_t *ps);

void
glx_on_root_change(session_t *ps);

bool
glx_init_blur(session_t *ps);

void
glx_release_pixmap(session_t *ps, glx_texture_t *ptex);

void
glx_paint_pre(session_t *ps);

/**
 * Check if a texture is binded, or is binded to the given pixmap.
 */
static inline bool
glx_tex_binded(const glx_texture_t *ptex, Pixmap pixmap) {
  return ptex && ptex->glpixmap && ptex->texture
    && (!pixmap || pixmap == ptex->pixmap);
}

bool
glx_blur_dst(session_t *ps, const Vector2* pos, const Vector2* size, float z,
    GLfloat factor_center, glx_blur_cache_t *pbc, win* w);

bool
glx_dim_dst(session_t *ps, int dx, int dy, int width, int height, float z,
    GLfloat factor);

bool
glx_render_(session_t *ps, const struct Texture* ptex,
    int x, int y, int dx, int dy, int width, int height, int z,
    double opacity, bool neg);

#define \
   glx_render(ps, ptex, x, y, dx, dy, width, height, z, opacity, neg) \
  glx_render_(ps, ptex, x, y, dx, dy, width, height, z, opacity, neg)

void
glx_swap_copysubbuffermesa(session_t *ps, XserverRegion reg);

unsigned char *
glx_take_screenshot(session_t *ps, int *out_length);

GLuint
glx_create_shader(GLenum shader_type, const char *shader_str);

GLuint
glx_create_program(const GLuint * const shaders, int nshaders, const bool isVertex);

GLuint
glx_create_program_from_str(const char *vert_shader_str,
    const char *frag_shader_str);

/**
 * Free a GLX texture.
 */
static inline void
free_texture_r(session_t *ps, GLuint *ptexture) {
  if (*ptexture) {
    assert(glx_has_context(ps));
    glDeleteTextures(1, ptexture);
    *ptexture = 0;
  }
}

/**
 * Free a GLX Framebuffer object.
 */
static inline void
free_glx_fbo(session_t *ps, GLuint *pfbo) {
  if (*pfbo) {
    glDeleteFramebuffers(1, pfbo);
    *pfbo = 0;
  }
  assert(!*pfbo);
}

/**
 * Add a OpenGL debugging marker.
 */
static inline void
glx_mark_(session_t *ps, const char *func, XID xid, bool start) {
#ifdef DEBUG_GLX_MARK
  if (glx_has_context(ps) && ps->psglx->glStringMarkerGREMEDY) {
    if (!func) func = "(unknown)";
    const char *postfix = (start ? " (start)": " (end)");
    char *str = malloc((strlen(func) + 12 + 2
          + strlen(postfix) + 5) * sizeof(char));
    strcpy(str, func);
    sprintf(str + strlen(str), "(%#010lx)%s", xid, postfix);
    ps->psglx->glStringMarkerGREMEDY(strlen(str), str);
    free(str);
  }
#endif
}

#define glx_mark(ps, xid, start) glx_mark_(ps, __func__, xid, start)

/**
 * Add a OpenGL debugging marker.
 */
static inline void
glx_mark_frame(session_t *ps) {
#ifdef DEBUG_GLX_MARK
  if (glx_has_context(ps) && ps->psglx->glFrameTerminatorGREMEDY)
    ps->psglx->glFrameTerminatorGREMEDY();
#endif
}

///@}


/**
 * @brief Dump the given data to a file.
 */
static inline bool
write_binary_data(const char *path, const unsigned char *data, int length) {
  if (!data)
    return false;
  FILE *f = fopen(path, "wb");
  if (!f) {
    printf_errf("(\"%s\"): Failed to open file for writing.", path);
    return false;
  }
  int wrote_len = fwrite(data, sizeof(unsigned char), length, f);
  fclose(f);
  if (wrote_len != length) {
    printf_errf("(\"%s\"): Failed to write all blocks: %d / %d", path,
        wrote_len, length);
    return false;
  }
  return true;
}

/**
 * @brief Dump raw bytes in HEX format.
 *
 * @param data pointer to raw data
 * @param len length of data
 */
static inline void
hexdump(const char *data, int len) {
  static const int BYTE_PER_LN = 16;

  if (len <= 0)
    return;

  // Print header
  printf("%10s:", "Offset");
  for (int i = 0; i < BYTE_PER_LN; ++i)
    printf(" %2d", i);
  putchar('\n');

  // Dump content
  for (int offset = 0; offset < len; ++offset) {
    if (!(offset % BYTE_PER_LN))
      printf("0x%08x:", offset);

    printf(" %02hhx", data[offset]);

    if ((BYTE_PER_LN - 1) == offset % BYTE_PER_LN)
      putchar('\n');
  }
  if (len % BYTE_PER_LN)
    putchar('\n');

  fflush(stdout);
}

#endif
