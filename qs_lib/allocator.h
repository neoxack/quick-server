#ifndef ALLOCATOR_H
#define ALLOCATOR_H

typedef struct _allocator {
	void * object;
	void *(*alloc)(size_t size);
	void (*free)(void *p);
} allocator;

#endif