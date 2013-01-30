#pragma once

#include <windows.h>

typedef struct _fast_buf {
	unsigned char *mem;
	unsigned char *head;
	size_t count;
	size_t size_of_element;
	CRITICAL_SECTION cs;
} fast_buf;

fast_buf* fast_buf_create(size_t size_of_element, size_t count_of_elements);
void* fast_buf_alloc(fast_buf *buf);
void fast_buf_free(fast_buf *buf, void* ptr);
void fast_buf_clear(fast_buf *buf);
void fast_buf_destroy(fast_buf *buf);
