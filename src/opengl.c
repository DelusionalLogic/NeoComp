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
#include "shadow.h"
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
  char* version = glGetString(GL_VERSION);
  printf("Opengl version %s\n", version);

  // Ensure we have a stencil buffer. X Fixes does not guarantee rectangles
  // in regions don't overlap, so we must use stencil buffer to make sure
  // we don't paint a region for more than one time, I think?
  if (need_render) {
    GLint val = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &val);
    // @INCOMPLETE: We'd still be able to render to an offscreen buffer, and
    // then later draw that to the default buffer. For now we'll just fail
    // though
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

  // Render preparations
  if (need_render) {
    glx_on_root_change(ps);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // Initialize stencil buffer
    glClear(GL_STENCIL_BUFFER_BIT);
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0x1);
    glStencilFunc(GL_EQUAL, 0x1, 0x1);

    // Clear screen
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // glXSwapBuffers(ps->dpy, get_tgt_window(ps));
  }

  add_shader_type(&global_info);
  add_shader_type(&downsample_info);
  add_shader_type(&upsample_info);
  add_shader_type(&passthough_info);
  add_shader_type(&profiler_info);
  add_shader_type(&text_info);
  add_shader_type(&shadow_info);
  add_shader_type(&stencil_info);
  add_shader_type(&colored_info);

  assets_add_handler(struct shader, "vs", vert_shader_load_file, shader_unload_file);
  assets_add_handler(struct shader, "fs", frag_shader_load_file, shader_unload_file);
  assets_add_handler(struct face, "face", face_load_file, face_unload_file);
  assets_add_handler(struct shader_program, "shader", shader_program_load_file,
    shader_program_unload_file);

  assets_add_path("./assets/");

  if(!framebuffer_init(&psglx->stencil_fbo)) {
      printf_errf("Failed initializing the stencil framebuffer");
      goto glx_init_end;
  }

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

  // Free all GLX resources of windows
  static const enum ComponentType req_types[] = { COMPONENT_MUD, 0 };
  struct SwissIterator it = {0};
  swiss_getFirst(&ps->win_list, req_types, &it);
  while(!it.done) {
      win* w = swiss_getComponent(&ps->win_list, COMPONENT_MUD, it.id);

      blur_cache_delete(&w->glx_blur_cache);
      shadow_cache_delete(&w->shadow_cache);

      swiss_getNext(&ps->win_list, req_types, &it);
  }

  blur_destroy(&ps->psglx->blur);

  glx_check_err(ps);

  // Free FBConfigs
  for (int i = 0; i <= OPENGL_MAX_DEPTH; ++i) {
    free(ps->psglx->fbconfigs[i]);
    ps->psglx->fbconfigs[i] = NULL;
  }

  xorgContext_delete(&ps->psglx->xcontext);

  framebuffer_delete(&ps->psglx->stencil_fbo);

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

  ps->psglx->view = mat4_orthogonal(0, ps->root_width, 0, ps->root_height, -.1, 1);
  view = ps->psglx->view;
}

/**
 * Initialize GLX blur filter.
 */
bool
glx_init_blur(session_t *ps) {
  blur_init(&ps->psglx->blur);

  glx_check_err(ps);

  return true;
}

/**
 * Preprocess function before start painting.
 */
void
glx_paint_pre(session_t *ps) {
  ps->psglx->z = 0.0;
  // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
  glx_mark(ps, 0xDEADBEEF, true);

  static XRectangle rect_blank = { .x = 0, .y = 0, .width = 0, .height = 0 };

  glDisable(GL_STENCIL_TEST);
  glDisable(GL_SCISSOR_TEST);

  if (!reg) {
    glx_mark(ps, 0xDEADBEEF, false);
    return;
  }

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


#ifdef DEBUG_GLX
      printf_dbgf("(): Rect %d: %f, %f, %f, %f\n", i, relpos.x, relpos.y, scale.x, scale.y);
#endif

      {
          Vector3 pos = vec3_from_vec2(&glRectPos, 0.0);
          draw_rect(face, passthough_type->mvp, pos, rectSize);
      }
    }

    /* glUseProgram(0); */

    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // glDepthMask(GL_TRUE);
  }

  cxfree(rects_free);

  glx_check_err(ps);
  glx_mark(ps, 0xDEADBEEF, false);
}

/**
 * Blur contents in a particular region.
 */
bool
glx_blur_dst(session_t *ps, const Vector2* pos, const Vector2* size, float z,
    GLfloat factor_center,
    glx_blur_cache_t *pbc, win* w) {
    bool ret = blur_backbuffer(&ps->psglx->blur, ps, pos, size, z, factor_center, pbc, w);
    glx_check_err(ps);
    return ret;
}

bool
glx_dim_dst(session_t *ps, int dx, int dy, int width, int height, float z,
    GLfloat factor) {
  // It's possible to dim in glx_render(), but it would be over-complicated
  // considering all those mess in color negation and modulation

  // @INCOMPLETE: Stop for now

  /* glEnable(GL_BLEND); */
  /* glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); */
  /* glColor4f(0.0f, 0.0f, 0.0f, factor); */

  /* { */
  /*   P_PAINTREG_START(); */
  /*   { */
  /*     GLint rdx = crect.x; */
  /*     GLint rdy = ps->root_height - crect.y; */
  /*     GLint rdxe = rdx + crect.width; */
  /*     GLint rdye = rdy - crect.height; */

  /*     glVertex3i(rdx, rdy, z); */
  /*     glVertex3i(rdxe, rdy, z); */
  /*     glVertex3i(rdxe, rdye, z); */
  /*     glVertex3i(rdx, rdye, z); */
  /*   } */
  /*   P_PAINTREG_END(); */
  /* } */

  /* glEnd(); */

  /* glColor4f(0.0f, 0.0f, 0.0f, 0.0f); */
  /* glDisable(GL_BLEND); */

  /* glx_check_err(ps); */

  return true;
}

/**
 * @brief Render a region with texture data.
 */
bool
glx_render_(session_t *ps, const struct Texture* ptex,
    int x, int y, int dx, int dy, int width, int height, int z,
    double opacity, bool neg) {
    assert(ptex != NULL);

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

  shader_set_future_uniform_bool(global_type->invert, neg);
  shader_set_future_uniform_bool(global_type->flip, ptex->flipped);
  shader_set_future_uniform_float(global_type->opacity, (float)opacity);
  shader_set_future_uniform_sampler(global_type->tex_scr, 0);

  shader_use(global_program);

  // Bind texture
  texture_bind(ptex, GL_TEXTURE0);


#ifdef DEBUG_GLX
  printf_dbgf("(): Draw: %d, %d, %d, %d -> %d, %d (%d, %d) z %d\n", x, y, width, height, dx, dy, ptex->width, ptex->height, z);
#endif

  struct face* face = assets_load("window.face");

  // Painting
  {
      Vector2 rectPos = {{dx, dy}};
      Vector2 rectSize = {{width, height}};
      Vector2 glRectPos = X11_rectpos_to_gl(ps, &rectPos, &rectSize);

#ifdef DEBUG_GLX
      printf_dbgf("(): Rect %f, %f, %f, %f\n", relpos.x, relpos.y, scale.x, scale.y);
#endif

	  {
          Vector3 pos = vec3_from_vec2(&glRectPos, 0.0);
          draw_rect(face, global_type->mvp, pos, rectSize);
	  }
  }

  /* glUseProgram(0); */

  // Cleanup
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_BLEND);

  glx_check_err(ps);

  return true;
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

