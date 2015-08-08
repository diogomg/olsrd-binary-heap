
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

#ifndef _HEAP_H
#define _HEAP_H

#include "olsr_types.h"

#define INLINE inline __attribute__((always_inline))

/**
 * Element included into a binary heap. Struct to control the heap
 */
struct heap_node{
  /**
   * node's key based on the link cost type.
   */
  olsr_linkcost key;

  /**
   * Pointer to parent node in the heap, NULL if root node.
   */
  struct heap_node *parent;

  /**
   * Pointer to left child, NULL if there isn't.
   */
  struct heap_node *left;

  /**
   * Pointer to right child, NULL if there isn't.
   */
  struct heap_node *right;
};

/**
 * Manager struct of the binary heap.
 * One of them is necessary for each heap.
 */
struct bin_heap{
  /**
   * Number of nodes in the heap.
   */
  unsigned int count;

  /**
   * Pointer to the root node of the heap, NULL if heap is empty.
   */
  struct heap_node *root_node;

  /**
   * Pointer to the rightest node of the lowest level in the heap,
   * NULL if heap is empty.
   */
  struct heap_node *last_node;
};

void heap_init(struct bin_heap *heap);
void heap_init_node(struct heap_node *node);
void heap_decrease_key(struct bin_heap *heap, struct heap_node *node);
void heap_insert(struct bin_heap *heap, struct heap_node *node);
struct heap_node *heap_extract_min(struct bin_heap *heap);

/**
 * @param heap pointer to binary heap
 * @return size of heap, 0 if is empty
 */
static INLINE unsigned int
heap_get_size(struct bin_heap *heap)
{
  return heap->count;
}

/**
 * @param heap pointer to binary heap
 * @return pointer to root node
 */
static INLINE struct heap_node*
heap_get_root_node(struct bin_heap *heap)
{
  return heap->root_node;
}

/**
 * @param heap pointer to binary heap
 * @return true if the heap is empty, false otherwise
 */
static INLINE bool
heap_is_empty(struct bin_heap *heap)
{
  return heap->count == 0;
}

/**
 * @param heap pointer to binary heap
 * @param node pointer to node of the heap
 * @return true if node is currently in the heap, false otherwise
 */
static INLINE bool
heap_is_node_added(struct bin_heap *heap, struct heap_node *node)
{
  if (node) {
    if (node->parent || node->left || node->right) {
      return true;
    }
    if (node == heap_get_root_node(heap)) {
      return true;
    }
  }
  return false;
}

#define HEAPNODE2STRUCT(funcname, structname, heapnodename) \
static inline structname * funcname (struct heap_node *ptr)\
{\
  return( \
    ptr ? \
      (structname *) (((size_t) ptr) - offsetof(structname, heapnodename)) : \
      NULL); \
}

#endif /* _HEAP_H */
