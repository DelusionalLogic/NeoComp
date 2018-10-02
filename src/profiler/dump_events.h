#pragma once

#include "zone.h"

#include "timer.h"

#include <stdio.h>
#include <stdbool.h>

struct ProfilerWriterSession {
    bool first;
    timestamp start;

    FILE* fd;
};

int profilerWriter_init(struct ProfilerWriterSession* session);
void profilerWriter_kill(struct ProfilerWriterSession* session);

void profilerWriter_emitFrame(struct ProfilerWriterSession* session, struct ZoneEventStream* stream);
