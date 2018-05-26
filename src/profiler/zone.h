#pragma once

#include <time.h>
#include <stdbool.h>

#define ZONE_STREAM_LENGTH 1024

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

struct ZoneEventStream {
    struct ProgramZone* rootZone;

    struct timespec start;
    struct timespec end;

    size_t events_num;
    struct ZoneEvent events[ZONE_STREAM_LENGTH];
};

#define DECLARE_ZONE(nme)                   \
    struct ProgramZone ZONE_##nme = { \
        .id = __COUNTER__,                  \
        .name = #nme,                       \
    }

void zone_enter(struct ProgramZone* zone);

void zone_leave(struct ProgramZone* zone);

void zone_start(struct ProgramZone* zone);
struct ZoneEventStream* zone_package(struct ProgramZone* zone);
