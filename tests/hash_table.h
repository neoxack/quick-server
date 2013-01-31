#pragma once

#include <stdlib.h>

#define KEY void*
#define VALUE void*

typedef struct _NODE {
	KEY key;
	VALUE val;
	struct _NODE *next;
} node;

typedef struct _HASH_TABLE {
	size_t size;       
	node **table; 
} hash_table;

hash_table *create_hash_table(size_t size);
void add_hash_table(hash_table *hashtable, KEY key, VALUE val);
VALUE find_hash_table(hash_table *hashtable, KEY key);
void delete_hash_table(hash_table *hashtable);
