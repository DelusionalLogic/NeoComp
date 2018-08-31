#pragma once
/*
 * Compton - a compositor for X11
 *
 * Based on `xcompmgr` - Copyright (c) 2003, Keith Packard
 *
 * Copyright (c) 2011-2013, Christopher Jeffrey
 * See LICENSE for more information.
 *
 */

#include "common.h"

#include <ctype.h>
#include <locale.h>

#ifdef DEBUG_GLX_ERR

/**
 * Get a textual representation of an OpenGL error.
 */
static inline const char *
glx_dump_err_str(GLenum err) {
  switch (err) {
    CASESTRRET(GL_NO_ERROR);
    CASESTRRET(GL_INVALID_ENUM);
    CASESTRRET(GL_INVALID_VALUE);
    CASESTRRET(GL_INVALID_OPERATION);
    CASESTRRET(GL_INVALID_FRAMEBUFFER_OPERATION);
    CASESTRRET(GL_OUT_OF_MEMORY);
    CASESTRRET(GL_STACK_UNDERFLOW);
    CASESTRRET(GL_STACK_OVERFLOW);
  }

  return NULL;
}

/**
 * Check for GLX error.
 *
 * http://blog.nobel-joergensen.com/2013/01/29/debugging-opengl-using-glgeterror/
 */
static inline void
glx_check_err_(session_t *ps, const char *func, int line) {
  if (!ps->psglx->context) return;

  GLenum err = GL_NO_ERROR;

  while (GL_NO_ERROR != (err = glGetError())) {
    print_timestamp(ps);
    printf("%s():%d: GLX error ", func, line);
    const char *errtext = glx_dump_err_str(err);
    if (errtext) {
      printf_dbg("%s\n", errtext);
    }
    else {
      printf_dbg("%d\n", err);
    }
  }
}

#define glx_check_err(ps) glx_check_err_(ps, __func__, __LINE__)
#else
#define glx_check_err(ps) ((void) 0)
#endif

/**
 * Check if a word is in string.
 */
static inline bool wd_is_in_str(const char *haystick, const char *needle) {
  if (!haystick)
    return false;

  assert(*needle);

  const char *pos = haystick - 1;
  while ((pos = strstr(pos + 1, needle))) {
    // Continue if it isn't a word boundary
    if (((pos - haystick) && !isspace(*(pos - 1)))
        || (strlen(pos) > strlen(needle) && !isspace(pos[strlen(needle)])))
      continue;
    return true;
  }

  return false;
}

/**
 * Check if a GLX extension exists.
 */
static inline bool glx_hasglxext(session_t *ps, const char *ext) {
  const char *glx_exts = glXQueryExtensionsString(ps->dpy, ps->scr);
  if (!glx_exts) {
    printf_errf("(): Failed get GLX extension list.");
    return false;
  }

  bool found = wd_is_in_str(glx_exts, ext);
  if (!found)
    printf_errf("(): Missing GLX extension %s.", ext);

  return found;
}

/**
 * Check if a GLX extension exists.
 */
static inline bool glx_hasglext(session_t *ps, const char *ext) {
    GLint n;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for(int i = 0; i < n; i++) {
        const char* extension = glGetStringi(GL_EXTENSIONS, i);
        if(strcmp(ext, extension)) {
            return true;
        }
    }
    printf_errf("(): Missing GL extension %s.", ext);
    return false;
}
