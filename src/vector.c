#include "vector.h"
#include <assert.h>
#include <errno.h>
#include <string.h>

static void resize(Vector* vector, size_t newElem)
{
	if(newElem + vector->size > vector->maxSize)
	{
		size_t newSize = newElem + vector->size;
		while(vector->maxSize < newSize)
			vector->maxSize *= 2;
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
	vector->data=(void*)0x72727272;
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

void vector_putBack(Vector* vector, const void* element)
{
	assert(vector->elementSize != 0);

	resize(vector, 1);

	memcpy(vector->data + vector->size * vector->elementSize, element, vector->elementSize);
	vector->size += 1;
}

void vector_putListBack(Vector* vector, const void* list, const size_t count)
{
	assert(vector->elementSize != 0);

	resize(vector, count);

	memcpy(vector->data + vector->size * vector->elementSize, list, count * vector->elementSize);
	vector->size += count;
}

void* vector_get(Vector* vector, const size_t count)
{
	assert(vector->elementSize != 0);
	assert(count < vector->size);
	return vector->data + vector->elementSize * count;
}

void vector_remove(Vector* vector, const size_t count)
{
	assert(vector->elementSize != 0);
	memmove(vector->data + count * vector->elementSize, vector->data + (count+1) * vector->elementSize, (vector->size-1) * vector->elementSize);
	vector->size -= 1;
}

void vector_clear(Vector* vector)
{
	assert(vector->elementSize != 0);
	vector->size = 0;
}

void vector_qsort(Vector* vector, int (*compar)(const void *, const void*))
{
	assert(vector->elementSize != 0);
	qsort(vector->data, vector->size, vector->elementSize, compar);
}

void* vector_getFirst(Vector* vector, size_t* index) {
	*index = 0;
	if(*index >= vector_size(vector))
		return NULL;
	return vector_get(vector, *index);
}

void* vector_getNext(Vector* vector, size_t* index) {
	++(*index);
	if(*index >= vector_size(vector))
		return NULL;
	return vector_get(vector, *index);
}

int vector_size(Vector* vector)
{
	assert(vector->elementSize != 0);
	return vector->size;
}
