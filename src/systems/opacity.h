#pragma once

#include "session.h"
#include "swiss.h"

void opacity_collect_fades(Swiss* em, Vector* fadable);

void opacity_tick(Swiss* em, session_t* ps);
void opacity_afterFade(Swiss* em);
