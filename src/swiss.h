#ifndef swiss_H
#define swiss_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

// The "Swiss" (more commonly "Entity Manager") Is a fun datastructure that
// keeps track of entities and components, for the purpose of fast iteration.
// The main challenges are as follows:
// - Fast allocation of entities
// - Allow addition and removal of components to an existing entity
// - Efficient iteration through entities having some components.
// - Decent performance after removing entities in the middle of the id space
//
// My way of solving this is to create a datastructure that holds an arbitrary
// number of components arrays, together with a bitfield telling us of the
// entity has a component. This isn't memory efficient, but computationally
// efficient. You can think of it as a multi-vector, with a bitfield for every
// vector.
// The bitfield is an array of uint64_t's called "buckets" from which we can
// use the clzll instruction on x86 platforms to do a very fast lookup. For
// fast iteration we do bitwise AND and clz to find the next entity with the
// given components.
// To keep track of some metadata, the swiss has an internal component
// (COMPONENT_META).

typedef uint64_t win_id;

struct MetaComponent {
};

enum ComponentType {
    COMPONENT_META, // Special component used for bookkeeping
    COMPONENT_END = COMPONENT_META ,
    COMPONENT_MUD, // The goal is to eliminate this one
    COMPONENT_PHYSICAL,
    COMPONENT_Z,
    COMPONENT_BINDS_TEXTURE,
    COMPONENT_TEXTURED,
    COMPONENT_TRACKS_WINDOW,
    COMPONENT_HAS_CLIENT,
    COMPONENT_SHADOW,
    COMPONENT_BLUR,
    COMPONENT_TINT,
    COMPONENT_OPACITY,
    COMPONENT_FADES_OPACITY,

    // Messages
    COMPONENT_MAP,
    COMPONENT_UNMAP,
    COMPONENT_MOVE,
    COMPONENT_RESIZE,
    COMPONENT_BLUR_DAMAGED,
    COMPONENT_CONTENTS_DAMAGED,
    COMPONENT_SHADOW_DAMAGED,
    COMPONENT_FOCUS_CHANGE,
    COMPONENT_WINTYPE_CHANGE,

    NUM_COMPONENT_TYPES,

    CQ_NOT,
    CQ_END,
};

typedef enum ComponentType CType;

#define for_componentsArr(IT, EM, ARR)                         \
    for(                                                       \
        struct SwissIterator IT = swiss_getFirstInit(EM, ARR); \
        !IT.done;                                              \
        swiss_getNext(EM, &IT)                                 \
    )
#define for_components(IT, EM, ...)                                               \
    for(                                                                          \
        struct SwissIterator IT = swiss_getFirstInit(EM, (CType[]){__VA_ARGS__}); \
        !IT.done;                                                                 \
        swiss_getNext(EM, &IT)                                                    \
    )

typedef struct {
    size_t capacity;
    size_t size;

    size_t firstFree;

    size_t componentSize[NUM_COMPONENT_TYPES];
    uint64_t* freelist[NUM_COMPONENT_TYPES];
    uint8_t* data[NUM_COMPONENT_TYPES];
    bool safemode[NUM_COMPONENT_TYPES];
} Swiss;

void swiss_clearComponentSizes(Swiss* index);
void swiss_setComponentSize(Swiss* index, const enum ComponentType type, size_t size);
void swiss_enableAllAutoRemove(Swiss* index);
void swiss_disableAutoRemove(Swiss* index, const enum ComponentType type);
void swiss_init(Swiss* index, size_t initialSize);

void swiss_kill(Swiss* vector);

win_id swiss_allocate(Swiss* index);
void swiss_remove(Swiss* index, win_id id);

void* swiss_addComponent(Swiss* index, const enum ComponentType type, win_id id);
void swiss_ensureComponent(Swiss* index, const enum ComponentType type, win_id id);
bool swiss_hasComponent(const Swiss* vector, enum ComponentType type, win_id id);
void swiss_removeComponent(Swiss* index, const enum ComponentType type, win_id id);
void swiss_resetComponent(Swiss* index, const enum ComponentType type);

// Avoid using this
void* swiss_getComponent(const Swiss* index, const enum ComponentType type, win_id id);
void* swiss_godComponent(const Swiss* index, const enum ComponentType type, win_id id);

void swiss_clear(Swiss* vector);
int swiss_size(Swiss* vector);

size_t swiss_indexOfPointer(Swiss* vector, const enum ComponentType type, void* data);

void swiss_removeComponentWhere(Swiss* index, const enum ComponentType type, const enum ComponentType* keys);

struct SwissIterator {
    win_id id;
    bool done;
    const enum ComponentType* types;
};
struct SwissIterator swiss_getFirstInit(const Swiss* index, const enum ComponentType* types);
void swiss_getFirst(const Swiss* index, const enum ComponentType* types, struct SwissIterator* it);
void swiss_getNext(const Swiss* index, struct SwissIterator* it);

#endif
