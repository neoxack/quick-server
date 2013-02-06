#include "fast_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define BYTE unsigned char
#define SPIN_COUNT 1024

fast_buf* fast_buf_create(size_t size_of_element, size_t count_of_elements)
{
	size_t chunkSize = sizeof(BYTE *)+ size_of_element;
	size_t mem_size;
	size_t i;
	BYTE* lastChunk;
	fast_buf* buffer = (fast_buf*)malloc(sizeof(fast_buf));
	if(!buffer) return NULL;
	mem_size = chunkSize * (count_of_elements+1);
	
	buffer->count = count_of_elements;
	buffer->size_of_element = size_of_element;

	buffer->mem = (BYTE *)malloc(mem_size);
	if(!buffer->mem)
	{
		free(buffer);
		return NULL;
	}
	memset(buffer->mem, 0, mem_size);

	for(i = 0; i<buffer->count; ++i)
	{		
		BYTE* currChunk = buffer->mem + (i * chunkSize);
		*((BYTE**)(currChunk + size_of_element)) = currChunk + chunkSize;
	}

	lastChunk = buffer->mem + (count_of_elements * chunkSize);
	*((BYTE**)(lastChunk + size_of_element)) = NULL; /* terminating NULL */

	buffer->head = buffer->mem;
	InitializeCriticalSectionAndSpinCount(&buffer->cs, SPIN_COUNT);
	return buffer;
}

void* fast_buf_alloc(fast_buf *buf)
{
	BYTE* currPtr;

	if(!buf)
		return NULL;
	EnterCriticalSection(&buf->cs);
	if(!(buf->head + buf->size_of_element))
		return NULL; /* out of memory */
	currPtr = buf->head;
	buf->head = *((BYTE**)(currPtr + buf->size_of_element));
	LeaveCriticalSection(&buf->cs);
	return currPtr;
}

void fast_buf_free(fast_buf *buf, void* ptr) 
{
	BYTE* currPtr;
	if(!buf || !ptr)
		return;

	currPtr = (BYTE*)ptr;

	EnterCriticalSection(&buf->cs);
	*((BYTE**)currPtr + buf->size_of_element) = buf->head;
	buf->head = currPtr;
	LeaveCriticalSection(&buf->cs);
	return;
}

void fast_buf_clear(fast_buf *buffer)
{
	size_t chunkSize = sizeof(BYTE *)+ buffer->size_of_element;
	size_t mem_size;
	size_t i;
	BYTE* lastChunk;
	mem_size = chunkSize * (buffer->count+1);
	
	memset(buffer->mem, 0, mem_size);

	for(i = 0; i<buffer->count; ++i)
	{		
		BYTE* currChunk = buffer->mem + (i * chunkSize);
		*((BYTE**)(currChunk + buffer->size_of_element)) = currChunk + chunkSize;
	}

	lastChunk = buffer->mem + (buffer->count * chunkSize);
	*((BYTE**)(lastChunk + buffer->size_of_element)) = NULL; 

	buffer->head = buffer->mem;
}

void fast_buf_destroy(fast_buf *buf)
{
	if(!buf) return;
	DeleteCriticalSection(&buf->cs);
	free(buf->mem);
	free(buf);	
}

