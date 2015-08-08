
/*
 * The olsr.org Optimized Link-State Routing daemon version 2 (olsrd2)
 * Copyright (c) 2004-2015, the olsr.org team - see HISTORY file
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "heap.h"

static unsigned int heap_perfect_log2 (unsigned int number);
static struct heap_node* heap_find_parent_insert_node(struct bin_heap *heap);
static void heap_swap_left(struct bin_heap *heap, struct heap_node *node);
static void heap_swap_right(struct bin_heap *heap, struct heap_node *node);
static void heap_increse_key(struct bin_heap *heap, struct heap_node *node);
static struct heap_node* heap_find_last_node(struct bin_heap *heap);

/**
 * Initialize a new binary heap struct
 * @param heap pointer to binary heap control structure
 */
void
heap_init(struct bin_heap *heap)
{
  heap->count = 0;
  heap->root_node = heap->last_node = NULL;
}

/**
 * Initialize a heap node
 * @param node pointer to the heap node
 */
void
heap_init_node(struct heap_node *node)
{
  node->parent = node->left = node->right = NULL;
}

/**
 * test if the last binary heap's level is full
 * @param number of elements in the heap
 * @return the difference between the binary heap's size and
 * a full binary heap with the same height.
 */
static unsigned int
heap_perfect_log2 (unsigned int number)
{
  int log = 0, original_number=number;
  while (number >>= 1) ++log;
  return original_number - (1 << log);
}

/**
 * finds the parent node of the new node that will be inserted
 * in the binary heap
 * @param heap pointer to binary heap control structure
 * @return the pointer to parent node
 */

static struct heap_node*
heap_find_parent_insert_node(struct bin_heap *heap)
{
  struct heap_node *aux = heap->last_node;
  unsigned int N = heap->count+1;
  if ( !heap_perfect_log2(N)) {
    /* if the heap is full a new level must be created */
    aux = heap->root_node;
    while (aux->left) {
      aux = aux->left;
    }
  }
  else if ( N % 2 == 0) {
    /*
    * if the heap isn't full, find the next empty child pointer
    * in the last level
    */
    while (aux->parent->right == aux){
      aux = aux->parent;
    }
    if (!aux->parent->right) {
      return aux->parent;
    }
    aux = aux->parent->right;
    while (aux->left) {
      aux = aux->left;
    }
  }
  else{
    /* the next empty pointer is the right child */
    aux = aux->parent;
  }
  return aux;
}


/**
 * updates the heap after node's key value be changed to a better value
 * @param heap pointer to binary heap control structure
 * @param node pointer to the node changed
 */
void
heap_decrease_key(struct bin_heap *heap, struct heap_node *node)
{
  struct heap_node *parent = node->parent;
  struct heap_node *left = node->left;
  struct heap_node *right = node->right;
  if (!parent) {
    return;
  }
  if (parent->key > node->key) {
    if (heap->last_node == node) {
      heap->last_node = parent;
    }
  }
  /* while the node be better than its parent, swat with it */
  while (parent && (parent->key > node->key)) {
    if (parent->left == node) {
      node->left = parent;
      node->right = parent->right;
      if (node->right) {
        node->right->parent = node;
      }
      node->parent = parent->parent;
      if (node->parent) {
        if (node->parent->left == parent) {
          node->parent->left = node;
        } else{
          node->parent->right = node;
        }
      } else {
        heap->root_node = node;
      }
    } else {
      node->right = parent;
      node->left = parent->left;
      if (node->left) {
        node->left->parent = node;
      }
      node->parent = parent->parent;
      if(node->parent) {
        if(node->parent->left == parent) {
          node->parent->left = node;
        } else {
          node->parent->right = node;
        }
      } else{
        heap->root_node = node;
      }
    }
    parent->left = left;
    parent->right = right;
    parent->parent = node;
    if(left) {
      left->parent = parent;
    }
    if(right) {
      right->parent = parent;
    }
    parent = node->parent;
    left = node->left;
    right = node->right;
  }
}

/**
 * inserts the node in the binary heap
 * @param heap pointer to binary heap control structure
 * @param node pointer to node that will be inserted
 */
void
heap_insert(struct bin_heap *heap, struct heap_node *node)
{
  struct heap_node *parent = NULL;

  heap_init_node(node);

  if (!heap->count) {
    heap->root_node = heap->last_node = node;
    heap->count++;
  }
  else {
    /* find the parent of this node */
    parent = heap_find_parent_insert_node(heap);
    if (parent->left) {
      parent->right = node;
    } else{
      parent->left = node;
    }
    node->parent = parent;
    heap->count++;
    heap->last_node = node;
    /* update the heap finding the right position to this node */
    heap_decrease_key(heap, node);
  }
}

/**
 * swaps the node with its left child
 * @param heap pointer to binary heap control structure
 * @param node pointer to node that will be swapped
 */
static void
heap_swap_left(struct bin_heap *heap, struct heap_node *node)
{
  struct heap_node *parent = node->parent;
  struct heap_node *left = node->left;
  struct heap_node *right = node->right;

  node->parent = left;
  node->left = left->left;
  if (node->left) {
    node->left->parent = node;
  }
  node->right = left->right;
  if (node->right) {
    node->right->parent = node;
  }
  left->parent = parent;
  if (parent) {
    if (parent->left == node) {
      parent->left = left;
    } else {
      parent->right = left;
    }
  } else {
    heap->root_node = left;
  }
  left->left = node;
  left->right = right;
  if (right) {
    right->parent = left;
  }
  if (heap->last_node == left) {
    heap->last_node = node;
  }
}

/**
 * swaps the node with its right child
 * @param heap pointer to binary heap control structure
 * @param node pointer to node that will be swapped
 */
static void
heap_swap_right(struct bin_heap *heap, struct heap_node *node)
{
  struct heap_node *parent = node->parent;
  struct heap_node *left = node->left;
  struct heap_node *right = node->right;

  node->parent = right;
  node->left = right->left;
  if (node->left) {
    node->left->parent = node;
  }
  node->right = right->right;
  if (node->right) {
    node->right->parent = node;
  }

  right->parent = parent;
  if (parent) {
    if (parent->left == node) {
      parent->left = right;
    } else {
      parent->right = right;
    }
  } else {
    heap->root_node = right;
  }
  right->right = node;
  right->left = left;
  if (left) {
    left->parent = right;
  }
  if (heap->last_node == right) {
    heap->last_node = node;
  }
}

/**
 * updates the heap after node's key value be changed to a worse value.
 * it's a recursive function
 * @param heap pointer to binary heap control structure
 * @param node pointer to the node changed
 */
static void
heap_increse_key(struct bin_heap *heap, struct heap_node *node)
{
  struct heap_node *left = node->left;
  struct heap_node *right = node->right;
  /* swap with the best child */
  if (left && (node->key > left->key)) {
    if (right && (node->key > right->key)) {
      if (left->key < right->key) {
        heap_swap_left(heap, node);
      } else {
        heap_swap_right(heap, node);
      }
    } else {
      heap_swap_left(heap, node);
    }
    heap_increse_key(heap, node);
  }
  else if (right && (node->key > right->key)) {
    heap_swap_right(heap, node);
    heap_increse_key(heap, node);
  }
}

/**
 * finds the last node of the binary heap
 * @param heap pointer to binary heap control structure
 * @return the pointer to last node
 */
static struct heap_node*
heap_find_last_node(struct bin_heap *heap)
{
  struct heap_node *aux = heap->last_node;
  unsigned int N = heap->count+1;
  if ( !heap_perfect_log2(N) ){
    /* if the heap is full the last node is the rightist node */
    aux = heap->root_node;
    while (aux->right) {
      aux = aux->right;
    }
  }
  else if ( N % 2 == 0){
    /* otherwise find the rightist node*/
    while (aux->parent->left == aux) {
      aux = aux->parent;
    }
    aux = aux->parent->left;
    while (aux->right) {
      aux = aux->right;
    }
  }
  return aux;
}

/**
 * deletes and returns the best node from binary heap
 * @param heap pointer to binary heap control structure
 * @return the pointer to best node
 */
struct heap_node *
heap_extract_min(struct bin_heap *heap)
{
  struct heap_node *min_node = heap->root_node;
  struct heap_node *new_min = heap->last_node;
  if (!min_node) {
    return NULL;
  }
  heap->count--;
  if (heap->count == 0) {
    heap->last_node = heap->root_node = NULL;
  }
  else if (heap->count == 1) {
    heap->last_node = heap->root_node = new_min;
    new_min->parent = NULL;
  } else {
    /* the last node goes to the root position */
    if (new_min->parent->left == new_min) {
      new_min->parent->left = NULL;
      heap->last_node = new_min->parent;
      /* set the new last node*/
      heap->last_node = heap_find_last_node(heap);
    } else {
      new_min->parent->right = NULL;
      heap->last_node = new_min->parent->left;
    }
    new_min->left = min_node->left;
    if (new_min->left){
      new_min->left->parent = new_min;
    }
    new_min->right = min_node->right;
    if (new_min->right) {
      new_min->right->parent = new_min;
    }
    new_min->parent = NULL;
    heap->root_node = new_min;
    /* update the heap to keep its property */
    heap_increse_key(heap, new_min);
  }
  heap_init_node(min_node);
  return min_node;
}
