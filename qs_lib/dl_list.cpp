/**
 * CS 2110 - Fall 2011 - Homework #11
 * Edited by: Tomer Elmalem
 *
 * list.c: Complete the functions!
 **/

#include <stdlib.h>
#include <stdio.h>
#include "dl_list.h"

/* The node struct.  Has a prev pointer, next pointer, and data. */
/* DO NOT DEFINE ANYTHING OTHER THAN WHAT'S GIVEN OR YOU WILL GET A ZERO*/
/* Design consideration only this file should know about nodes */
/* Only this file should be manipulating nodes */
typedef struct lnode
{
  struct lnode* prev; /* Pointer to previous node */
  struct lnode* next; /* Pointer to next node */
  void* data; /* User data */
} node;


/* Do not create any global variables here. Your linked list library should obviously work for multiple linked lists */
// This function is declared as static since you should only be calling this inside this file.
static node* create_node(list* llist, void* data);

/** create_list
  *
  * Creates a list by allocating memory for it on the heap.
  * Be sure to initialize size to zero and head to NULL.
  *
  * @return an empty linked list
  */
list* create_list(allocator alloc)
{
  list *l = (list *)malloc(sizeof(list));
  if(!l) return NULL;
  l->head = NULL;
  l->size = 0;
  l->allocator = alloc;
  return l;
}

/** create_node
  *
  * Helper function that creates a node by allocating memory for it on the heap.
  * Be sure to set its pointers to NULL.
  *
  * @param data a void pointer to data the user wants to store in the list
  * @return a node
  */
static node* create_node(list* llist, void* data)
{
  node *n = (node *)llist->allocator.alloc(sizeof(node));
  n->data = data;
  n->prev = NULL;
  n->next = NULL;
  return n;
}

/** push_front
  *
  * Adds the data to the front of the linked list
  *
  * @param llist a pointer to the list.
  * @param data pointer to data the user wants to store in the list.
  */
void push_front(list* llist, void* data)
{
  node *n = create_node(llist, data);

  // if the list if size 0
  if (!llist->size) {
    // then the next and prev nodes to itself
    n->next = n;
    n->prev = n;
  } else {
    node *head = llist->head;
    node *prev = head->prev;
    
    // set the new nodes next and prev pointers
    n->next = head;
    n->prev = head->prev;

    // set the prev and next pointers to the new node
    head->prev = n;
    prev->next = n;
  }

  // set the head of the list to the new node
  llist->head = n;
  llist->size++;
}

/** push_back
  *
  * Adds the data to the back/end of the linked list
  *
  * @param llist a pointer to the list.
  * @param data pointer to data the user wants to store in the list.
  */
void push_back(list* llist, void* data)
{
  node *n = create_node(llist, data);

  // if the list size is 0
  if (!llist->size) {
    // set the next and prev to the new node
    n->next = n;
    n->prev = n;

    // since there are no other nodes this node will be the head
    llist->head = n;
  } else {
    node *head = llist->head;
    node *prev = head->prev;

    // insert to the back by setting next to the head
    n->next = head;
    n->prev = head->prev;

    // Update the next and prev pointers to the current node
    head->prev = n;
    prev->next = n;
  }
  llist->size++;
}

/** remove_front
  *
  * Removes the node at the front of the linked list
  *
  * @warning Note the data the node is pointing to is also freed. If you have any pointers to this node's data it will be freed!
  *
  * @param llist a pointer to the list.
  * @param free_func pointer to a function that is responsible for freeing the node's data.
  * @return -1 if the remove failed (which is only there are no elements) 0 if the remove succeeded.
  */
int remove_front(list* llist)
{
  if (!llist->size) return -1;

  node *head = llist->head;
  
  if (llist->size == 1) {
    // if the size is 1 set the head to NULL since there are no other nodes
    llist->head = NULL;
  } else {
    node *next = head->next;
    node *prev = head->prev;

    // update the head
    llist->head = next;

    // update the pointers
    next->prev = prev;
    prev->next = next;
  }

  // free the data and the node
 // free_func(head->data);
  llist->allocator.free(head);

  llist->size--;
  
  return 0;
}

/** remove_index
  *
  * Removes the indexth node of the linked list
  *
  * @warning Note the data the node is pointing to is also freed. If you have any pointers to this node's data it will be freed!
  *
  * @param llist a pointer to the list.
  * @param index index of the node to remove.
  * @param free_func pointer to a function that is responsible for freeing the node's data.
  * @return -1 if the remove failed 0 if the remove succeeded.
  */
int remove_index(list* llist, size_t index)
{
  if (!llist->size) return -1;

  node *current = llist->head; // = index of 0

  // loop through until you get to where you want
  for (size_t i=0; i<index; i++) {
    current = current->next;
  }

  // if the size is 1
  if (llist->size == 1) {
    // make the head null
    llist->head = NULL;
  } else {
    node *next = current->next;
    node *prev = current->prev;
    
    // update the pointers to remove the node
    prev->next = next;
    next->prev = prev;
  }
  
  // Free the data and node
 // free_func(current->data);
  llist->allocator.free(current);
  
  llist->size--;

  return 0;
}

/** remove_back
  *
  * Removes the node at the back of the linked list
  *
  * @warning Note the data the node is pointing to is also freed. If you have any pointers to this node's data it will be freed!
  *
  * @param llist a pointer to the list.
  * @param free_func pointer to a function that is responsible for freeing the node's data.
  * @return -1 if the remove failed 0 if the remove succeeded.
  */
int remove_back(list* llist)
{
  if (!llist->size) return -1;

  node *head = llist->head;
  node *tbr = head->prev; // to be removed
  node *nb = tbr->prev; // new back

  // if list if of size 1
  if (llist->size == 1) {
    // make the head null
    llist->head = NULL;
  } else {
    // update the pointers to back is gone.
    head->prev = nb;
    nb->next = head;
  }

  // free the data and node
 // free_func(tbr->data);
  llist->allocator.free(tbr);
  
  llist->size--;

  return 0;
}

/** remove_data
  *
  * Removes ALL nodes whose data is EQUAL to the data you passed in or rather when the comparison function returns true (!0)
  * @warning Note the data the node is pointing to is also freed. If you have any pointers to this node's data it will be freed!
  *
  * @param llist a pointer to the list
  * @param data data to compare to.
  * @param compare_func a pointer to a function that when it returns true it will remove the element from the list and do nothing otherwise @see equal_op.
  * @param free_func a pointer to a function that is responsible for freeing the node's data
  * @return the number of nodes that were removed.
  */
size_t remove_data(list* llist, const void* data, equal_op compare_func)
{
  size_t removed = 0;

  if (!llist->size) return removed;

  node *current = llist->head;
  node *next = current->next;
  node *prev = current->prev;
  int is_head = 1;

  for (size_t i=0; i<llist->size; i++) {
    if (compare_func(data, current->data)) {
      // if we are still on the head update the current head
      if (is_head) llist->head = next;

      // update the pointers
      next->prev = prev;
      prev->next = next;

      // free the data and node
    //  free_func(current->data);
      llist->allocator.free(current);

      // the current is the next node
      current = next;

      removed++;
    } else {
      // no longer on the head
      is_head = 0;
      
      // move to the next node
      current = current->next;
    }

    // update the previous and next node
    if (llist->size > 1) {
      next = current->next;
      prev = current->prev;
    }
  }

  // update the size
  llist->size-=removed;

  // if the size is zero the list is empty and the head should be null
  if (!llist->size) llist->head = NULL;

  return removed;
}

size_t remove_data_once(list* llist, const void* data, equal_op compare_func)
{
	size_t removed = 0;

	if (!llist->size) return removed;

	node *current = llist->head;
	node *next = current->next;
	node *prev = current->prev;
	int is_head = 1;

	for (size_t i=0; i<llist->size && removed < 1; i++) {
		if (compare_func(data, current->data)) {
			// if we are still on the head update the current head
			if (is_head) llist->head = next;

			// update the pointers
			next->prev = prev;
			prev->next = next;

			// free the data and node
			//  free_func(current->data);
			llist->allocator.free(current);

			// the current is the next node
			current = next;

			removed++;
		} else {
			// no longer on the head
			is_head = 0;

			// move to the next node
			current = current->next;
		}

		// update the previous and next node
		if (llist->size > 1) {
			next = current->next;
			prev = current->prev;
		}
	}

	// update the size
	llist->size-=removed;

	// if the size is zero the list is empty and the head should be null
	if (!llist->size) llist->head = NULL;

	return removed;
}

/** remove_if
  *
  * Removes all nodes whose data when passed into the predicate function returns true
  *
  * @param llist a pointer to the list
  * @param pred_func a pointer to a function that when it returns true it will remove the element from the list and do nothing otherwise @see list_pred.
  * @param free_func a pointer to a function that is responsible for freeing the node's data
  * @return the number of nodes that were removed.
  */
size_t remove_if(list* llist, list_pred pred_func)
{
  if (!llist->size) return 0;

  size_t removed = 0;
  node *current = llist->head;
  node *next = current->next;
  node *prev = current->prev;
  int is_head = 1;

  for (size_t i=0; i<llist->size; i++) {
    if (pred_func(current->data)) {
      // if we're still on the head update the list's head
      if (is_head) llist->head = next;

      // update the pointers
      next->prev = prev;
      prev->next = next;

      // free the data and node
   //   free_func(current->data);
      llist->allocator.free(current);

      // update the current
      current = next;

      removed++;
    } else {
      // we're no longer on the head
      is_head = 0;

      // update the current
      current = current->next;
    }

    // update the next and prev nodes
    if (llist->size > 0) {
      next = current->next;
      prev = current->prev;
    }
  }

  // update the list size
  llist->size-=removed;

  // if the list is empty make the head null
  if (!llist->size) llist->head = NULL;

  return removed;
}

/** front
  *
  * Gets the data at the front of the linked list
  * If the list is empty return NULL.
  *
  * @param llist a pointer to the list
  * @return The data at the first node in the linked list or NULL.
  */
void* front(list* llist)
{
  if (llist->size) {
    // if the list has a size return the data
    return llist->head->data;
  } else {
    // or return NULL
    return NULL;
  }
}

/** get_index
  *
  * Gets the data at the indexth node of the linked list
  * If the list is empty or if the index is invalid return NULL.
  *
  * @param llist a pointer to the list
  * @return The data at the indexth node in the linked list or NULL.
  */
void* get_index(list* llist, size_t index)
{
  // if the list is 0 or if the index in larger than the size
  if (!llist->size || index >= llist->size) {
    return NULL;
  }
  
  // loop through the list until you get to the desired index
  node *current = llist->head; // index = 0
  for (size_t i=0; i<index; i++) {
    current = current->next;
  }

  return current->data;
}

/** back
  *
  * Gets the data at the "end" of the linked list
  * If the list is empty return NULL.
  *
  * @param llist a pointer to the list
  * @return The data at the last node in the linked list or NULL.
  */
void* back(list* llist)
{
  // if the list is empty return null
  if (!llist->size) return NULL;

  // return the previous of the head
  node *end = llist->head->prev;
  return end->data;
}

/** is_empty
  *
  * Checks to see if the list is empty.
  *
  * @param llist a pointer to the list
  * @return 1 if the list is indeed empty 0 otherwise.
  */
int is_empty(list* llist)
{
  if (llist->size == 0 && llist->head == NULL) {
    return 1;
  } else {
    return 0;
  }
}

/** size
  *
  * Gets the size of the linked list
  *
  * @param llist a pointer to the list
  * @return The size of the linked list
  */
size_t size(list* llist)
{
  return llist->size;
}

/** find_occurence
  *
  * Tests if the search data passed in is in the linked list.
  *
  * @param llist a pointer to a linked list.
  * @param search data to search for the occurence.
  * @param compare_func a pointer to a function that returns true if two data items are equal @see equal_op.
  * @return 1 if the data is indeed in the linked list 0 otherwise.
  */
int find_occurrence(list* llist, const void* search, equal_op compare_func)
{
  // loop through the list
  node *current = llist->head;
  for (size_t i=0; i<llist->size; i++) {
    // if the compare func is 1, occurrence found return 1
    if (compare_func(search, current->data)) return 1;
    current = current->next;
  }

  // no occurrence founds
  return 0;
}

/** empty_list
  *
  * Empties the list after this is called the list should be empty.
  *
  * @param llist a pointer to a linked list.
  * @param free_func function used to free the node's data.
  *
  */
void empty_list(list* llist)
{
  // if the size is 0 return
  if (!llist->size) return;

  node *current = llist->head;
  node *next = current->next;

  // loop through the list and free all the nodes
  for (size_t i=0; i<llist->size; i++) {
  //  free_func(current->data);
    llist->allocator.free(current);
    current = next;
    
    // if it's not the end of the list set the next node
    if (i < llist->size-1) next = current->next;
  }

  // reset the head and size
  llist->head=NULL;
  llist->size=0;
  free(llist);
}

/** traverse
  *
  * Traverses the linked list calling a function on each node's data.
  *
  * @param llist a pointer to a linked list.
  * @param do_func a function that does something to each node's data.
  */
void traverse(list* llist, list_op do_func, void *user_data)
{
  node *current = llist->head;
  for (size_t i=0; i<llist->size; i++) {
    do_func(current->data, user_data);
    current = current->next;
  }
}