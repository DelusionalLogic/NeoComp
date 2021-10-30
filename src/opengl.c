/*
 * Compton - a compositor for X11
 *
 * Based on `xcompmgr` - Copyright (c) 2003, Keith Packard
 *
 * Copyright (c) 2011-2013, Christopher Jeffrey
 * See LICENSE for more information.
 *
 */

#include "opengl.h"
#include <execinfo.h>
#include "vmath.h"
#include "texture.h"
#include "framebuffer.h"
#include "shadow.h"
#include "textureeffects.h"
#include "assets/assets.h"
#include "assets/shader.h"
#include "assets/face.h"

#include "systems/blur.h"

#include "renderutil.h"

/**
 * Check if a word is in string.
 */
static bool wd_is_in_str(const char *haystick, const char *needle) {
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

static GLXFBConfig get_fbconfig_from_visualinfo(session_t *ps, const XVisualInfo *visualinfo) {
  int nelements = 0;
  GLXFBConfig *fbconfigs = glXGetFBConfigs(ps->dpy, visualinfo->screen,
      &nelements);
  for (int i = 0; i < nelements; ++i) {
    int visual_id = 0;
    if (Success == glXGetFBConfigAttrib(ps->dpy, fbconfigs[i], GLX_VISUAL_ID, &visual_id)
        && visual_id == visualinfo->visualid)
      return fbconfigs[i];
  }
  free(fbconfigs);

  return NULL;
}

static XVisualInfo * get_visualinfo_from_visual(session_t *ps, Visual *visual) {
  XVisualInfo vreq = { .visualid = XVisualIDFromVisual(visual) };
  int nitems = 0;

  return XGetVisualInfo(ps->dpy, VisualIDMask, &vreq, &nitems);
}

/**
 * Check if a GLX extension exists.
 */
static bool glx_hasglxext(session_t *ps, const char *ext) {
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
 * Initialize OpenGL.
 */
bool
glx_init(session_t *ps) {
  bool success = false;
  XVisualInfo *pvis = NULL;

  // Get XVisualInfo
  pvis = get_visualinfo_from_visual(ps, ps->vis);
  if (!pvis) {
    printf_errf("(): Failed to acquire XVisualInfo for current visual.");
    goto glx_init_end;
  }

  // Ensure the visual is double-buffered
  int value = 0;
  if (Success != glXGetConfig(ps->dpy, pvis, GLX_USE_GL, &value) || !value) {
      printf_errf("(): Root visual is not a GL visual.");
      goto glx_init_end;
  }

  if (Success != glXGetConfig(ps->dpy, pvis, GLX_DOUBLEBUFFER, &value)
          || !value) {
      printf_errf("(): Root visual is not a double buffered GL visual.");
      goto glx_init_end;
  }

  // Ensure GLX_EXT_texture_from_pixmap exists
  if (!glx_hasglxext(ps, "GLX_EXT_texture_from_pixmap"))
    goto glx_init_end;

  // Initialize GLX data structure
  if (!ps->psglx) {
    static const glx_session_t CGLX_SESSION_DEF = CGLX_SESSION_INIT;
    ps->psglx = cmalloc(1, glx_session_t);
    memcpy(ps->psglx, &CGLX_SESSION_DEF, sizeof(glx_session_t));
  }

  glx_session_t *psglx = ps->psglx;

  if (!psglx->context) {
    // Get GLX context
    {
      GLXFBConfig fbconfig = get_fbconfig_from_visualinfo(ps, pvis);
      if (!fbconfig) {
        printf_errf("(): Failed to get GLXFBConfig for root visual %#lx.",
            pvis->visualid);
        goto glx_init_end;
      }

      f_glXCreateContextAttribsARB p_glXCreateContextAttribsARB =
        (f_glXCreateContextAttribsARB)
        glXGetProcAddress((const GLubyte *) "glXCreateContextAttribsARB");
      if (!p_glXCreateContextAttribsARB) {
        printf_errf("(): Failed to get glXCreateContextAttribsARB().");
        goto glx_init_end;
      }

      static const int attrib_list[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 2,
        GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_DEBUG_BIT_ARB,
        GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None
      };
      psglx->context = p_glXCreateContextAttribsARB(ps->dpy, fbconfig, NULL,
          GL_TRUE, attrib_list);
    }
    if (!psglx->context) {
      printf_errf("(): Failed to get GLX context.");
      goto glx_init_end;
    }

    // Attach GLX context
    if (!glXMakeCurrent(ps->dpy, get_tgt_window(ps), psglx->context)) {
      printf_errf("(): Failed to attach GLX context.");
      goto glx_init_end;
    }

  }
  const GLubyte* version = glGetString(GL_VERSION);
  printf("Opengl version %s\n", version);

  // Ensure we have a stencil buffer. X Fixes does not guarantee rectangles
  // in regions don't overlap, so we must use stencil buffer to make sure
  // we don't paint a region for more than one time, I think?
  GLint val = 0;
  glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &val);
  // @INCOMPLETE: We'd still be able to render to an offscreen buffer, and
  // then later draw that to the default buffer. For now we'll just fail
  // though
  if (!val) {
      printf_errf("(): Target window doesn't have stencil buffer.");
      goto glx_init_end;
  }

  // Check GL_ARB_texture_non_power_of_two, requires a GLX context and
  // must precede FBConfig fetching
  psglx->has_texture_non_power_of_two = glx_hasglext(ps, "GL_ARB_texture_non_power_of_two");

  // Acquire function addresses
  psglx->glXBindTexImageProc = (f_BindTexImageEXT)
    glXGetProcAddress((const GLubyte *) "glXBindTexImageEXT");
  psglx->glXReleaseTexImageProc = (f_ReleaseTexImageEXT)
    glXGetProcAddress((const GLubyte *) "glXReleaseTexImageEXT");
  if (!psglx->glXBindTexImageProc || !psglx->glXReleaseTexImageProc) {
    printf_errf("(): Failed to acquire glXBindTexImageEXT() / glXReleaseTexImageEXT().");
    goto glx_init_end;
  }

  // Render preparations
  glViewport(0, 0, ps->root_size.x, ps->root_size.y);

  ps->psglx->view = mat4_orthogonal(0, ps->root_size.x, 0, ps->root_size.y, -.1, 1);
  view = ps->psglx->view;

  success = true;

glx_init_end:
  cxfree(pvis);

  if (!success)
    glx_destroy(ps);

  return success;
}

/**
 * Destroy GLX related resources.
 */
void
glx_destroy(session_t *ps) {
  if (!ps->psglx)
    return;

  glx_check_err(ps);

  // Free FBConfigs
  for (int i = 0; i <= OPENGL_MAX_DEPTH; ++i) {
    free(ps->psglx->fbconfigs[i]);
    ps->psglx->fbconfigs[i] = NULL;
  }

  // Destroy GLX context
  glXMakeCurrent(ps->dpy, 0, 0);
  glXDestroyContext(ps->dpy, ps->psglx->context);
  ps->psglx->context = NULL;

  free(ps->psglx);
  ps->psglx = NULL;
}
