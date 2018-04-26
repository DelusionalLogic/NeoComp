#pragma once

#include <time.h>
#include <stdbool.h>

typedef struct timespec timestamp;

bool getTime(timestamp* stamp);

double timeDiff(timestamp* t1, timestamp* t2);
