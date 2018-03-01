#pragma once

#include <time.h>
#include <stdbool.h>

struct ProgramZone {
    int id;
    const char* name;

    bool bound;

    struct timespec startTime;
    struct timespec endTime;

    struct ProgramZone* parent;

    struct ProgramZone* child;
    struct ProgramZone** nextChild;

    struct ProgramZone* next;
};

#define DECLARE_ZONE(nme)                   \
    struct ProgramZone ZONE_##nme = { \
        .id = __COUNTER__,                  \
        .name = #nme,                       \
    };

void zone_enter(struct ProgramZone* zone);

void zone_leave(struct ProgramZone* zone);

struct ProgramZone* zone_package(struct ProgramZone* zone);
