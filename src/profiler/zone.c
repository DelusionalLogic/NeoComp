#include "zone.h"

#include <stdarg.h>
#include <stdio.h>
#include <assert.h>

struct ZoneEventStream stream;
size_t event_cursor;

#define CLOCK_TYPE CLOCK_MONOTONIC

static void zone_event(struct ProgramZone* zone, enum ZoneEventType type, struct ZoneEvent* event) {
    event->zone = zone;
    event->type = type;

    if(clock_gettime(CLOCK_TYPE, &event->time) != 0) {
        printf("Failed setting time for the event %s\n", zone->name);
        return;
    }
}

static struct ZoneEvent* reserve() {
    assert(event_cursor < ZONE_STREAM_LENGTH);
    return &stream.events[event_cursor++];
}

void zone_enter_raw(struct ProgramZone* zone, char* location) {
    struct ZoneEvent* event = reserve();
    event->location = location;
    event->userdata[0] = '\0';
    zone_event(zone, ZE_ENTER, event);
}

void zone_insta_raw(struct ProgramZone* zone, char* location) {
    struct ZoneEvent* event = reserve();
    event->location = location;
    event->userdata[0] = '\0';
    zone_event(zone, ZE_INSTA, event);
}

void zone_insta_extra_raw(struct ProgramZone* zone, char* location, char* format, ...) {
    va_list args;
    va_start(args, format);

    struct ZoneEvent* event = reserve();
    event->location = location;
    vsnprintf(event->userdata, 64, format, args);
    zone_event(zone, ZE_INSTA, event);

    va_end(args);
}

void zone_enter_extra_raw(struct ProgramZone* zone, char* location, char* format, ...) {
    va_list args;
    va_start(args, format);

    struct ZoneEvent* event = reserve();
    event->location = location;
    vsnprintf(event->userdata, 64, format, args);
    zone_event(zone, ZE_ENTER, event);

    va_end(args);
}

void zone_leave_raw(struct ProgramZone* zone, char* location) {
    struct ZoneEvent* event = reserve();
    event->location = location;
    event->userdata[0] = '\0';
    zone_event(zone, ZE_LEAVE, event);
}

void zone_leave_extra_raw(struct ProgramZone* zone, char* location, char* format, ...) {
    va_list args;
    va_start(args, format);

    struct ZoneEvent* event = reserve();
    event->location = location;
    vsnprintf(event->userdata, 64, format, args);
    zone_event(zone, ZE_LEAVE, event);

    va_end(args);
}

void zone_start(struct ProgramZone* zone) {
    if(clock_gettime(CLOCK_TYPE, &stream.start) != 0) {
        printf("Failed setting start for the stream %s\n", zone->name);
        return;
    }
    stream.rootZone = zone;
}

void zone_render() {
    if(clock_gettime(CLOCK_TYPE, &stream.render) != 0) {
        printf("Failed setting start for the stream %s\n", stream.rootZone->name);
        return;
    }
}

struct ZoneEventStream* zone_package(struct ProgramZone* zone) {
    stream.events_num = event_cursor;
    event_cursor = 0;

    if(clock_gettime(CLOCK_TYPE, &stream.end) != 0) {
        printf("Failed setting end for the stream %s\n", zone->name);
        return NULL;
    }

    return &stream;
}
