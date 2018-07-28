#ifndef swiss_H
#define swiss_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    size_t maxSize;
    size_t size;
    size_t elementSize;

    size_t firstFree;

    uint8_t* freelist;
    uint8_t* data;
} Swiss;

void swiss_init(Swiss* vector, size_t elementsize, size_t initialsize);
void swiss_kill(Swiss* vector);
void* swiss_detach(Swiss* vector);

void swiss_putBack(Swiss* vector, const void* element, size_t* index);
void swiss_putListBack(Swiss* vector, const void* list, const size_t count);

size_t swiss_indexOfPointer(Swiss* vector, void* data);

void* swiss_get(const Swiss* vector, const size_t count);

void swiss_remove(Swiss* vector, size_t count);
void swiss_clear(Swiss* vector);
void swiss_qsort(Swiss* vector, int (*compar)(const void *, const void*));

bool swiss_foreach(Swiss* vector, bool (*callback)(void* elem, void* userdata), void* userdata);

void* swiss_getFirst(Swiss* vector, size_t* index);
void* swiss_getNext(Swiss* vector, size_t* index);

int swiss_size(Swiss* vector);

#endif
