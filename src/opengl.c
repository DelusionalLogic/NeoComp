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
#include "blur.h"
#include "textureeffects.h"
#include "assets/assets.h"
#include "assets/shader.h"
#include "assets/face.h"
#include "shaders/shaderinfo.h"

#include "renderutil.h"

#ifdef CONFIG_GLX_SYNC
void
xr_glx_sync(session_t *ps, Drawable d, XSyncFence *pfence) {
  if (*pfence) {
    // GLsync sync = ps->psglx->glFenceSyncProc(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    GLsync sync = ps->psglx->glImportSyncEXT(GL_SYNC_X11_FENCE_EXT, *pfence, 0);
    /* GLenum ret = ps->psglx->glClientWaitSyncProc(sync, GL_SYNC_FLUSH_COMMANDS_BIT,
        1000);
    assert(GL_CONDITION_SATISFIED == ret); */
    XSyncTriggerFence(ps->dpy, *pfence);
    XFlush(ps->dpy);
    ps->psglx->glWaitSyncProc(sync, 0, GL_TIMEOUT_IGNORED);
    // ps->psglx->glDeleteSyncProc(sync);
    // XSyncResetFence(ps->dpy, *pfence);
  }
  glx_check_err(ps);
}
#endif

static inline GLXFBConfig
get_fbconfig_from_visualinfo(session_t *ps, const XVisualInfo *visualinfo) {
  int nelements = 0;
  GLXFBConfig *fbconfigs = glXGetFBConfigs(ps->dpy, visualinfo->screen,
      &nelements);
  for (int i = 0; i < nelements; ++i) {
    int visual_id = 0;
    if (Success == glXGetFBConfigAttrib(ps->dpy, fbconfigs[i], GLX_VISUAL_ID, &visual_id)
        && visual_id == visualinfo->visualid)
      return fbconfigs[i];
  }

  return NULL;
}

#ifdef DEBUG_GLX_DEBUG_CONTEXT
static void
glx_debug_msg_callback(GLenum source, GLenum type,
    GLuint id, GLenum severity, GLsizei length, const GLchar *message,
    GLvoid *userParam) {
  printf_dbgf("(): source 0x%04X, type 0x%04X, id %u, severity 0x%0X, \"%s\"\n",
      source, type, id, severity, message);
  void* returns[5];
  backtrace(returns, 5);
  char** names = backtrace_symbols(returns, 5);
  for(int i = 0; i < 5; i++) {
      char* str = names[i];
      if(str == NULL)
          break;
      printf_dbgf("(): backtrace: %s\n", str);
  }
  free(names);
}
#endif

/**
 * Initialize OpenGL.
 */
bool
glx_init(session_t *ps, bool need_render) {
  bool success = false;
  XVisualInfo *pvis = NULL;

  // Check for GLX extension
  if (!ps->glx_exists) {
    if (glXQueryExtension(ps->dpy, &ps->glx_event, &ps->glx_error))
      ps->glx_exists = true;
    else {
      printf_errf("(): No GLX extension.");
      goto glx_init_end;
    }
  }

  // Get XVisualInfo
  pvis = get_visualinfo_from_visual(ps, ps->vis);
  if (!pvis) {
    printf_errf("(): Failed to acquire XVisualInfo for current visual.");
    goto glx_init_end;
  }

  // Ensure the visual is double-buffered
  if (need_render) {
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
  }

  // Ensure GLX_EXT_texture_from_pixmap exists
  if (need_render && !glx_hasglxext(ps, "GLX_EXT_texture_from_pixmap"))
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
#ifndef DEBUG_GLX_DEBUG_CONTEXT
    psglx->context = glXCreateContext(ps->dpy, pvis, None, GL_TRUE);
#else
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
#endif
    if (!psglx->context) {
      printf_errf("(): Failed to get GLX context.");
      goto glx_init_end;
    }

    // Attach GLX context
    if (!glXMakeCurrent(ps->dpy, get_tgt_window(ps), psglx->context)) {
      printf_errf("(): Failed to attach GLX context.");
      goto glx_init_end;
    }

#ifdef DEBUG_GLX_DEBUG_CONTEXT
    {
      f_DebugMessageCallback p_DebugMessageCallback =
        (f_DebugMessageCallback)
        glXGetProcAddress((const GLubyte *) "glDebugMessageCallback");
      if (!p_DebugMessageCallback) {
        printf_errf("(): Failed to get glDebugMessageCallback(0.");
        goto glx_init_end;
      }
      p_DebugMessageCallback(glx_debug_msg_callback, ps);
    }
#endif

  }

  // Ensure we have a stencil buffer. X Fixes does not guarantee rectangles
  // in regions don't overlap, so we must use stencil buffer to make sure
  // we don't paint a region for more than one time, I think?
  if (need_render && !ps->o.glx_no_stencil) {
    GLint val = 0;
    glGetIntegerv(GL_STENCIL_BITS, &val);
    if (!val) {
      printf_errf("(): Target window doesn't have stencil buffer.");
      goto glx_init_end;
    }
  }

  // Check GL_ARB_texture_non_power_of_two, requires a GLX context and
  // must precede FBConfig fetching
  if (need_render)
    psglx->has_texture_non_power_of_two = glx_hasglext(ps,
        "GL_ARB_texture_non_power_of_two");

  // Acquire function addresses
  if (need_render) {
#ifdef DEBUG_GLX_MARK
    psglx->glStringMarkerGREMEDY = (f_StringMarkerGREMEDY)
      glXGetProcAddress((const GLubyte *) "glStringMarkerGREMEDY");
    psglx->glFrameTerminatorGREMEDY = (f_FrameTerminatorGREMEDY)
      glXGetProcAddress((const GLubyte *) "glFrameTerminatorGREMEDY");
#endif

    psglx->glXBindTexImageProc = (f_BindTexImageEXT)
      glXGetProcAddress((const GLubyte *) "glXBindTexImageEXT");
    psglx->glXReleaseTexImageProc = (f_ReleaseTexImageEXT)
      glXGetProcAddress((const GLubyte *) "glXReleaseTexImageEXT");
    if (!psglx->glXBindTexImageProc || !psglx->glXReleaseTexImageProc) {
      printf_errf("(): Failed to acquire glXBindTexImageEXT() / glXReleaseTexImageEXT().");
      goto glx_init_end;
    }

    if (ps->o.glx_use_copysubbuffermesa) {
      psglx->glXCopySubBufferProc = (f_CopySubBuffer)
        glXGetProcAddress((const GLubyte *) "glXCopySubBufferMESA");
      if (!psglx->glXCopySubBufferProc) {
        printf_errf("(): Failed to acquire glXCopySubBufferMESA().");
        goto glx_init_end;
      }
    }

#ifdef CONFIG_GLX_SYNC
    psglx->glFenceSyncProc = (f_FenceSync)
      glXGetProcAddress((const GLubyte *) "glFenceSync");
    psglx->glIsSyncProc = (f_IsSync)
      glXGetProcAddress((const GLubyte *) "glIsSync");
    psglx->glDeleteSyncProc = (f_DeleteSync)
      glXGetProcAddress((const GLubyte *) "glDeleteSync");
    psglx->glClientWaitSyncProc = (f_ClientWaitSync)
      glXGetProcAddress((const GLubyte *) "glClientWaitSync");
    psglx->glWaitSyncProc = (f_WaitSync)
      glXGetProcAddress((const GLubyte *) "glWaitSync");
    psglx->glImportSyncEXT = (f_ImportSyncEXT)
      glXGetProcAddress((const GLubyte *) "glImportSyncEXT");
    if (!psglx->glFenceSyncProc || !psglx->glIsSyncProc || !psglx->glDeleteSyncProc
        || !psglx->glClientWaitSyncProc || !psglx->glWaitSyncProc
        || !psglx->glImportSyncEXT) {
      printf_errf("(): Failed to acquire GLX sync functions.");
      goto glx_init_end;
    }
#endif
  }

  // Acquire FBConfigs
  if (need_render && !glx_update_fbconfig(ps))
    goto glx_init_end;

  // Render preparations
  if (need_render) {
    glx_on_root_change(ps);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glDisable(GL_BLEND);

    if (!ps->o.glx_no_stencil) {
      // Initialize stencil buffer
      glClear(GL_STENCIL_BUFFER_BIT);
      glDisable(GL_STENCIL_TEST);
      glStencilMask(0x1);
      glStencilFunc(GL_EQUAL, 0x1, 0x1);
    }

    // Clear screen
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // glXSwapBuffers(ps->dpy, get_tgt_window(ps));
  }

  add_shader_type(&global_info);
  add_shader_type(&downsample_info);
  add_shader_type(&upsample_info);
  add_shader_type(&passthough_info);
  add_shader_type(&profiler_info);

  assets_add_handler(struct shader, "vs", vert_shader_load_file, shader_unload_file);
  assets_add_handler(struct shader, "fs", frag_shader_load_file, shader_unload_file);
  assets_add_handler(struct face, "face", face_load_file, face_unload_file);
  assets_add_handler(struct shader_program, "shader", shader_program_load_file,
    shader_program_unload_file);

  assets_add_path("./assets/");

  success = true;

glx_init_end:
  cxfree(pvis);

  if (!success)
    glx_destroy(ps);

  return success;
}

static void
glx_free_prog_main(session_t *ps, glx_prog_main_t *pprogram) {
  if (!pprogram)
    return;
  if (pprogram->prog) {
    glDeleteProgram(pprogram->prog);
    pprogram->prog = 0;
  }
  pprogram->unifm_opacity = -1;
  pprogram->unifm_invert_color = -1;
  pprogram->unifm_tex = -1;
}

/**
 * Destroy GLX related resources.
 */
void
glx_destroy(session_t *ps) {
  if (!ps->psglx)
    return;

  // Free all GLX resources of windows
  for (win *w = ps->list; w; w = w->next)
    free_win_res_glx(ps, w);

  blur_destroy(&ps->psglx->blur);

  glx_free_prog_main(ps, &ps->o.glx_prog_win);

  glx_check_err(ps);

  // Free FBConfigs
  for (int i = 0; i <= OPENGL_MAX_DEPTH; ++i) {
    free(ps->psglx->fbconfigs[i]);
    ps->psglx->fbconfigs[i] = NULL;
  }

  // Destroy GLX context
  if (ps->psglx->context) {
    glXDestroyContext(ps->dpy, ps->psglx->context);
    ps->psglx->context = NULL;
  }

  free(ps->psglx);
  ps->psglx = NULL;
}

/**
 * Reinitialize GLX.
 */
bool
glx_reinit(session_t *ps, bool need_render) {
  // Reinitialize VSync as well
  vsync_deinit(ps);

  glx_destroy(ps);
  if (!glx_init(ps, need_render)) {
    printf_errf("(): Failed to initialize GLX.");
    return false;
  }

  if (!vsync_init(ps)) {
    printf_errf("(): Failed to initialize VSync.");
    return false;
  }

  return true;
}

/**
 * Callback to run on root window size change.
 */
void
glx_on_root_change(session_t *ps) {
  glViewport(0, 0, ps->root_width, ps->root_height);

  ps->psglx->view = orthogonal(0, ps->root_width, 0, ps->root_height, -1000.0, 1000.0);

  // Initialize matrix, copied from dcompmgr
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, ps->root_width, 0, ps->root_height, -1000.0, 1000.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

/**
 * Initialize GLX blur filter.
 */
bool
glx_init_blur(session_t *ps) {
  assert(ps->o.blur_kerns[0]);

  // Allocate PBO if more than one blur kernel is present
  if (ps->o.blur_kerns[1]) {
    // Try to generate a framebuffer
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    if (!fbo) {
      printf_errf("(): Failed to generate Framebuffer. Cannot do "
          "multi-pass blur with GLX backend.");
      return false;
    }
    glDeleteFramebuffers(1, &fbo);
  }

  blur_init(&ps->psglx->blur);

  glx_check_err(ps);

  return true;
}

/**
 * Load a GLSL main program from shader strings.
 */
bool
glx_load_prog_main(session_t *ps,
    const char *vshader_str, const char *fshader_str,
    glx_prog_main_t *pprogram) {
  assert(pprogram);

  // Build program
  printf_dbgf("(): Creating global shader");
  pprogram->prog = glx_create_program_from_str(vshader_str, fshader_str);
  if (!pprogram->prog) {
    printf_errf("(): Failed to create GLSL program.");
    return false;
  }

  // Get uniform addresses
#define P_GET_UNIFM_LOC(name, target) { \
      pprogram->target = glGetUniformLocation(pprogram->prog, name); \
      if (pprogram->target < 0) { \
        printf_errf("(): Failed to get location of uniform '" name "'. Might be troublesome."); \
      } \
    }
  P_GET_UNIFM_LOC("opacity", unifm_opacity);
  P_GET_UNIFM_LOC("invert_color", unifm_invert_color);
  P_GET_UNIFM_LOC("tex", unifm_tex);
#undef P_GET_UNIFM_LOC

  glx_check_err(ps);

  return true;
}

/**
 * @brief Update the FBConfig of given depth.
 */
static inline void
glx_update_fbconfig_bydepth(session_t *ps, int depth, glx_fbconfig_t *pfbcfg) {
  // Make sure the depth is sane
  if (depth < 0 || depth > OPENGL_MAX_DEPTH)
    return;

  // Compare new FBConfig with current one
  if (glx_cmp_fbconfig(ps, ps->psglx->fbconfigs[depth], pfbcfg) < 0) {
#ifdef DEBUG_GLX
    printf_dbgf("(%d): %#x overrides %#x, target %#x.\n", depth, (unsigned) pfbcfg->cfg, (ps->psglx->fbconfigs[depth] ? (unsigned) ps->psglx->fbconfigs[depth]->cfg: 0), pfbcfg->texture_tgts);
#endif
    if (!ps->psglx->fbconfigs[depth]) {
      ps->psglx->fbconfigs[depth] = malloc(sizeof(glx_fbconfig_t));
      allocchk(ps->psglx->fbconfigs[depth]);
    }
    (*ps->psglx->fbconfigs[depth]) = *pfbcfg;
  }
}

/**
 * Get GLX FBConfigs for all depths.
 */
static bool
glx_update_fbconfig(session_t *ps) {
  // Acquire all FBConfigs and loop through them
  int nele = 0;
  GLXFBConfig* pfbcfgs = glXGetFBConfigs(ps->dpy, ps->scr, &nele);

  for (GLXFBConfig *pcur = pfbcfgs; pcur < pfbcfgs + nele; pcur++) {
    glx_fbconfig_t fbinfo = {
      .cfg = *pcur,
      .texture_fmt = 0,
      .texture_tgts = 0,
      .y_inverted = false,
    };
    int id = (int) (pcur - pfbcfgs);
    int depth = 0, depth_alpha = 0, val = 0;

    // Skip over multi-sampled visuals
    // http://people.freedesktop.org/~glisse/0001-glx-do-not-use-multisample-visual-config-for-front-o.patch
#ifdef GLX_SAMPLES
    if (Success == glXGetFBConfigAttrib(ps->dpy, *pcur, GLX_SAMPLES, &val)
        && val > 1)
      continue;
#endif

    if (Success != glXGetFBConfigAttrib(ps->dpy, *pcur, GLX_BUFFER_SIZE, &depth)
        || Success != glXGetFBConfigAttrib(ps->dpy, *pcur, GLX_ALPHA_SIZE, &depth_alpha)) {
      printf_errf("(): Failed to retrieve buffer size and alpha size of FBConfig %d.", id);
      continue;
    }
    if (Success != glXGetFBConfigAttrib(ps->dpy, *pcur, GLX_BIND_TO_TEXTURE_TARGETS_EXT, &fbinfo.texture_tgts)) {
      printf_errf("(): Failed to retrieve BIND_TO_TEXTURE_TARGETS_EXT of FBConfig %d.", id);
      continue;
    }

    int visualdepth = 0;
    {
      XVisualInfo *pvi = glXGetVisualFromFBConfig(ps->dpy, *pcur);
      if (!pvi) {
        // On nvidia-drivers-325.08 this happens slightly too often...
        // printf_errf("(): Failed to retrieve X Visual of FBConfig %d.", id);
        continue;
      }
      visualdepth = pvi->depth;
      cxfree(pvi);
    }

    bool rgb = false;
    bool rgba = false;

    if (depth >= 32 && depth_alpha && Success == glXGetFBConfigAttrib(ps->dpy, *pcur, GLX_BIND_TO_TEXTURE_RGBA_EXT, &val) && val)
      rgba = true;

    if (Success == glXGetFBConfigAttrib(ps->dpy, *pcur, GLX_BIND_TO_TEXTURE_RGB_EXT, &val) && val)
      rgb = true;

    if (Success == glXGetFBConfigAttrib(ps->dpy, *pcur, GLX_Y_INVERTED_EXT, &val))
      fbinfo.y_inverted = val;

    {
      int tgtdpt = depth - depth_alpha;
      if (tgtdpt == visualdepth && tgtdpt < 32 && rgb) {
        fbinfo.texture_fmt = GLX_TEXTURE_FORMAT_RGB_EXT;
        glx_update_fbconfig_bydepth(ps, tgtdpt, &fbinfo);
      }
    }

    if (depth == visualdepth && rgba) {
      fbinfo.texture_fmt = GLX_TEXTURE_FORMAT_RGBA_EXT;
      glx_update_fbconfig_bydepth(ps, depth, &fbinfo);
    }
  }

  cxfree(pfbcfgs);

  // Sanity checks
  if (!ps->psglx->fbconfigs[ps->depth]) {
    printf_errf("(): No FBConfig found for default depth %d.", ps->depth);
    return false;
  }

  if (!ps->psglx->fbconfigs[32]) {
    printf_errf("(): No FBConfig found for depth 32. Expect crazy things.");
  }

#ifdef DEBUG_GLX
  printf_dbgf("(): %d-bit: %#3x, 32-bit: %#3x\n",
      ps->depth, (int) ps->psglx->fbconfigs[ps->depth]->cfg,
      (int) ps->psglx->fbconfigs[32]->cfg);
#endif

  return true;
}

static inline int
glx_cmp_fbconfig_cmpattr(session_t *ps,
    const glx_fbconfig_t *pfbc_a, const glx_fbconfig_t *pfbc_b,
    int attr) {
  int attr_a = 0, attr_b = 0;

  // TODO: Error checking
  glXGetFBConfigAttrib(ps->dpy, pfbc_a->cfg, attr, &attr_a);
  glXGetFBConfigAttrib(ps->dpy, pfbc_b->cfg, attr, &attr_b);

  return attr_a - attr_b;
}

/**
 * Compare two GLX FBConfig's to find the preferred one.
 */
static int
glx_cmp_fbconfig(session_t *ps,
    const glx_fbconfig_t *pfbc_a, const glx_fbconfig_t *pfbc_b) {
  int result = 0;

  if (!pfbc_a)
    return -1;
  if (!pfbc_b)
    return 1;

#define P_CMPATTR_LT(attr) { if ((result = glx_cmp_fbconfig_cmpattr(ps, pfbc_a, pfbc_b, (attr)))) return -result; }
#define P_CMPATTR_GT(attr) { if ((result = glx_cmp_fbconfig_cmpattr(ps, pfbc_a, pfbc_b, (attr)))) return result; }

  P_CMPATTR_LT(GLX_BIND_TO_TEXTURE_RGBA_EXT);
  P_CMPATTR_LT(GLX_DOUBLEBUFFER);
  P_CMPATTR_LT(GLX_STENCIL_SIZE);
  P_CMPATTR_LT(GLX_DEPTH_SIZE);
  P_CMPATTR_GT(GLX_BIND_TO_MIPMAP_TEXTURE_EXT);

  return 0;
}

/**
 * Bind a X pixmap to an OpenGL texture.
 */
bool
glx_bind_pixmap(session_t *ps, glx_texture_t **pptex, Pixmap pixmap,
    unsigned width, unsigned height, unsigned depth) {
  if (!pixmap) {
    printf_errf("(%#010lx): Binding to an empty pixmap. This can't work.",
        pixmap);
    return false;
  }

  glx_texture_t *ptex = *pptex;
  bool need_release = true;

  // Allocate structure
  if (!ptex) {
    static const glx_texture_t GLX_TEX_DEF = {
      .texture = 0,
      .glpixmap = 0,
      .pixmap = 0,
      .target = 0,
      .width = 0,
      .height = 0,
      .depth = 0,
      .y_inverted = false,
    };

    ptex = malloc(sizeof(glx_texture_t));
    allocchk(ptex);
    memcpy(ptex, &GLX_TEX_DEF, sizeof(glx_texture_t));
    *pptex = ptex;
  }

  // Release pixmap if parameters are inconsistent
  if (ptex->texture && ptex->pixmap != pixmap) {
    glx_release_pixmap(ps, ptex);
  }

  // Create GLX pixmap
  if (!ptex->glpixmap) {
    need_release = false;

    // Retrieve pixmap parameters, if they aren't provided
    if (!(width && height && depth)) {
      Window rroot = None;
      int rx = 0, ry = 0;
      unsigned rbdwid = 0;
      if (!XGetGeometry(ps->dpy, pixmap, &rroot, &rx, &ry,
            &width, &height, &rbdwid, &depth)) {
        printf_errf("(%#010lx): Failed to query Pixmap info.", pixmap);
        return false;
      }
      if (depth > OPENGL_MAX_DEPTH) {
        printf_errf("(%d): Requested depth higher than %d.", depth,
            OPENGL_MAX_DEPTH);
        return false;
      }
    }

    const glx_fbconfig_t *pcfg = ps->psglx->fbconfigs[depth];
    if (!pcfg) {
      printf_errf("(%d): Couldn't find FBConfig with requested depth.", depth);
      return false;
    }

    // Determine texture target, copied from compiz
    // The assumption we made here is the target never changes based on any
    // pixmap-specific parameters, and this may change in the future
    GLenum tex_tgt = 0;
    tex_tgt = GLX_TEXTURE_2D_EXT;

#ifdef DEBUG_GLX
    printf_dbgf("(): depth %d, tgt %#x, rgba %d\n", depth, tex_tgt,
        (GLX_TEXTURE_FORMAT_RGBA_EXT == pcfg->texture_fmt));
#endif

    GLint attrs[] = {
        GLX_TEXTURE_FORMAT_EXT,
        pcfg->texture_fmt,
        GLX_TEXTURE_TARGET_EXT,
        tex_tgt,
        0,
    };

    ptex->glpixmap = glXCreatePixmap(ps->dpy, pcfg->cfg, pixmap, attrs);
    ptex->pixmap = pixmap;
    ptex->target = GL_TEXTURE_2D;
    ptex->width = width;
    ptex->height = height;
    ptex->depth = depth;
    ptex->y_inverted = pcfg->y_inverted;
  }
  if (!ptex->glpixmap) {
    printf_errf("(): Failed to allocate GLX pixmap.");
    return false;
  }


  // Create texture
  if (!ptex->texture) {
    need_release = false;

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    ptex->texture = texture;
  }
  if (!ptex->texture) {
    printf_errf("(): Failed to allocate texture.");
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, ptex->texture);

  // The specification requires rebinding whenever the content changes...
  // We can't follow this, too slow.
  if (need_release)
    ps->psglx->glXReleaseTexImageProc(ps->dpy, ptex->glpixmap, GLX_FRONT_LEFT_EXT);

  ps->psglx->glXBindTexImageProc(ps->dpy, ptex->glpixmap, GLX_FRONT_LEFT_EXT, NULL);

  // Cleanup
  glBindTexture(GL_TEXTURE_2D, 0);

  glx_check_err(ps);

  return true;
}

/**
 * @brief Release binding of a texture.
 */
void
glx_release_pixmap(session_t *ps, glx_texture_t *ptex) {
  // Release binding
  if (ptex->glpixmap && ptex->texture) {
    glBindTexture(GL_TEXTURE_2D, ptex->texture);
    ps->psglx->glXReleaseTexImageProc(ps->dpy, ptex->glpixmap, GLX_FRONT_LEFT_EXT);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  // Free GLX Pixmap
  if (ptex->glpixmap) {
    glXDestroyPixmap(ps->dpy, ptex->glpixmap);
    ptex->glpixmap = 0;
  }

  glx_check_err(ps);
}

/**
 * Preprocess function before start painting.
 */
void
glx_paint_pre(session_t *ps, XserverRegion *damaged) {
  ps->psglx->z = 0.0;
  // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Get buffer age
  bool trace_damage = (ps->o.glx_swap_method < 0 || ps->o.glx_swap_method > 1);

  // Trace raw damage regions
  XserverRegion newdamage = None;
  if (trace_damage && *damaged)
    newdamage = copy_region(ps, *damaged);

  // OpenGL doesn't support partial repaint without GLX_MESA_copy_sub_buffer,
  // we could redraw the whole screen or copy unmodified pixels from
  // front buffer with --glx-copy-from-front.
  if (ps->o.glx_use_copysubbuffermesa || !*damaged) {
  }
  else {
    int buffer_age = ps->o.glx_swap_method;

    // Getting buffer age
    {
      // Query GLX_EXT_buffer_age for buffer age
      if (SWAPM_BUFFER_AGE == buffer_age) {
        unsigned val = 0;
        glXQueryDrawable(ps->dpy, get_tgt_window(ps),
            GLX_BACK_BUFFER_AGE_EXT, &val);
        buffer_age = val;
      }

      // Buffer age too high
      if (buffer_age > CGLX_MAX_BUFFER_AGE + 1)
        buffer_age = 0;

      // Make sure buffer age >= 0
      buffer_age = max_i(buffer_age, 0);

      // Check if we have we have empty regions
      if (buffer_age > 1) {
        for (int i = 0; i < buffer_age - 1; ++i)
          if (!ps->all_damage_last[i]) { buffer_age = 0; break; }
      }
    }

    // Do nothing for buffer_age 1 (copy)
    if (1 != buffer_age) {
      // Copy pixels
      if (ps->o.glx_copy_from_front) {
        // Determine copy area
        XserverRegion reg_copy = XFixesCreateRegion(ps->dpy, NULL, 0);
        if (!buffer_age) {
          XFixesSubtractRegion(ps->dpy, reg_copy, ps->screen_reg, *damaged);
        }
        else {
          for (int i = 0; i < buffer_age - 1; ++i)
            XFixesUnionRegion(ps->dpy, reg_copy, reg_copy,
                ps->all_damage_last[i]);
          XFixesSubtractRegion(ps->dpy, reg_copy, reg_copy, *damaged);
        }

        // Actually copy pixels
        {
          GLfloat raster_pos[4];
          GLfloat curx = 0.0f, cury = 0.0f;
          glGetFloatv(GL_CURRENT_RASTER_POSITION, raster_pos);
          glReadBuffer(GL_FRONT);
          glRasterPos2f(0.0, 0.0);
          {
            int nrects = 0;
            XRectangle *rects = XFixesFetchRegion(ps->dpy, reg_copy, &nrects);
            for (int i = 0; i < nrects; ++i) {
              const int x = rects[i].x;
              const int y = ps->root_height - rects[i].y - rects[i].height;
              // Kwin patch says glRasterPos2f() causes artifacts on bottom
              // screen edge with some drivers
              glBitmap(0, 0, 0, 0, x - curx, y - cury, NULL);
              curx = x;
              cury = y;
              glCopyPixels(x, y, rects[i].width, rects[i].height, GL_COLOR);
            }
            cxfree(rects);
          }
          glReadBuffer(GL_BACK);
          glRasterPos4fv(raster_pos);
        }

        free_region(ps, &reg_copy);
      }

      // Determine paint area
      if (ps->o.glx_copy_from_front) { }
      else if (buffer_age) {
        for (int i = 0; i < buffer_age - 1; ++i)
          XFixesUnionRegion(ps->dpy, *damaged, *damaged, ps->all_damage_last[i]);
      }
      else {
        free_region(ps, damaged);
      }
    }
  }

  if (trace_damage) {
    free_region(ps, &ps->all_damage_last[CGLX_MAX_BUFFER_AGE - 1]);
    memmove(ps->all_damage_last + 1, ps->all_damage_last,
        (CGLX_MAX_BUFFER_AGE - 1) * sizeof(XserverRegion));
    ps->all_damage_last[0] = newdamage;
  }

  glx_set_clip(ps, *damaged, NULL);

#ifdef DEBUG_GLX_PAINTREG
  glx_render_color(ps, 0, 0, ps->root_width, ps->root_height, 0, *damaged, NULL);
#endif

  glx_check_err(ps);
}

static Vector2 X11_rectpos_to_gl(session_t *ps, const Vector2* xpos, const Vector2* size) {
    Vector2 glpos = {{
        xpos->x, ps->root_height - xpos->y - size->y
    }};
    return glpos;
}

/**
 * Set clipping region on the target window.
 */
void
glx_set_clip(session_t *ps, XserverRegion reg, const reg_data_t *pcache_reg) {
  // Quit if we aren't using stencils
  if (ps->o.glx_no_stencil)
    return;

  static XRectangle rect_blank = { .x = 0, .y = 0, .width = 0, .height = 0 };

  glDisable(GL_STENCIL_TEST);
  glDisable(GL_SCISSOR_TEST);

  if (!reg)
    return;

  int nrects = 0;
  XRectangle *rects_free = NULL;
  const XRectangle *rects = NULL;
  if (pcache_reg) {
    rects = pcache_reg->rects;
    nrects = pcache_reg->nrects;
  }
  if (!rects) {
    nrects = 0;
    rects = rects_free = XFixesFetchRegion(ps->dpy, reg, &nrects);
  }
  // Use one empty rectangle if the region is empty
  if (!nrects) {
    cxfree(rects_free);
    rects_free = NULL;
    nrects = 1;
    rects = &rect_blank;
  }

  assert(nrects);
  if (1 == nrects) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(rects[0].x, ps->root_height - rects[0].y - rects[0].height,
        rects[0].width, rects[0].height);
  }
  else {
    glEnable(GL_STENCIL_TEST);
    glClear(GL_STENCIL_BUFFER_BIT);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glStencilOp(GL_REPLACE, GL_KEEP, GL_KEEP);

    // @CLEANUP: remove this
    Vector2 root_size = {{ps->root_width, ps->root_height}};
    Vector2 pixeluv = {{1.0f, 1.0f}};
    vec2_div(&pixeluv, &root_size);

    struct shader_program* passthough_program = assets_load("passthough.shader");
    if(passthough_program->shader_type_info != &passthough_info) {
        printf_errf("Shader was not a passthough shader");
        return;
    }

    struct Passthough* passthough_type = passthough_program->shader_type;
    shader_use(passthough_program);

    struct face* face = assets_load("window.face");

    for (int i = 0; i < nrects; ++i) {
      Vector2 rectPos = {{rects[i].x, rects[i].y}};
      Vector2 rectSize = {{rects[i].width, rects[i].height}};
      Vector2 glRectPos = X11_rectpos_to_gl(ps, &rectPos, &rectSize);

      Vector2 scale = pixeluv;
      vec2_mul(&scale, &rectSize);

      Vector2 relpos = pixeluv;
      vec2_mul(&relpos, &glRectPos);


#ifdef DEBUG_GLX
      printf_dbgf("(): Rect %d: %f, %f, %f, %f\n", i, relpos.x, relpos.y, scale.x, scale.y);
#endif

      draw_rect(face, passthough_type->mvp, relpos, scale);
    }

    glUseProgram(0);

    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // glDepthMask(GL_TRUE);
  }

  cxfree(rects_free);

  glx_check_err(ps);
}

#define P_PAINTREG_START() \
  XserverRegion reg_new = None; \
  XRectangle rec_all = { .x = dx, .y = dy, .width = width, .height = height }; \
  XRectangle *rects = &rec_all; \
  int nrects = 1; \
 \
  if (ps->o.glx_no_stencil && reg_tgt) { \
    if (pcache_reg) { \
      rects = pcache_reg->rects; \
      nrects = pcache_reg->nrects; \
    } \
    else { \
      reg_new = XFixesCreateRegion(ps->dpy, &rec_all, 1); \
      XFixesIntersectRegion(ps->dpy, reg_new, reg_new, reg_tgt); \
 \
      nrects = 0; \
      rects = XFixesFetchRegion(ps->dpy, reg_new, &nrects); \
    } \
  } \
  glBegin(GL_QUADS); \
 \
  for (int ri = 0; ri < nrects; ++ri) { \
    XRectangle crect; \
    rect_crop(&crect, &rects[ri], &rec_all); \
 \
    if (!crect.width || !crect.height) \
      continue; \

#define P_PAINTREG_END() \
  } \
  glEnd(); \
 \
  if (rects && rects != &rec_all && !(pcache_reg && pcache_reg->rects == rects)) \
    cxfree(rects); \
  free_region(ps, &reg_new); \

/**
 * Blur contents in a particular region.
 */
bool
glx_blur_dst(session_t *ps, const Vector2* pos, const Vector2* size, float z,
    GLfloat factor_center,
    XserverRegion reg_tgt, const reg_data_t *pcache_reg,
    glx_blur_cache_t *pbc) {
    bool ret = blur_backbuffer(&ps->psglx->blur, ps, pos, size, z, factor_center, reg_tgt, pcache_reg, pbc);
    glx_check_err(ps);
    return ret;
}

bool
glx_dim_dst(session_t *ps, int dx, int dy, int width, int height, float z,
    GLfloat factor, XserverRegion reg_tgt, const reg_data_t *pcache_reg) {
  // It's possible to dim in glx_render(), but it would be over-complicated
  // considering all those mess in color negation and modulation
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(0.0f, 0.0f, 0.0f, factor);

  {
    P_PAINTREG_START();
    {
      GLint rdx = crect.x;
      GLint rdy = ps->root_height - crect.y;
      GLint rdxe = rdx + crect.width;
      GLint rdye = rdy - crect.height;

      glVertex3i(rdx, rdy, z);
      glVertex3i(rdxe, rdy, z);
      glVertex3i(rdxe, rdye, z);
      glVertex3i(rdx, rdye, z);
    }
    P_PAINTREG_END();
  }

  glEnd();

  glColor4f(0.0f, 0.0f, 0.0f, 0.0f);
  glDisable(GL_BLEND);

  glx_check_err(ps);

  return true;
}

void glx_shadow_dst(session_t *ps, win* w, const Vector2* pos, const Vector2* size, float z) {
    Vector2 border = {{64, 64}};

    Vector2 overflowSize = border;
    vec2_imul(&overflowSize, 2);
    vec2_add(&overflowSize, size);

    struct Texture texture;
    if(texture_init(&texture, GL_TEXTURE_2D, &overflowSize) != 0) {
        printf("Couldn't create texture for shadow\n");
        return;
    }

    struct RenderBuffer buffer;
    if(renderbuffer_stencil_init(&buffer, &overflowSize) != 0) {
        printf("Couldn't create renderbuffer stencil for shadow\n");
        texture_delete(&texture);
        return;
    }

    struct Framebuffer framebuffer;
    if(!framebuffer_init(&framebuffer)) {
        printf("Couldn't create framebuffer for shadow\n");
        texture_delete(&texture);
        renderbuffer_delete(&buffer);
        return;
    }

    framebuffer_targetTexture(&framebuffer, &texture);
    framebuffer_targetRenderBuffer_stencil(&framebuffer, &buffer);
    framebuffer_bind(&framebuffer);

    glViewport(0, 0, texture.size.x, texture.size.y);

    glEnable(GL_BLEND);
    glEnable(GL_STENCIL_TEST);

    glClearColor(0.0, 0.0, 0.0, 0.0);

    glStencilMask(0xFF);
    glClearStencil(0);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    // @CLEANUP: We have to do this since the window isn't using the new nice
    // interface
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, w->paint.ptex->texture);

    struct shader_program* global_program = assets_load("shadow.shader");
    if(global_program->shader_type_info != &global_info) {
        printf_errf("Shader was not a global shader\n");
        texture_delete(&texture);
        renderbuffer_delete(&buffer);
        framebuffer_delete(&framebuffer);
        return;
    }

    struct Global* global_type = global_program->shader_type;
    shader_use(global_program);

    shader_set_uniform_float(global_type->invert, false);
    shader_set_uniform_float(global_type->flip, true);
    shader_set_uniform_float(global_type->opacity, 1.0);
    shader_set_uniform_sampler(global_type->tex_scr, 0);

    Vector2 pixeluv = {{1.0f, 1.0f}};
    vec2_div(&pixeluv, &texture.size);

    struct face* face = assets_load("window.face");

    Vector2 scale = pixeluv;
    vec2_mul(&scale, size);

    Vector2 relpos = pixeluv;
    vec2_mul(&relpos, &border);

#ifdef DEBUG_GLX
    printf_dbgf("SHADOW %f, %f, %f, %f\n", relpos.x, relpos.y, scale.x, scale.y);
#endif

    draw_rect(face, global_type->mvp, relpos, scale);

    glDisable(GL_STENCIL_TEST);

    // Do the blur
    if(!texture_blur(&texture, 4)) {
        printf_errf("Failed blurring the background texture");
        texture_delete(&texture);
        renderbuffer_delete(&buffer);
        framebuffer_delete(&framebuffer);
        return;
    }

    struct Texture clipBuffer;
    if(texture_init(&clipBuffer, GL_TEXTURE_2D, &buffer.size) != 0) {
        printf("Failed creating clipping renderbuffer\n");
        texture_delete(&texture);
        renderbuffer_delete(&buffer);
        framebuffer_delete(&framebuffer);
        return;
    }

    framebuffer_resetTarget(&framebuffer);
    framebuffer_targetTexture(&framebuffer, &clipBuffer);
    framebuffer_targetRenderBuffer_stencil(&framebuffer, &buffer);
    if(framebuffer_bind(&framebuffer) != 0) {
        printf("Failed binding framebuffer to clip shadow\n");

        texture_delete(&texture);
        renderbuffer_delete(&buffer);
        texture_delete(&clipBuffer);
        framebuffer_delete(&framebuffer);
        return;
    }
    glViewport(0, 0, clipBuffer.size.x, clipBuffer.size.y);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_STENCIL_TEST);

    glStencilMask(0xFF);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    draw_tex(ps, face, &texture, &VEC2_ZERO, &VEC2_UNIT);

    glDisable(GL_STENCIL_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    static const GLenum DRAWBUFS[2] = { GL_BACK_LEFT };
    glDrawBuffers(1, DRAWBUFS);

    glViewport(0, 0, ps->root_width, ps->root_height);

    /* { */
    /*     Vector2 rpos = {{0, 0}}; */
    /*     Vector2 rsize = {{.4, .6}}; */
    /*     draw_tex(ps, face, &clipBuffer, &rpos, &rsize); */
    /* } */

    Vector2 root_size = {{ps->root_width, ps->root_height}};
    {
        Vector2 rpos = X11_rectpos_to_gl(ps, pos, size);
        vec2_sub(&rpos, &border);
        Vector2 rsize = overflowSize;

        Vector2 pixeluv = {{1.0f, 1.0f}};
        vec2_div(&pixeluv, &root_size);

        Vector2 scale = pixeluv;
        vec2_mul(&scale, &rsize);

        Vector2 relpos = pixeluv;
        vec2_mul(&relpos, &rpos);

        draw_tex(ps, face, &clipBuffer, &relpos, &scale);
    }

    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    texture_delete(&texture);
    renderbuffer_delete(&buffer);
    texture_delete(&clipBuffer);
    framebuffer_delete(&framebuffer);
}

/**
 * @brief Render a region with texture data.
 */
bool
glx_render_(session_t *ps, const glx_texture_t *ptex,
    int x, int y, int dx, int dy, int width, int height, int z,
    double opacity, bool argb, bool neg,
    XserverRegion reg_tgt, const reg_data_t *pcache_reg
    , const glx_prog_main_t *pprogram
    ) {
  if (!ptex || !ptex->texture) {
    printf_errf("(): Missing texture.");
    return false;
  }

#ifdef DEBUG_GLX_PAINTREG
  glx_render_dots(ps, dx, dy, width, height, z, reg_tgt, pcache_reg);
  return true;
#endif

  argb = argb || (GLX_TEXTURE_FORMAT_RGBA_EXT ==
      ps->psglx->fbconfigs[ptex->depth]->texture_fmt);
  glEnable(GL_BLEND);

  // This is all weird, but X Render is using premultiplied ARGB format, and
  // we need to use those things to correct it. Thanks to derhass for help.
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  /* glColor4f(opacity, opacity, opacity, opacity); */

  struct shader_program* global_program = assets_load("global.shader");
  if(global_program->shader_type_info != &global_info) {
      printf_errf("Shader was not a global shader");
      // @INCOMPLETE: Make sure the config is correct
      return true;
  }

  struct Global* global_type = global_program->shader_type;
  shader_use(global_program);

  // Bind texture
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, ptex->texture);

  shader_set_uniform_float(global_type->invert, neg);
  shader_set_uniform_float(global_type->flip, ptex->y_inverted);
  shader_set_uniform_float(global_type->opacity, opacity);
  shader_set_uniform_sampler(global_type->tex_scr, 0);

#ifdef DEBUG_GLX
  printf_dbgf("(): Draw: %d, %d, %d, %d -> %d, %d (%d, %d) z %d\n", x, y, width, height, dx, dy, ptex->width, ptex->height, z);
#endif

  // @CLEANUP: remove this
  Vector2 root_size = {{ps->root_width, ps->root_height}};
  Vector2 pixeluv = {{1.0f, 1.0f}};
  vec2_div(&pixeluv, &root_size);

  struct face* face = assets_load("window.face");

  // Painting
  {
    XserverRegion reg_new = None;
    XRectangle rec_all = { .x = dx, .y = dy, .width = width, .height = height };
    XRectangle *rects = &rec_all;
    int nrects = 1;

    if (ps->o.glx_no_stencil && reg_tgt) {
        if (pcache_reg) {
            rects = pcache_reg->rects;
            nrects = pcache_reg->nrects;
        }
        else {
            reg_new = XFixesCreateRegion(ps->dpy, &rec_all, 1);
            XFixesIntersectRegion(ps->dpy, reg_new, reg_new, reg_tgt);

            nrects = 0;
            rects = XFixesFetchRegion(ps->dpy, reg_new, &nrects);
        }
    }

    for (int ri = 0; ri < nrects; ++ri) {
      XRectangle crect;
      rect_crop(&crect, &rects[ri], &rec_all);

      Vector2 rectPos = {{crect.x, crect.y}};
      Vector2 rectSize = {{crect.width, crect.height}};
      Vector2 glRectPos = X11_rectpos_to_gl(ps, &rectPos, &rectSize);

      Vector2 scale = pixeluv;
      vec2_mul(&scale, &rectSize);

      Vector2 relpos = pixeluv;
      vec2_mul(&relpos, &glRectPos);

#ifdef DEBUG_GLX
      printf_dbgf("(): Rect %f, %f, %f, %f\n", relpos.x, relpos.y, scale.x, scale.y);
#endif

      draw_rect(face, global_type->mvp, relpos, scale);
    }

    if (rects && rects != &rec_all && !(pcache_reg && pcache_reg->rects == rects))
        cxfree(rects);
    free_region(ps, &reg_new);
  }

  glUseProgram(0);

  // Cleanup
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_BLEND);

  glx_check_err(ps);

  return true;
}

/**
 * Render a region with color.
 */
static void
glx_render_color(session_t *ps, int dx, int dy, int width, int height, int z,
    XserverRegion reg_tgt, const reg_data_t *pcache_reg) {
  static int color = 0;

  color = color % (3 * 3 * 3 - 1) + 1;
  z += 0.2;

  {
    P_PAINTREG_START();
    {
      GLint rdx = crect.x;
      GLint rdy = ps->root_height - crect.y;
      GLint rdxe = rdx + crect.width;
      GLint rdye = rdy - crect.height;

	  glViewport(rdx, rdye, rdxe, rdy);
      glClearColor(1.0 / 3.0 * (color / (3 * 3)),
              1.0 / 3.0 * (color % (3 * 3) / 3),
              1.0 / 3.0 * (color % 3),
              1.0f
              );
      glClear(GL_COLOR_BUFFER_BIT);
    }
    P_PAINTREG_END();
  }
  glViewport(0, 0, ps->root_width, ps->root_height);

  glx_check_err(ps);
}

/**
 * Render a region with dots.
 */
static void
glx_render_dots(session_t *ps, int dx, int dy, int width, int height, int z,
    XserverRegion reg_tgt, const reg_data_t *pcache_reg) {
  glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
  z -= 0.1;

  {
    P_PAINTREG_START();
    {
      static const GLint BLK_WID = 5, BLK_HEI = 5;

      glEnd();
      glPointSize(1.0);
      glBegin(GL_POINTS);

      GLint rdx = crect.x;
      GLint rdy = ps->root_height - crect.y;
      GLint rdxe = rdx + crect.width;
      GLint rdye = rdy - crect.height;
      rdx = (rdx) / BLK_WID * BLK_WID;
      rdy = (rdy) / BLK_HEI * BLK_HEI;
      rdxe = (rdxe) / BLK_WID * BLK_WID;
      rdye = (rdye) / BLK_HEI * BLK_HEI;

      for (GLint cdx = rdx; cdx < rdxe; cdx += BLK_WID)
        for (GLint cdy = rdy; cdy > rdye; cdy -= BLK_HEI)
          glVertex3i(cdx + BLK_WID / 2, cdy - BLK_HEI / 2, z);
    }
    P_PAINTREG_END();
  }
  glColor4f(0.0f, 0.0f, 0.0f, 0.0f);

  glx_check_err(ps);
}

/**
 * Swap buffer with glXCopySubBufferMESA().
 */
void
glx_swap_copysubbuffermesa(session_t *ps, XserverRegion reg) {
  int nrects = 0;
  XRectangle *rects = XFixesFetchRegion(ps->dpy, reg, &nrects);

  if (1 == nrects && rect_is_fullscreen(ps, rects[0].x, rects[0].y,
        rects[0].width, rects[0].height)) {
    glXSwapBuffers(ps->dpy, get_tgt_window(ps));
  }
  else {
    glx_set_clip(ps, None, NULL);
    for (int i = 0; i < nrects; ++i) {
      const int x = rects[i].x;
      const int y = ps->root_height - rects[i].y - rects[i].height;
      const int wid = rects[i].width;
      const int hei = rects[i].height;

#ifdef DEBUG_GLX
      printf_dbgf("(): %d, %d, %d, %d\n", x, y, wid, hei);
#endif
      ps->psglx->glXCopySubBufferProc(ps->dpy, get_tgt_window(ps), x, y, wid, hei);
    }
  }

  glx_check_err(ps);

  cxfree(rects);
}

/**
 * @brief Get tightly packed RGB888 data from GL front buffer.
 *
 * Don't expect any sort of decent performance.
 *
 * @returns tightly packed RGB888 data of the size of the screen,
 *          to be freed with `free()`
 */
unsigned char *
glx_take_screenshot(session_t *ps, int *out_length) {
  int length = 3 * ps->root_width * ps->root_height;
  GLint unpack_align_old = 0;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_align_old);
  assert(unpack_align_old > 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  unsigned char *buf = cmalloc(length, unsigned char);
  glReadBuffer(GL_FRONT);
  glReadPixels(0, 0, ps->root_width, ps->root_height, GL_RGB,
      GL_UNSIGNED_BYTE, buf);
  glReadBuffer(GL_BACK);
  glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_align_old);
  if (out_length)
    *out_length = sizeof(unsigned char) * length;
  return buf;
}

GLuint
glx_create_shader(GLenum shader_type, const char *shader_str) {
#ifdef DEBUG_GLX_GLSL
  printf("glx_create_shader(): ===\n%s\n===\n", shader_str);
  fflush(stdout);
#endif

  bool success = false;
  GLuint shader = glCreateShader(shader_type);
  if (!shader) {
    printf_errf("(): Failed to create shader with type %#x.", shader_type);
    goto glx_create_shader_end;
  }
  glShaderSource(shader, 1, &shader_str, NULL);
  glCompileShader(shader);

  // Get shader status
  {
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (GL_FALSE == status) {
      GLint log_len = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
      if (log_len) {
        char log[log_len + 1];
        glGetShaderInfoLog(shader, log_len, NULL, log);
        printf_errf("(): Failed to compile shader with type %d: %s",
            shader_type, log);
      }
      goto glx_create_shader_end;
    }
  }

  success = true;

glx_create_shader_end:
  if (shader && !success) {
    glDeleteShader(shader);
    shader = 0;
  }

  return shader;
}

GLuint
glx_create_program(const GLuint * const shaders, int nshaders, const bool isVertex) {
  bool success = false;
  GLuint program = glCreateProgram();
  if (!program) {
    printf_errf("(): Failed to create program.");
    goto glx_create_program_end;
  }

  for (int i = 0; i < nshaders; ++i)
    glAttachShader(program, shaders[i]);
  if (isVertex) {
      glBindAttribLocation(program, 0, "vertex");
      glBindAttribLocation(program, 1, "uv");
  }
  glLinkProgram(program);

  // Get program status
  {
    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (GL_FALSE == status) {
      GLint log_len = 0;
      glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
      if (log_len) {
        char log[log_len + 1];
        glGetProgramInfoLog(program, log_len, NULL, log);
        printf_errf("(): Failed to link program: %s", log);
      }
      goto glx_create_program_end;
    }
  }
  success = true;

glx_create_program_end:
  if (program) {
    for (int i = 0; i < nshaders; ++i)
      glDetachShader(program, shaders[i]);
  }
  if (program && !success) {
    glDeleteProgram(program);
    program = 0;
  }

  return program;
}

/**
 * @brief Create a program from vertex and fragment shader strings.
 */
GLuint
glx_create_program_from_str(const char *vert_shader_str,
        const char *frag_shader_str) {
  GLuint vert_shader = 0;
  GLuint frag_shader = 0;
  GLuint prog = 0;

  if (vert_shader_str)
    vert_shader = glx_create_shader(GL_VERTEX_SHADER, vert_shader_str);
  if (frag_shader_str)
    frag_shader = glx_create_shader(GL_FRAGMENT_SHADER, frag_shader_str);

  {
    GLuint shaders[2];
    int count = 0;
    if (vert_shader)
      shaders[count++] = vert_shader;
    if (frag_shader)
      shaders[count++] = frag_shader;
    assert(count <= sizeof(shaders) / sizeof(shaders[0]));
    if (count)
      prog = glx_create_program(shaders, count, true);
  }

  if (vert_shader)
    glDeleteShader(vert_shader);
  if (frag_shader)
    glDeleteShader(frag_shader);

  return prog;
}

