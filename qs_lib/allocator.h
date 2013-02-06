#ifndef ALLOCATOR_H
#define ALLOCATOR_H

typedef struct _allocator {
	void * object;
	void *(*alloc)(void *object, size_t size);
	void (*free)(void *object, void *p);
} allocator;

#endif