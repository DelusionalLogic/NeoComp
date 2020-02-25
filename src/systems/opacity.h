#pragma once

#include "session.h"
#include "swiss.h"

void calculate_window_opacity(session_t* ps, Swiss* em);
void opacity_collect_fades(Swiss* em, Vector* fadable);
void opacity_commit_fades(Swiss* em);
void commit_opacity_change(Swiss* em, double fade_time, double bg_fade_time);
void start_focus_fade(Swiss* em, double fade_time, double bg_fade_time, double dim_fade_time);
