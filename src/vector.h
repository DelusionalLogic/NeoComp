#ifndef VECTOR_H
#define VECTOR_H

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
} Vector;

void vector_init(Vector* vector, size_t elementsize, size_t initialsize);
void vector_kill(Vector* vector);
void* vector_detach(Vector* vector);

void vector_putBack(Vector* vector, const void* element, size_t* index);
void vector_putListBack(Vector* vector, const void* list, const size_t count);

size_t vector_indexOfPointer(Vector* vector, void* data);

void* vector_get(Vector* vector, const size_t count);

void vector_remove(Vector* vector, size_t count);
void vector_clear(Vector* vector);
void vector_qsort(Vector* vector, int (*compar)(const void *, const void*));

bool vector_foreach(Vector* vector, bool (*callback)(void* elem, void* userdata), void* userdata);

void* vector_getFirst(Vector* vector, size_t* index);
void* vector_getNext(Vector* vector, size_t* index);

int vector_size(Vector* vector);

#endif
