#include "zone.h"

#include <stdio.h>
#include <assert.h>

struct ZoneEventStream stream;
size_t event_cursor;

static void zone_event(struct ProgramZone* zone, enum ZoneEventType type, struct ZoneEvent* event) {
    event->zone = zone;
    event->type = type;

    if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &event->time) != 0) {
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
    zone_event(zone, ZE_ENTER, event);
}

void zone_leave_raw(struct ProgramZone* zone, char* location) {
    struct ZoneEvent* event = reserve();
    event->location = location;
    zone_event(zone, ZE_LEAVE, event);
}

void zone_start(struct ProgramZone* zone) {
    if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &stream.start) != 0) {
        printf("Failed setting start for the stream %s\n", zone->name);
        return;
    }
    stream.rootZone = zone;
}

struct ZoneEventStream* zone_package(struct ProgramZone* zone) {
    stream.events_num = event_cursor;
    event_cursor = 0;

    if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &stream.end) != 0) {
        printf("Failed setting end for the stream %s\n", zone->name);
        return NULL;
    }

    return &stream;
}
