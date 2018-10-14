#include "malloc_profile.h"

#include <malloc.h>
#include <stdbool.h>
#include "profiler/zone.h"

extern void *__libc_malloc(size_t);

DECLARE_ZONE(malloc);

// Enable to trace malloc (real broken)
/*
void * malloc (size_t size) {
    zone_enter(&ZONE_malloc);
    void* addr = __libc_malloc(size);
    zone_leave(&ZONE_malloc);
    return addr;
}
*/
