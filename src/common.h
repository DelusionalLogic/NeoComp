/*
 * Compton - a compositor for X11
 *
 * Based on `xcompmgr` - Copyright (c) 2003, Keith Packard
 *
 * Copyright (c) 2011-2013, Christopher Jeffrey
 * See LICENSE for more information.
 *
 */

#pragma once

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
// #define DEBUG_GLX_PAINTREG 1

// Whether to enable parsing of configuration files using libconfig.
// #define CONFIG_LIBCONFIG 1

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

// === Constants ===
#if !(COMPOSITE_MAJOR > 0 || COMPOSITE_MINOR >= 2)
#error libXcomposite version unsupported
#endif

/// @brief Length of generic buffers.
#define BUF_LEN 80

#define OPAQUE 0xffffffff
#define REGISTER_PROP "_NET_WM_CM_S"

#define TIME_MS_MAX 1000
#define FADE_DELTA_TOLERANCE 0.2
#define TIMEOUT_RUN_TOLERANCE 0.05
#define WIN_GET_LEADER_MAX_RECURSION 20

#define SEC_WRAP (15L * 24L * 60L * 60L)

#define NS_PER_SEC 1000000000L
#define US_PER_SEC 1000000L
#define MS_PER_SEC 1000

/// @brief Maximum OpenGL buffer age.
#define CGLX_MAX_BUFFER_AGE 5

// Window flags

// Window size is changed
#define WFLAG_SIZE_CHANGE   0x0001
// Window size/position is changed
#define WFLAG_POS_CHANGE    0x0002

// === Types ===

typedef uint32_t opacity_t;

typedef GLXContext (*f_glXCreateContextAttribsARB) (Display *dpy,
    GLXFBConfig config, GLXContext share_context, Bool direct,
    const int *attrib_list);
/* typedef void (*GLDEBUGPROC) (GLenum source, GLenum type, */
/*     GLuint id, GLenum severity, GLsizei length, const GLchar* message, */
/*     GLvoid* userParam); */

#define PAINT_INIT { .pixmap = None, .pict = None }

#define REG_DATA_INIT { NULL, 0 }

struct _win;

extern session_t *ps_g;

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

/// @brief Wrapper of ealloc().
#define crealloc(ptr, nmemb, type) ((type *) allocchk(realloc((ptr), (nmemb) * sizeof(type))))

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
 * Allocate the space and copy a string.
 */
static inline char *
mstrcpy(const char *src) {
  char *str = cmalloc(strlen(src) + 1, char);

  strcpy(str, src);

  return str;
}

/** @name GLX
 */
///@{
bool
glx_init(session_t *ps);

void
glx_destroy(session_t *ps);
///@}
