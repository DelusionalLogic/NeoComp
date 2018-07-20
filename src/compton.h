#pragma once
/**
 * compton.h
 */

// Throw everything in here.


// === Includes ===

#include "common.h"
#include "window.h"
#include "blur.h"
#include "shadow.h"

#include <math.h>
#include <sys/select.h>
#include <limits.h>
#include <unistd.h>
#include <getopt.h>
#include <locale.h>
#include <signal.h>

#ifdef CONFIG_VSYNC_DRM
#include <fcntl.h>
// We references some definitions in drm.h, which could also be found in
// /usr/src/linux/include/drm/drm.h, but that path is probably even less
// reliable than libdrm
#include <drm.h>
#include <sys/ioctl.h>
#include <errno.h>
#endif

void convert_xrects_to_relative_rect(XRectangle* rects, size_t rect_count, Vector2* extents, Vector* mrects);

session_t * session_init(session_t *ps_old, int argc, char **argv);
void session_destroy(session_t *ps);

void session_run(session_t *ps);
