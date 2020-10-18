#pragma once
/**
 * compton.h
 */

// Throw everything in here.


// === Includes ===

#include "common.h"
#include "window.h"
#include "shadow.h"

#include "systems/blur.h"

#include <math.h>
#include <sys/select.h>
#include <limits.h>
#include <unistd.h>
#include <getopt.h>
#include <locale.h>
#include <signal.h>

void convert_xrects_to_relative_rect(XRectangle* rects, size_t rect_count, Vector2* extents, Vector2* offset, Vector* mrects);
bool do_win_fade(struct Bezier* curve, double dt, Swiss* em);
void commit_destroy(Swiss* em);

session_t * session_init(session_t *ps_old, int argc, char **argv);
void session_destroy(session_t *ps);


void session_run(session_t *ps);
