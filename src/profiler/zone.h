#pragma once

#include <time.h>
#include <stdbool.h>

struct ProgramZone {
    int id;
    const char* name;
};

enum ZoneEventType {
    ZE_ENTER,
    ZE_LEAVE,
    ZE_END,
};

struct ZoneEvent {
    struct ProgramZone* zone;

    struct timespec time;
    enum ZoneEventType type;
};

#define DECLARE_ZONE(nme)                   \
    struct ProgramZone ZONE_##nme = { \
        .id = __COUNTER__,                  \
        .name = #nme,                       \
    };

void zone_enter(struct ProgramZone* zone);

void zone_leave(struct ProgramZone* zone);

struct ZoneEvent* zone_package(struct ProgramZone* zone);
