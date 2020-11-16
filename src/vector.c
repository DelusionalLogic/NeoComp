#define _GNU_SOURCE
#include "vector.h"
#include <assert.h>
#include <string.h>
#include "logging.h"

static void resize(Vector* vector, size_t newElem)
{
    if(newElem + vector->size > vector->maxSize)
    {
        size_t newSize = newElem + vector->size;
        while(vector->maxSize < newSize) {
            vector->maxSize *= 2;
        }
        void* newMem = realloc(vector->data, vector->maxSize * vector->elementSize);
        assert(newMem != NULL);
        vector->data = newMem;
    }
}

void vector_init(Vector* vector, size_t elementsize, size_t initialsize)
{
    vector->maxSize = initialsize;
    vector->elementSize = elementsize;
    vector->size = 0;
    vector->data = malloc(initialsize * elementsize); //This should really not be done here
    assert(vector->data != NULL);
}

void vector_kill(Vector* vector)
{
    assert(vector->elementSize != 0);
    free(vector->data);
    vector->data = (void*)0x42424242;
    vector->elementSize = 0;
}

char* vector_detach(Vector* vector)
{
    vector->maxSize = 0;
    vector->elementSize = 0;
    vector->size = 0;
    char* oldDat = vector->data;
    vector->data = NULL;
    return oldDat;
}

void* vector_reserve(Vector* vector, size_t count) {
    assert(vector->elementSize != 0);

    size_t start = vector->size;

    resize(vector, count);
    vector->size += count;

    return vector->data + start * vector->elementSize;
}

void vector_putBack(Vector* vector, const void* element)
{
    assert(vector->elementSize != 0);

    void* dest = vector_reserve(vector, 1);
    memcpy(dest, element, vector->elementSize);
}

void vector_putListBack(Vector* vector, const void* list, const size_t count)
{
    assert(vector->elementSize != 0);

    void* dest = vector_reserve(vector, count);
    memcpy(dest, list, count * vector->elementSize);
}

void* vector_get(const Vector* vector, const size_t count)
{
    assert(vector->elementSize != 0);
    assert(count < vector->size);

    if(count >= vector->size) {
        return NULL;
    }

    return vector->data + vector->elementSize * count;
}

void vector_remove(Vector* vector, const size_t count)
{
    assert(vector->elementSize != 0);
    memmove(
        vector->data + count * vector->elementSize,
        vector->data + (count+1) * vector->elementSize,
        (vector->size-count-1) * vector->elementSize
    );
    vector->size -= 1;
}

void vector_clear(Vector* vector)
{
    assert(vector->elementSize != 0);
    vector->size = 0;
}

void vector_qsort(Vector* vector, int (*compar)(const void *, const void*, void*), void* userdata) {
    assert(vector->elementSize != 0);
    qsort_r(vector->data, vector->size, vector->elementSize, compar, userdata);
}

int vector_foreach(Vector* vector, int (*callback)(void* elem, void* userdata), void* userdata)
{
    assert(vector->elementSize != 0);
    size_t index = 0;
    void* elem  = vector_getFirst(vector, &index); //Should never error out
    while(elem != NULL) {
        int cont = callback(elem, userdata);
        if(cont != 0)
            return cont;
        elem = vector_getNext(vector, &index); //Should never error out
    }
    return true;
}

void* vector_getFirst(const Vector* vector, size_t* index) {
    *index = 0;
    if(*index >= vector_size(vector))
        return NULL;
    return vector_get(vector, *index);
}

void* vector_getNext(const Vector* vector, size_t* index) {
    ++(*index);
    if(*index >= vector_size(vector))
        return NULL;
    return vector_get(vector, *index);
}

void* vector_getLast(const Vector* vector, size_t* index) {
    *index = vector_size(vector);
    if(*index == 0)
        return NULL;
    (*index)--;
    return vector_get(vector, *index);
}

void* vector_getPrev(const Vector* vector, size_t* index) {
    if(*index == 0)
        return NULL;
    --(*index);
    return vector_get(vector, *index);
}

int vector_size(const Vector* vector)
{
    assert(vector->elementSize != 0);
    return vector->size;
}

size_t vector_bisect(Vector* vector, const void* needle, int (*compar)(const void *, const void*, void*), void* userdata) {
    assert(vector->elementSize != 0);

    if(vector->size == 0)
        return -1;

    int32_t bottom = 0;
    int32_t top = vector_size(vector) - 1;

    while(true) {
        int32_t pivot = (top - bottom) / 2 + bottom;
        int cmp = compar(needle, vector_get(vector, pivot), userdata);
        if(cmp == 0) {
            return pivot;
        } else if(cmp > 0) {
            bottom = pivot + 1;
        } else {
            top = pivot - 1;
        }

        if(top < bottom) {
            return -1;
        }
    }
}

size_t vector_find_uint64(Vector* vector, uint64_t value) {
    assert(vector->elementSize != 0);
    assert(vector->elementSize == sizeof(uint64_t));

    size_t index;
    uint64_t* elem = vector_getFirst(vector, &index);
    while(elem != NULL) {
        if(*elem == value)
            return index;
        elem = vector_getNext(vector, &index);
    }
    return -1;
}

void vector_circulate(Vector* vector, size_t old, size_t new) {
    assert(vector->elementSize != 0);
    assert(old != new);

    void* tmp = malloc(vector->elementSize);
    memcpy(tmp, vector_get(vector, old), vector->elementSize);

    if(old < new) {
        memmove(vector_get(vector, old), vector_get(vector, old+1), vector->elementSize * (new - old));
        memcpy(vector_get(vector, new), tmp, vector->elementSize);
    } else {
        memmove(vector_get(vector, new+1), vector_get(vector, new), vector->elementSize * (old - new));
        memcpy(vector_get(vector, new), tmp, vector->elementSize);
    }

    free(tmp);
}
