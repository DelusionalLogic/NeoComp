#include "swiss.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "logging.h"

#define SWISS_FREELIST_BUCKET_SIZE (64)
#define SWISS_FREELIST_BUCKET_SIZE_BYTES (64/8)

static size_t freelist_numBuckets(size_t elements) {
    return elements / SWISS_FREELIST_BUCKET_SIZE + (elements % SWISS_FREELIST_BUCKET_SIZE != 0);
}

static void resize_real(Swiss* vector, size_t newSize) {
    assert(newSize != 0);

    size_t newBucketCount = freelist_numBuckets(newSize);
    size_t oldBucketCount = freelist_numBuckets(vector->capacity);
    size_t newBuckets = newBucketCount - oldBucketCount;

    for(int i = 0; i < NUM_COMPONENT_TYPES; i++) {
        void* newMem = NULL;
        // If a component has no size (which is valid) the memory required for
        // the array is 0.
        size_t cSize = vector->componentSize[i];
        if(cSize != 0) {
            newMem = realloc(vector->data[i], newSize * cSize);
            assert(newMem != NULL);
            memset(newMem + (vector->capacity * cSize), 0x00, (newSize - vector->capacity) * cSize);
        }
        vector->data[i] = newMem;

        newMem = realloc(vector->freelist[i], newBucketCount * SWISS_FREELIST_BUCKET_SIZE_BYTES);
        assert(newMem != NULL);
        vector->freelist[i] = newMem;

        // Set all the newly allocated words to the right value
        memset(&vector->freelist[i][oldBucketCount], 0x00, newBuckets * SWISS_FREELIST_BUCKET_SIZE_BYTES);
    }

    // The end of the freelist might lie within a word, but in that case the
    // memset above will already have marked them as free, so we don't have to
    // do it here. Assuming all other functions correctly check the size value
    // of course.

    vector->capacity = newSize;
}

static int findFirstSet(uint64_t value) {
    return __builtin_clzll(value);
}

static uint64_t makeBucket(const Swiss* index, const enum ComponentType* types, const size_t bucket) {
    uint64_t finalKey = index->freelist[COMPONENT_META][bucket];

    bool flip = false;
    for(int i = 0; types[i] != CQ_END; i++) {
        if(types[i] == CQ_NOT) {
            flip = true;
            continue;
        }
        uint64_t freelist = index->freelist[types[i]][bucket];

        uint64_t value = flip ? ~freelist : freelist;
        finalKey &= value;
        flip = false;
    }

    return finalKey;
}

// The start is the first index we care about, but we don't do sub-byte
// positioning. If start is a mid-byte value, it will be rounded DOWN to the
// byte it intersects, and we will start the search from there. It's just an
// optimization after all.
static size_t findNextFree(const Swiss* vector, const enum ComponentType type, const size_t start) {
#if SWISS_FREELIST_BUCKET_SIZE != 64
#error "findNextFree has to be made aware of the new size"
#endif
    size_t numBuckets = freelist_numBuckets(vector->capacity);
    uint64_t* freelist = vector->freelist[type];

    for(int i = start / SWISS_FREELIST_BUCKET_SIZE; i < numBuckets; i++) {
        uint64_t freeByte = freelist[i];

        if(freeByte == (~0ULL))
            continue;

        int freeIndex = findFirstSet(~freeByte) + i * SWISS_FREELIST_BUCKET_SIZE;
        if(freeIndex >= vector->capacity)
            return -1;
        return freeIndex;
    }
    return -1;
}

#ifndef NDEBUG // This is currently used for asserts
// This takes an array of types to support query like things
static size_t findNextUsed(const Swiss* vector, const enum ComponentType* types, const size_t start) {
#if SWISS_FREELIST_BUCKET_SIZE != 64
#error "findNextUsed has to be made aware of the new size"
#endif
    size_t firstBucket = start / SWISS_FREELIST_BUCKET_SIZE;

    uint64_t value = makeBucket(vector, types, firstBucket);

    uint64_t mask = (~0ULL) >> (start % SWISS_FREELIST_BUCKET_SIZE);
    value &= mask;
    if(value != 0) {
        size_t index = findFirstSet(value) + firstBucket * SWISS_FREELIST_BUCKET_SIZE;
        if(index >= vector->capacity)
            return -1;
        return index;
    }

    size_t numBuckets = freelist_numBuckets(vector->capacity);
    for(int i = firstBucket + 1; i < numBuckets; i++) {
        value = makeBucket(vector, types, i);

        if(value == 0)
            continue;

        size_t freeIndex = findFirstSet(value) + i * SWISS_FREELIST_BUCKET_SIZE;
        if(freeIndex >= vector->capacity)
            return -1;
        return freeIndex;
    }
    return -1;
}
#endif

static void setFreeStatus(Swiss* vector, enum ComponentType type, size_t index, bool isFree) {
    size_t bucket = index / SWISS_FREELIST_BUCKET_SIZE;
    size_t offset = index % SWISS_FREELIST_BUCKET_SIZE;
    uint64_t* freelist = vector->freelist[type];

    if(isFree) {
        freelist[bucket] &= ~(0x1ULL << ((SWISS_FREELIST_BUCKET_SIZE - offset) - 1));
    } else {
        freelist[bucket] |= (0x1ULL << ((SWISS_FREELIST_BUCKET_SIZE - offset) - 1));
    }
}

void swiss_clearComponentSizes(Swiss* index) {
    for(int i = 0; i < NUM_COMPONENT_TYPES; i++) {
        index->componentSize[i] = 0;
    }
}

void swiss_setComponentSize(Swiss* index, const enum ComponentType type, size_t size) {
    index->componentSize[type] = size;
}

void swiss_enableAllAutoRemove(Swiss* index) {
    memset(index->safemode, 0x00, sizeof(bool) * NUM_COMPONENT_TYPES);
}

void swiss_disableAutoRemove(Swiss* index, const enum ComponentType type) {
    index->safemode[type] = true;
}

void swiss_init(Swiss* index, size_t initialsize) {
    swiss_setComponentSize(index, COMPONENT_META, sizeof(struct MetaComponent));

    index->size = 0;
    index->capacity = 0;
    index->firstFree = 0;

    memset(index->data, 0x00, sizeof(uint8_t*) * NUM_COMPONENT_TYPES);
    memset(index->freelist, 0x00, sizeof(uint64_t*) * NUM_COMPONENT_TYPES);

    resize_real(index, initialsize);

    assert(index->data != NULL);
    assert(index->freelist != NULL);
    assert(index->capacity != 0);
}

void swiss_kill(Swiss* index) {
    assert(index->capacity != 0);

#ifndef NDEBUG
    // Safety check for debug builds. Check if we are leaking resources
    enum ComponentType type[2] = {
        CQ_END,
        CQ_END,
    };
    for(int i = 0; i < NUM_COMPONENT_TYPES; i++) {
        if(index->safemode[i]) {
            type[0] = i;
            assert(findNextUsed(index, type, 0) == -1);
        }
    }
#endif

    for(int i = 0; i < NUM_COMPONENT_TYPES; i++) {
        // Calling free on a NULL ptr isn't a problem, so there's no reason to
        // check
        free(index->data[i]);
        index->data[i] = NULL;

        free(index->freelist[i]);
        index->freelist[i] = NULL;

        index->componentSize[i] = 0;
    }
    index->capacity = 0;
}

win_id swiss_allocate(Swiss* index) {
    assert(index->capacity != 0);

    if(index->firstFree == -1) {
        // Allocate space at the end of the array
        assert(index->capacity != 0);

        size_t newSize = index->capacity * 2;
        resize_real(index, newSize);

        index->firstFree = findNextFree(index, COMPONENT_META, newSize);
    }

    win_id id = index->firstFree;

    swiss_addComponent(index, COMPONENT_META, id);

    index->firstFree = findNextFree(index, COMPONENT_META, id + 1);
    index->size++;
    return id;
}

void swiss_remove(Swiss* index, win_id id) {
    assert(index->capacity != 0);
    assert(swiss_hasComponent(index, COMPONENT_META, id) == true);

    for(int i = 0; i < NUM_COMPONENT_TYPES; i++) {
        if(swiss_hasComponent(index, i, id)) {
            assert(!index->safemode[i]);
            swiss_removeComponent(index, i, id);
        }
    }

    if(index->firstFree > id)
        index->firstFree = id;

    index->size--;
}

void* swiss_addComponent(Swiss* index, const enum ComponentType type, win_id id) {
    assert(index->capacity != 0);
    if(type != COMPONENT_META)
        assert(swiss_hasComponent(index, COMPONENT_META, id) == true);
    assert(swiss_hasComponent(index, type, id) == false);

    setFreeStatus(index, type, id, false);

    return index->data[type] + index->componentSize[type] * id;
}

void swiss_ensureComponent(Swiss* index, const enum ComponentType type, win_id id) {
    assert(index->capacity != 0);
    assert(swiss_hasComponent(index, COMPONENT_META, id) == true);

    setFreeStatus(index, type, id, false);
}

bool swiss_hasComponent(const Swiss* index, enum ComponentType type, win_id id) {
    assert(index->capacity != 0);

    size_t bucket = id / SWISS_FREELIST_BUCKET_SIZE;
    size_t offset = id % SWISS_FREELIST_BUCKET_SIZE;
    uint64_t* freelist = index->freelist[type];

    uint64_t bit = freelist[bucket] & (1ULL << ((SWISS_FREELIST_BUCKET_SIZE - offset) - 1));

    return bit != 0;
}

void swiss_removeComponent(Swiss* index, const enum ComponentType type, win_id id) {
    assert(index->capacity != 0);

    setFreeStatus(index, type, id, true);
}

void swiss_resetComponent(Swiss* index, const enum ComponentType type) {
    assert(index->capacity != 0);

    size_t freeSize = freelist_numBuckets(index->capacity);
    memset(index->freelist[type], 0, freeSize * SWISS_FREELIST_BUCKET_SIZE_BYTES);
}

void* swiss_getComponent(const Swiss* index, const enum ComponentType type, win_id id) {
    assert(index->capacity != 0);
    assert(swiss_hasComponent(index, COMPONENT_META, id) == true);
    assert(swiss_hasComponent(index, type, id) == true);
    assert(index->componentSize[type] != 0);

    return index->data[type] + index->componentSize[type] * id;
}

void* swiss_godComponent(const Swiss* index, const enum ComponentType type, win_id id) {
    assert(index->capacity != 0);

    if(!swiss_hasComponent(index, type, id))
        return NULL;

    return index->data[type] + index->componentSize[type] * id;
}

void swiss_clear(Swiss* index) {
    assert(index->capacity != 0);
    for(int i = 0; i < NUM_COMPONENT_TYPES; i++) {
        size_t freeSize = freelist_numBuckets(index->capacity);
        memset(index->freelist[i], 0x00, freeSize * SWISS_FREELIST_BUCKET_SIZE_BYTES);
    }

    index->size = 0;
    index->firstFree = 0;
}

int swiss_size(Swiss* index) {
    assert(index->capacity != 0);
    return index->size;
}

int swiss_count_holes(Swiss* vector) {
    assert(vector->capacity != 0);

    size_t holes = vector->capacity;
    size_t highwater = 0;
    for(int i = 0; i < freelist_numBuckets(vector->capacity); i++) {
        uint64_t value = makeBucket(vector, (CType[]){COMPONENT_META, CQ_END}, i);

        size_t localHighwater = SWISS_FREELIST_BUCKET_SIZE - __builtin_ctzll(value);
        if(localHighwater > 0) {
            // There's at least one set in this bucket. Calculate the new
            // highwater mark
            highwater = i * SWISS_FREELIST_BUCKET_SIZE + localHighwater;
        }

        holes -= __builtin_popcountll(value);
    }

    // Everything between the highwater mark and the capacity is not fragmentation
    return holes - (vector->capacity - highwater);
}

size_t swiss_indexOfPointer(Swiss* vector, enum ComponentType type, void* data) {
    assert(data >= (void*)vector->data[type]);
    assert(data <= (void*)(vector->data[type] + vector->componentSize[type] * vector->capacity));

    return (data - (void*)vector->data[type]) / vector->componentSize[type];
}

void swiss_setComponentWhere(Swiss* index, const enum ComponentType type, const enum ComponentType* keys) {
    size_t numBuckets = freelist_numBuckets(index->capacity);
    for(size_t i = 0; i < numBuckets; i++) {
        uint64_t key = makeBucket(index, keys, i);

        if(key == 0)
            continue;

        index->freelist[type][i] = key;
    }
}

void swiss_removeComponentWhere(Swiss* index, const enum ComponentType type, const enum ComponentType* keys) {
    size_t numBuckets = freelist_numBuckets(index->capacity);
    for(size_t i = 0; i < numBuckets; i++) {
        uint64_t key = makeBucket(index, keys, i);

        if(key == 0)
            continue;

        index->freelist[type][i] &= ~key;
    }
}

void swiss_ensureComponentWhere(Swiss* index, const enum ComponentType type, const enum ComponentType* keys) {
    size_t numBuckets = freelist_numBuckets(index->capacity);
    for(size_t i = 0; i < numBuckets; i++) {
        uint64_t key = makeBucket(index, keys, i);

        if(key == 0)
            continue;

        index->freelist[type][i] |= key;
    }
}

size_t swiss_countWhere(Swiss* index, const enum ComponentType* keys) {
    size_t count = 0;

    size_t numBuckets = freelist_numBuckets(index->capacity);
    for(size_t i = 0; i < numBuckets; i++) {
        uint64_t key = makeBucket(index, keys, i);

        if(key == 0)
            continue;

        count += __builtin_popcountll(key);
    }

    return count;
}

struct SwissIterator swiss_getFirstInit(const Swiss* index, const enum ComponentType* types) {
    struct SwissIterator it;
    swiss_getFirst(index, types, &it);
    return it;
}

void swiss_getFirst(const Swiss* index, const enum ComponentType* types, struct SwissIterator* it) {
    it->types = types;

    it->id = 0;

    size_t numBuckets = freelist_numBuckets(index->capacity);
    if(numBuckets == 0) {
        it->id = -1;
        it->done = true;
        return;
    }

    size_t ibucket = 0;

    it->bucket = makeBucket(index, types, ibucket);
    while(it->bucket == 0) {
        ibucket++;
        if(ibucket >= numBuckets) {
            it->id = -1;
            it->done = true;
            return;
        }

        it->bucket = makeBucket(index, types, ibucket);
    }

    size_t ind = findFirstSet(it->bucket);
    if(ind >= index->capacity) {
        it->id = -1;
        it->done = true;
        return;
    }

    // Clear the bit we are returning now
    it->bucket ^= (1ULL) << (63 - ind);
    it->id = ibucket * SWISS_FREELIST_BUCKET_SIZE + ind;
    it->done = false;
}

void swiss_getNext(const Swiss* index, struct SwissIterator* it) {
    if(it->done) {
        return;
    }

    size_t numBuckets = freelist_numBuckets(index->capacity);
    if(numBuckets == 0) {
        it->id = -1;
        it->done = true;
        return;
    }

    size_t ibucket = it->id / SWISS_FREELIST_BUCKET_SIZE;

    while(it->bucket == 0) {
        ibucket++;
        if(ibucket >= numBuckets) {
            it->id = -1;
            it->done = true;
            return;
        }

        it->bucket = makeBucket(index, it->types, ibucket);
    }

    size_t ind = findFirstSet(it->bucket);
    if(ind >= index->capacity) {
        it->id = -1;
        it->done = true;
        return;
    }

    it->bucket ^= (1ULL) << (63 - ind);
    it->id = ibucket * SWISS_FREELIST_BUCKET_SIZE + ind;
    it->done = false;
}
