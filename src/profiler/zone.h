#pragma once

#include <time.h>
#include <stdbool.h>

#define ZONE_STREAM_LENGTH 1024000

struct ProgramZone {
    int id;
    const char* name;
};

enum ZoneEventType {
    ZE_ENTER,
    ZE_LEAVE,
};

struct ZoneEvent {
    struct ProgramZone* zone;

    struct timespec time;
    enum ZoneEventType type;

    char* location;
    char userdata[64];
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

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

#define zone_enter(zone) zone_enter_raw(zone, __FILE__ ": " STRINGIFY(__LINE__))
#define zone_enter_extra(zone, format, ...) zone_enter_extra_raw(zone, __FILE__ ": " STRINGIFY(__LINE__), format, __VA_ARGS__)
#define zone_leave(zone) zone_leave_raw(zone, __FILE__ ": " STRINGIFY(__LINE__))

#define zone_scope(zone) \
    zone_enter(zone); \
    defer { zone_leave(zone); }

#define defer_(x) do{}while(0); \
    auto void _dtor1_##x(); \
    auto void _dtor2_##x(); \
    int __attribute__((cleanup(_dtor2_##x))) _dtorV_##x=69; \
    void _dtor2_##x(){if(_dtorV_##x==42)return _dtor1_##x();};_dtorV_##x=42; \
    void _dtor1_##x()
#define defer__(x) defer_(x)
#define defer defer__(__COUNTER__)

void zone_enter_raw(struct ProgramZone* zone, char* location);
void zone_enter_extra_raw(struct ProgramZone* zone, char* location, char* format, ...);
void zone_leave_raw(struct ProgramZone* zone, char* location);

void zone_start(struct ProgramZone* zone);
struct ZoneEventStream* zone_package(struct ProgramZone* zone);
