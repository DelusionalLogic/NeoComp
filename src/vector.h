#ifndef VECTOR_H
#define VECTOR_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    size_t maxSize;
    size_t size;
    size_t elementSize;
    char* data;
} Vector;

typedef int (*comparator)(const void* e1, const void* e2, const size_t size);

void vector_init(Vector* vector, size_t elementsize, size_t initialsize);
void vector_kill(Vector* vector);
char* vector_detach(Vector* vector);

// Allocate some space without actually writing anything
void* vector_reserve(Vector* vector, size_t count);

void vector_putBack(Vector* vector, const void* element);
void vector_putListBack(Vector* vector, const void* list, const size_t count);

void* vector_get(const Vector* vector, const size_t count);

void vector_remove(Vector* vector, size_t count);
void vector_clear(Vector* vector);
void vector_qsort(Vector* vector, int (*compar)(const void *, const void*, void*), void* userdata);

int vector_foreach(Vector* vector, int (*callback)(void* elem, void* userdata), void* userdata);

void* vector_getFirst(const Vector* vector, size_t* index);
void* vector_getNext(const Vector* vector, size_t* index);
void* vector_getLast(const Vector* vector, size_t* index);
void* vector_getPrev(const Vector* vector, size_t* index);

int vector_size(const Vector* vector);

size_t vector_bisect(Vector* vector, const void* needle, int (*compar)(const void *, const void*, void*), void* userdata);

// Only defined for vectors of uint64 elements
size_t vector_find_uint64(Vector* vector, uint64_t value);

void vector_circulate(Vector* vector, size_t old, size_t new);

#endif
