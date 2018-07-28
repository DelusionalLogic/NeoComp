#include "swiss.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#define SWISS_FREELIST_ELEM_SIZE 8

static size_t freelist_getByteSize(size_t bits) {
    return bits / SWISS_FREELIST_ELEM_SIZE + (bits % SWISS_FREELIST_ELEM_SIZE != 0);
}

static void resize_real(Swiss* vector, size_t newSize) {
    assert(newSize != 0);

    void* newMem = realloc(vector->data, newSize * vector->elementSize);
    assert(newMem != NULL);
    vector->data = newMem;

    size_t freeSize = freelist_getByteSize(newSize);
    newMem = realloc(vector->freelist, freeSize);
    assert(newMem != NULL);
    vector->freelist = newMem;

    // Set all the newly allocated words to the right value
    size_t oldFreeSize = freelist_getByteSize(vector->maxSize);
    size_t newFreeBytes = freeSize - oldFreeSize;
    memset(vector->freelist + oldFreeSize, 0xFF, newFreeBytes);

    // The end of the freelist might lie within a word, but in that case the
    // memset above will already have marked them as free, so we don't have to
    // do it here. Assuming all other functions correctly check the size value
    // of course.

    vector->maxSize = newSize;
}

const uint8_t BITINDEX[] = {
    0x07, 0x02, 0x06, 0x01, 0x03, 0x04, 0x05, 0x00,
};
const uint8_t DEBRUIJN = 0b00011101;

static size_t findFirstSet(uint8_t value) {
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    return BITINDEX[(uint8_t)(value * DEBRUIJN) >> 5];
}

// The start is the first index we care about, but we don't do sub-byte
// positioning. If start is a mid-byte value, it will be rounded DOWN to the
// byte it intersects, and we will start the search from there. It's just an
// optimization after all.
static size_t findNextFree(Swiss* vector, size_t start) {
#if SWISS_FREELIST_ELEM_SIZE != 8
#error "findNextFree has to be made aware of the new size"
#endif
    size_t freeSize = freelist_getByteSize(vector->maxSize);

    for(int i = start / SWISS_FREELIST_ELEM_SIZE; i < freeSize; i++) {
        uint8_t freeByte = vector->freelist[i];

        if(freeByte == 0)
            continue;

        size_t freeIndex = findFirstSet(freeByte) + i * 8;
        if(freeIndex >= vector->maxSize)
            return -1;
        return freeIndex;
    }
    return -1;
}

static size_t findNextUsed(Swiss* vector, size_t start) {
#if SWISS_FREELIST_ELEM_SIZE != 8
#error "findNextUsed has to be made aware of the new size"
#endif
    size_t freeSize = freelist_getByteSize(vector->maxSize);
    size_t firstByte = start / SWISS_FREELIST_ELEM_SIZE;

    uint8_t mask = 0xFF00 >> (start % SWISS_FREELIST_ELEM_SIZE);
    uint8_t value = vector->freelist[firstByte] | mask;
    if(value != 0xFF) {
        size_t index = findFirstSet(~value) + firstByte * 8;
        if(index >= vector->maxSize)
            return -1;
        return index;
    }

    for(int i = firstByte + 1; i < freeSize; i++) {
        value = vector->freelist[i];

        if(value == 0xFF)
            continue;

        size_t freeIndex = findFirstSet(~value) + i * 8;
        if(freeIndex >= vector->maxSize)
            return -1;
        return freeIndex;
    }
    return -1;
}

static void setFreeStatus(Swiss* vector, size_t index, bool isFree) {
    size_t byte = index / SWISS_FREELIST_ELEM_SIZE;
    size_t offset = index % SWISS_FREELIST_ELEM_SIZE;

    if(!isFree)
        vector->freelist[byte] &= ~(0x80 >> offset);
    else
        vector->freelist[byte] |= (0x80 >> offset);
}

static bool getFreeStatus(const Swiss* vector, size_t index) {
    size_t byte = index / SWISS_FREELIST_ELEM_SIZE;
    size_t offset = index % SWISS_FREELIST_ELEM_SIZE;

    return vector->freelist[byte] & (0x80 >> offset);
}

static size_t allocateNextFree(Swiss* vector) {

    if(vector->firstFree == -1) {
        // Allocate space at the end of the array

        assert(vector->maxSize != 0);

        size_t newSize = vector->maxSize + 1;
        size_t maxSize = vector->maxSize;
        while(maxSize < newSize)
            maxSize *= 2;
        resize_real(vector, maxSize);

        vector->firstFree = findNextFree(vector, maxSize);
    }

    size_t free = vector->firstFree;

    setFreeStatus(vector, free, false);

    vector->firstFree = findNextFree(vector, free + 1);
    return free;
}

void swiss_init(Swiss* vector, size_t elementsize, size_t initialsize)
{
    vector->elementSize = elementsize;
    vector->maxSize = 0;
    vector->firstFree = 2;

    vector->data = NULL;
    vector->freelist = NULL;
    resize_real(vector, initialsize);

    setFreeStatus(vector, 0, false);
    setFreeStatus(vector, 1, false);

    assert(vector->data != NULL);
    assert(vector->freelist != NULL);
    assert(vector->maxSize != 0);
}

void swiss_kill(Swiss* vector)
{
    assert(vector->elementSize != 0);
    free(vector->data);
    free(vector->freelist);
    vector->data=(void*)0x72727272;
    vector->freelist=(void*)0x72727272;
}

void* swiss_detach(Swiss* vector)
{
    vector->maxSize = 0;
    vector->elementSize = 0;
    uint8_t* oldDat = vector->data;
    vector->data = NULL;
    free(vector->freelist);
    return oldDat;
}

void swiss_putBack(Swiss* vector, const void* element, size_t* index)
{
    assert(vector->elementSize != 0);

    if(index == NULL) {
        size_t placeholder = 0;
        index = &placeholder;
    }

    *index = allocateNextFree(vector);

    memcpy(vector->data + (*index) * vector->elementSize, element, vector->elementSize);
    vector->size++;
}

size_t swiss_indexOfPointer(Swiss* vector, void* data) {
    assert(data >= (void*)vector->data);
    assert(data <= (void*)(vector->data + vector->elementSize * vector->maxSize));

    return (data - (void*)vector->data) / vector->elementSize;
}

void* swiss_get(const Swiss* vector, const size_t count)
{
    assert(vector->elementSize != 0);
    assert(getFreeStatus(vector, count) == false);
    return vector->data + vector->elementSize * count;
}

void swiss_remove(Swiss* vector, const size_t index) {
    assert(vector->elementSize != 0);
    assert(getFreeStatus(vector, index) == false);

    setFreeStatus(vector, index, true);
    if(vector->firstFree > index)
        vector->firstFree = index;
    vector->size--;
}

void swiss_clear(Swiss* vector)
{
    assert(vector->elementSize != 0);
    vector->size = 0;

    size_t freeSize = freelist_getByteSize(vector->maxSize);
    memset(vector->freelist, 0xFF, freeSize);

    vector->firstFree = 0;
}

void* swiss_getFirst(Swiss* vector, size_t* index) {
    *index = 0;
    if(*index >= swiss_size(vector))
        return NULL;
    return swiss_get(vector, *index);
}

void* swiss_getNext(Swiss* vector, size_t* index) {
    *index = findNextUsed(vector, (*index)+1);
    if(*index == -1) {
        return NULL;
    }
    return swiss_get(vector, *index);
}

int swiss_size(Swiss* vector)
{
    assert(vector->elementSize != 0);
    return vector->size;
}
