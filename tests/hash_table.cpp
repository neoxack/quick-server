#include "hash_table.h"

hash_table *create_hash_table(size_t size)
{
	hash_table *new_table;
	size_t i;

	if (size<1) return NULL; /* invalid size for table */
	if ((new_table = (hash_table *)malloc(sizeof(hash_table))) == NULL) 
	{
		return NULL;
	}

	/* Attempt to allocate memory for the table itself */
	if ((new_table->table = (node **)malloc(sizeof(node *) * size)) == NULL)
	{
		free(new_table);
		return NULL;
	}

	/* Initialize the elements of the table */
	for(i=0; i<size; ++i) 
		new_table->table[i] = NULL;

	new_table->size = size;
	return new_table;
}

static size_t hash(const void* pointer)
{
	intptr_t key = (intptr_t)pointer;
	key += ~(key << 16);
	key ^=  (key >>  5);
	key +=  (key <<  3);
	key ^=  (key >> 13);
	key += ~(key <<  9);
	key ^=  (key >> 17);
	return key;
}

void add_hash_table(hash_table *hashtable, KEY key, VALUE val)
{
	node *new_node = (node *)malloc(sizeof(node));
	if(new_node != NULL)
	{
		size_t index = hash(key) & (hashtable->size - 1);

		node **table = hashtable->table;
		new_node->key = key;
		new_node->val = val;
		new_node->next = table[index];
		table[index] = new_node;
	}
}

VALUE find_hash_table(hash_table *hashtable, KEY key)
{
	size_t index = hash(key) & (hashtable->size - 1);

	node **table = hashtable->table;

	if(table[index]!=NULL)
	{
		node *entry = table[index];
		while(entry!=NULL && entry->key != key) 
		{
			entry = entry->next;				
		}
		if(entry != NULL)
			return entry->val;
		return NULL;
	}
	return NULL;
}

static void clear(hash_table *hashtable)
{
	size_t i;
	node **table = hashtable->table;
	for (i = 0; i < hashtable->size; i++)
	{
		node *entry = table[i];
		if (entry != NULL) 
		{
			node *prevEntry = NULL;
			while (entry != NULL) 
			{
				prevEntry = entry;
				entry = entry->next;
				free(prevEntry);
			}
		}
	}
}

void delete_hash_table(hash_table *hashtable) 
{
	clear(hashtable);
	free(hashtable->table);
	free(hashtable);
}