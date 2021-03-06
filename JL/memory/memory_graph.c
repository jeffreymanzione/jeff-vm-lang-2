/*
 * heap.c
 *
 *  Created on: Sep 30, 2016
 *      Author: Jeff
 */

#include "memory_graph.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../arena/arena.h"
#include "../arena/strings.h"
#include "../datastructure/array.h"
#include "../datastructure/expando.h"
#include "../datastructure/map.h"
#include "../datastructure/tuple.h"
#include "../error.h"
#include "../ltable/ltable.h"
#include "../shared.h"
#include "memory.h"

// Large prime. May help initial startup.
#define DEFAULT_NODE_TABLE_SZ 48337

typedef struct MemoryGraph_ {
  // ID-related bools
  bool rand_seeded, use_rand;
  uint32_t id_counter;
  Set /*<Node>*/ nodes;
  Set /*<Node>*/ roots;

  //  Set/*<Thread>*/threads;
#ifdef ENABLE_MEMORY_LOCK
  ThreadHandle access_mutex;
//  RWLock *rw_lock;
#endif
} MemoryGraph;

NodeID new_id(MemoryGraph *graph) {
  ASSERT_NOT_NULL(graph);
  uint32_t int_id;
  if (graph->use_rand) {
    if (!graph->rand_seeded) {
      srand((uint32_t)time(NULL));
      graph->rand_seeded = true;
    }
    int_id = rand();
  } else {
    int_id = graph->id_counter++;
  }
  NodeID id = {int_id};
  return id;
}

uint32_t node_hasher(const void *node) {
  ASSERT_NOT_NULL(node);
  return ((Node *)node)->id.int_id;
}

int32_t node_comparator(const void *n1, const void *n2) {
  ASSERT_NOT_NULL(n1);
  ASSERT_NOT_NULL(n2);
  return ((Node *)n1)->id.int_id - ((Node *)n2)->id.int_id;
}

MemoryGraph *memory_graph_create() {
  MemoryGraph *graph = ALLOC2(MemoryGraph);
#ifdef ENABLE_MEMORY_LOCK
  graph->access_mutex = mutex_create(NULL);
//  graph->rw_lock = create_rwlock();
#endif
  set_init(&graph->nodes, DEFAULT_NODE_TABLE_SZ, default_hasher,
           default_comparator);
  set_init(&graph->roots, DEFAULT_TABLE_SZ, default_hasher, default_comparator);
  graph->rand_seeded = false;
  graph->use_rand = false;
  graph->id_counter = 0;
  return graph;
}

uint32_t node_edge_hasher(const NodeEdge *edge) {
  ASSERT_NOT_NULL(edge);
  return edge->node->id.int_id;
}

int32_t node_edge_comparator(const NodeEdge *edge1, const NodeEdge *edge2) {
  return edge1->node->id.int_id - edge2->node->id.int_id;
}

Node *node_create(MemoryGraph *graph) {
  ASSERT_NOT_NULL(graph);
  Node *node = ARENA_ALLOC(Node);
  // TODO: Instead of locking here, give each thread its own arena and merge
  // them when threads finish.
#ifdef ENABLE_MEMORY_LOCK
  mutex_await(graph->access_mutex, INFINITE);
#endif
  node->id = new_id(graph);
  set_insert(&graph->nodes, node);
#ifdef ENABLE_MEMORY_LOCK
  mutex_release(graph->access_mutex);
#endif
  set_init(&node->parents, DEFAULT_TABLE_SZ, (Hasher)node_edge_hasher,
           (Comparator)node_edge_comparator);
  set_init(&node->children, DEFAULT_TABLE_SZ, (Hasher)node_edge_hasher,
           (Comparator)node_edge_comparator);
#ifdef ENABLE_MEMORY_LOCK
  node->access_mutex = mutex_create(NULL);
#endif
  return node;
}

void node_delete(MemoryGraph *graph, Node *node, bool free_mem) {
  ASSERT_NOT_NULL(graph);
  ASSERT_NOT_NULL(node);
  if (free_mem) {
    void delete_node_edge(void *ptr) {
      ARENA_DEALLOC(NodeEdge, (NodeEdge *)ptr);
    }
    set_iterate(&node->children, delete_node_edge);
    set_iterate(&node->parents, delete_node_edge);
  }
  set_finalize(&node->children);
  set_finalize(&node->parents);
  obj_delete_ptr(&node->obj, /*free_mem=*/free_mem);
#ifdef ENABLE_MEMORY_LOCK
  mutex_close(node->access_mutex);
#endif
  if (free_mem) {
    set_remove(&graph->nodes, node);
    ARENA_DEALLOC(Node, node);
  }
}

void memory_graph_delete(MemoryGraph *graph) {
  ASSERT_NOT_NULL(graph);
#ifdef DEBUG
  int node_count = 0;
  int field_count = 0;
  int children_count = 0;
#endif
  void delete_node_and_obj(void *p) {
    ASSERT_NOT_NULL(p);
    Node *node = (Node *)p;
#ifdef DEBUG
    node_count++;
    field_count += map_size(&node->obj.fields);
    Set *set = &node->children;
    if (set) {
      children_count += set_size(set);
    }
#endif
    node_delete(graph, node, /*free_mem=*/false);
  }
  // Do not adjust order of deletes
  set_finalize(&graph->roots);
  set_iterate(&graph->nodes, delete_node_and_obj);

#ifdef DEBUG
  fprintf(stdout,
          "There are %d members of the graph.\n"
          "Total/Avg # fields: %d/%.02f\n"
          "Total/Avg # children %d/%.02f\n",
          node_count, field_count, (field_count * 1.0) / node_count,
          children_count, (children_count * 1.0) / node_count);
  fflush(stdout);
#endif
  set_finalize(&graph->nodes);
#ifdef ENABLE_MEMORY_LOCK
  mutex_close(graph->access_mutex);
//  close_rwlock(graph->rw_lock);
#endif
  DEALLOC(graph);
}

Element memory_graph_new_node(MemoryGraph *graph) {
  ASSERT_NOT_NULL(graph);
  Node *node = node_create(graph);
  node->obj.node = node;
  node->obj.type = OBJ;
  node->obj.is_external = false;
  map_init_default(&node->obj.fields);
  node->obj.parent_objs = expando(Object *, 4);
  Element e = {
      .type = OBJECT,
      .obj = &node->obj,
  };
  e.obj->is_const = false;
  Element adr = create_int((int32_t)e.obj);
  memory_graph_set_field_ptr(graph, e.obj, ADDRESS_KEY, &adr);
  return e;
}

NodeEdge *node_edge_create(Node *to) {
  NodeEdge *ne = ARENA_ALLOC(NodeEdge);
  ne->ref_count = 1;
  ne->node = to;
  return ne;
}

#ifdef ENABLE_MEMORY_LOCK
void acquire_all_mutex(const Node *const n1, const Node *const n2) {
  if (n1 == NULL && n2 == NULL) {
    return;
  }
  if (n1 != NULL && n2 == NULL) {
    mutex_await(n1->access_mutex, INFINITE);
    return;
  }
  if (n1 == NULL && n2 != NULL) {
    mutex_await(n2->access_mutex, INFINITE);
    return;
  }

  ThreadHandle first =
      n1->id.int_id > n2->id.int_id ? n1->access_mutex : n2->access_mutex;
  ThreadHandle second =
      n1->id.int_id > n2->id.int_id ? n2->access_mutex : n1->access_mutex;
  mutex_await(first, INFINITE);
  mutex_await(second, INFINITE);
}

void release_all_mutex(const Node *const n1, const Node *const n2) {
  if (n1 == NULL && n2 == NULL) {
    return;
  }
  if (n1 != NULL && n2 == NULL) {
    mutex_release(n1->access_mutex);
    return;
  }
  if (n1 == NULL && n2 != NULL) {
    mutex_release(n2->access_mutex);
    return;
  }
  ThreadHandle first =
      n1->id.int_id > n2->id.int_id ? n1->access_mutex : n2->access_mutex;
  ThreadHandle second =
      n1->id.int_id > n2->id.int_id ? n2->access_mutex : n1->access_mutex;
  mutex_release(second);
  mutex_release(first);
}
#endif

DEB_FN(void, memory_graph_inc_edge, MemoryGraph *graph,
       const Object *const parent, const Object *const child) {
  ASSERT_NOT_NULL(graph);
  Node *parent_node = parent->node;
  ASSERT_NOT_NULL(parent_node);
  Node *child_node = child->node;
  ASSERT_NOT_NULL(child_node);

  Set *children_of_parent = &parent_node->children;
  ASSERT_NOT_NULL(children_of_parent);
  NodeEdge tmp_child_edge = {child_node, -1};
  NodeEdge *child_edge;

  Set *parents_of_child = &child_node->parents;
  ASSERT_NOT_NULL(parents_of_child);
  NodeEdge tmp_parent_edge = {parent_node, -1};
  NodeEdge *parent_edge;

#ifdef ENABLE_MEMORY_LOCK
  acquire_all_mutex(parent_node, child_node);
//  begin_read(graph->rw_lock);
#endif
  // if There is already an edge, increase the edge count
  if (NULL != (child_edge = set_lookup(children_of_parent, &tmp_child_edge))) {
    child_edge->ref_count++;
  } else {
    // Create edge from parent to child
    set_insert(children_of_parent, node_edge_create(child_node));
  }
  // if There is already an edge, increase the edge count
  if (NULL != (parent_edge = set_lookup(parents_of_child, &tmp_parent_edge))) {
    parent_edge->ref_count++;
  } else {
    // Create edge from child to parent
    set_insert(parents_of_child, node_edge_create(parent_node));
  }
#ifdef ENABLE_MEMORY_LOCK
  //  end_read(graph->rw_lock);
  release_all_mutex(parent_node, child_node);
#endif
}
#define memory_graph_inc_edge(...) CALL_FN(memory_graph_inc_edge__, __VA_ARGS__)

Element memory_graph_create_root_element(MemoryGraph *graph) {
  ASSERT_NOT_NULL(graph);
  Element elt = memory_graph_new_node(graph);
  set_insert(&graph->roots, elt.obj->node);
  return elt;
}

DEB_FN(void, memory_graph_dec_edge, MemoryGraph *graph,
       const Object *const parent, const Object *const child) {
  ASSERT_NOT_NULL(graph);
  Node *parent_node = parent->node;
  ASSERT_NOT_NULL(parent_node);
  Node *child_node = child->node;
  ASSERT_NOT_NULL(child_node);

  Set *children_of_parent = &parent_node->children;
  ASSERT_NOT_NULL(children_of_parent);
  NodeEdge tmp_child_edge = {child_node, -1};

  Set *parents_of_child = &child_node->parents;
  ASSERT_NOT_NULL(parents_of_child);
  NodeEdge tmp_parent_edge = {parent_node, -1};

#ifdef ENABLE_MEMORY_LOCK
  //  begin_read(graph->rw_lock);
  acquire_all_mutex(parent_node, child_node);
#endif
  // Remove edge from parent to child
  NodeEdge *child_edge = set_lookup(children_of_parent, &tmp_child_edge);
  ASSERT_NOT_NULL(child_edge);
  --child_edge->ref_count;
  // Remove edge from child to parent
  NodeEdge *parent_edge = set_lookup(parents_of_child, &tmp_parent_edge);
  ASSERT_NOT_NULL(parent_edge);
  --parent_edge->ref_count;

#ifdef ENABLE_MEMORY_LOCK
  release_all_mutex(parent_node, child_node);
//  end_read(graph->rw_lock);
#endif
}
#define memory_graph_dec_edge(...) CALL_FN(memory_graph_dec_edge__, __VA_ARGS__)

const Node *node_for(const Element *e) {
  return (e->type == OBJECT) ? e->obj->node : NULL;
}

DEB_FN(void, memory_graph_set_field, MemoryGraph *graph, const Element parent,
       const char field_name[], const Element field_val) {
  ASSERT_NOT_NULL(graph);
  ASSERT(OBJECT == parent.type);
  ASSERT_NOT_NULL(parent.obj);
  Element existing = obj_get_field(parent, field_name);
  // If the field is already set to an object, remove the edge to the old child
  // node.
  if (OBJECT == existing.type) {
    memory_graph_dec_edge(graph, parent.obj, existing.obj);
  }
  if (OBJECT == field_val.type) {
    memory_graph_inc_edge(graph, parent.obj, field_val.obj);
  }
  obj_set_field(parent.obj, field_name, &field_val);
}

DEB_FN(void, memory_graph_set_field_ptr, MemoryGraph *graph, Object *parent,
       const char field_name[], const Element *field_val) {
  ASSERT_NOT_NULL(graph);
  ASSERT_NOT_NULL(parent);
  ElementContainer *ref = obj_get_field_obj_raw(parent, field_name);
  // No existing ref.
  if (NULL == ref) {
    if (OBJECT == field_val->type) {
      memory_graph_inc_edge(graph, parent, field_val->obj);
    }
    obj_set_field(parent, field_name, field_val);
    return;
  }
  // If the field is already set to an object, remove the edge to the old child
  // node.
  if (OBJECT == ref->elt.type) {
    memory_graph_dec_edge(graph, parent, ref->elt.obj);
  }
  if (OBJECT == field_val->type) {
    memory_graph_inc_edge(graph, parent, field_val->obj);
  }
  CommonKey key = CKey_lookup_key(field_name);
  if (key >= 0) {
    parent->ltable[key] = *field_val;
  }
  ref->elt = *field_val;
}

DEB_FN(void, memory_graph_set_field_ptr_value, MemoryGraph *graph,
       Object *parent, const char field_name[], const Value val) {
  ASSERT_NOT_NULL(graph);
  ASSERT_NOT_NULL(parent);
  Element e = {.type = VALUE, .val = val};
  obj_set_field(parent, field_name, &e);
}

void memory_graph_set_var(MemoryGraph *graph, const Element block,
                          const char field_name[], const Element field_val) {
  ASSERT_NOT_NULL(graph);
  ASSERT(OBJECT == block.type);
  ASSERT_NOT_NULL(block.obj);
  ElementContainer *container = obj_get_field_obj_raw(block.obj, field_name);

  Element relevant_block;
  Element parent = block;
  while (NULL == container) {
    parent = obj_lookup(parent.obj, CKey_$parent);
    if (NONE == parent.type) break;
    container = obj_get_field_obj_raw(parent.obj, field_name);
  }
  if (NULL == container) {
    relevant_block = block;
  } else {
    relevant_block = parent;
  }

  // If the field is already set to an object, remove the edge to the old
  // child node.
  if (NULL != container && OBJECT == container->elt.type) {
    memory_graph_dec_edge(graph, relevant_block.obj, container->elt.obj);
  }
  if (OBJECT == field_val.type) {
    memory_graph_inc_edge(graph, relevant_block.obj, field_val.obj);
  }

  obj_set_field(relevant_block.obj, field_name, &field_val);
}

void traverse_subtree(MemoryGraph *graph, Set *marked, Node *node) {
  ASSERT_NOT_NULL(graph);
  if (set_lookup(marked, node)) {
    return;
  }
  set_insert(marked, node);
  void traverse_subtree_helper(void *ptr) {
    NodeEdge *child_edge = (NodeEdge *)ptr;
    ASSERT_NOT_NULL(child_edge);
    ASSERT_NOT_NULL(child_edge->node);
    // Don't traverse edges which no longer are present
    if (child_edge->ref_count < 1) {
      // Cannot do set_remove because it breaks the iterator?
      //      set_remove(&node->children, child_edge);
      //      ARENA_DEALLOC(NodeEdge, child_edge);
      return;
    }
    traverse_subtree(graph, marked, child_edge->node);
  }
  set_iterate(&node->children, traverse_subtree_helper);
}

int memory_graph_free_space(MemoryGraph *graph) {
  ASSERT_NOT_NULL(graph);

#ifdef ENABLE_MEMORY_LOCK
  mutex_await(graph->access_mutex, INFINITE);
//  begin_write(graph->rw_lock);
#endif

  int nodes_deleted = 0;
  // default table size to # of nodes. This will avoid resizing the table.
  Set *marked =
      set_create(set_size(&graph->nodes) * 2, node_hasher, node_comparator);
  void traverse_graph(void *ptr) {
    Node *node = (Node *)ptr;
    ASSERT_NOT_NULL(node);
    traverse_subtree(graph, marked, node);
  }
  set_iterate(&graph->roots, traverse_graph);

  void delete_node_if_not_marked(void *p) {
    ASSERT_NOT_NULL(p);
    Node *node = (Node *)p;
    if (set_lookup(marked, node)) {
      return;
    }
    node_delete(graph, node, /*free_mem=*/true);
    nodes_deleted++;
    ASSERT_NULL(set_lookup(&graph->nodes, node));
  }
  set_iterate(&graph->nodes, delete_node_if_not_marked);
  set_delete(marked);

#ifdef ENABLE_MEMORY_LOCK
  //  end_write(graph->rw_lock);
  mutex_release(graph->access_mutex);
#endif
  return nodes_deleted;
}

Array *extract_array(Element element) {
  ASSERT(OBJECT == element.type);
  ASSERT(ARRAY == element.obj->type);
  ASSERT_NOT_NULL(element.obj->array);
  return element.obj->array;
}

void memory_graph_array_push(MemoryGraph *graph, Object *parent,
                             const Element *element) {
  ASSERT_NOT_NULL(graph);
  Array *arr = parent->array;
  Array_push(arr, *element);
  if (OBJECT == element->type) {
    memory_graph_inc_edge(graph, parent, element->obj);
  }
  Element array_size = create_int(Array_size(arr));
  memory_graph_set_field_ptr(graph, parent, LENGTH_KEY, &array_size);
}

void memory_graph_array_set(MemoryGraph *graph, Object *parent, int64_t index,
                            const Element *element) {
  ASSERT(NOT_NULL(graph), index >= 0);
  Array *arr = parent->array;
  if (index < Array_size(arr)) {
    Element old = Array_get(arr, index);
    if (old.type == OBJECT) {
      memory_graph_dec_edge(graph, parent, old.obj);
    }
  }
  Array_set(arr, index, *element);
  if (OBJECT == element->type) {
    memory_graph_inc_edge(graph, parent, element->obj);
  }
  Element array_size = create_int(Array_size(arr));
  memory_graph_set_field_ptr(graph, parent, LENGTH_KEY, &array_size);
}

Element memory_graph_array_pop(MemoryGraph *graph, Object *parent) {
  ASSERT_NOT_NULL(graph);
  ASSERT(ARRAY == parent->type);
  Array *arr = parent->array;
  ASSERT(Array_size(arr) > 0);
  Element element = Array_pop(arr);
  if (OBJECT == element.type) {
    memory_graph_dec_edge(graph, parent, element.obj);
  }
  Element array_size = create_int(Array_size(arr));
  memory_graph_set_field_ptr(graph, parent, LENGTH_KEY, &array_size);
  return element;
}

void memory_graph_array_enqueue(MemoryGraph *graph, const Element parent,
                                const Element element) {
  ASSERT_NOT_NULL(graph);
  Array *arr = extract_array(parent);
  Array_enqueue(arr, element);
  if (OBJECT == element.type) {
    memory_graph_inc_edge(graph, parent.obj, element.obj);
  }
  Element array_size = create_int(Array_size(arr));
  memory_graph_set_field_ptr(graph, parent.obj, LENGTH_KEY, &array_size);
}

Element memory_graph_array_join(MemoryGraph *graph, const Element a1,
                                const Element a2) {
  Element joined = create_array(graph);
  Array_append(joined.obj->array, a1.obj->array);
  Array_append(joined.obj->array, a2.obj->array);
  void append_child_edges(void *p) {
    NodeEdge *ne = (NodeEdge *)p;
    int i;
    for (i = 0; i < ne->ref_count; i++) {
      memory_graph_inc_edge(graph, joined.obj, &ne->node->obj);
    }
  }
  set_iterate(&a1.obj->node->children, append_child_edges);
  set_iterate(&a2.obj->node->children, append_child_edges);
  Element array_size = create_int(Array_size(joined.obj->array));
  memory_graph_set_field_ptr(graph, joined.obj, LENGTH_KEY, &array_size);
  return joined;
}

Element memory_graph_array_dequeue(MemoryGraph *graph, const Element parent) {
  ASSERT_NOT_NULL(graph);
  Array *arr = extract_array(parent);
  Element element = Array_dequeue(arr);
  if (OBJECT == element.type) {
    memory_graph_dec_edge(graph, parent.obj, element.obj);
  }
  Element array_size = create_int(Array_size(arr));
  memory_graph_set_field_ptr(graph, parent.obj, LENGTH_KEY, &array_size);
  return element;
}

Element memory_graph_array_remove(MemoryGraph *graph, const Element parent,
                                  int index) {
  ASSERT_NOT_NULL(graph);
  Array *arr = extract_array(parent);
  Element element = Array_remove(arr, index);
  if (OBJECT == element.type) {
    memory_graph_dec_edge(graph, parent.obj, element.obj);
  }
  Element array_size = create_int(Array_size(arr));
  memory_graph_set_field_ptr(graph, parent.obj, LENGTH_KEY, &array_size);
  return element;
}

void memory_graph_array_shift(MemoryGraph *graph, const Element parent,
                              int start, int count, int shift) {
  ASSERT_NOT_NULL(graph);
  if (shift == 0 || count == 0) {
    return;
  }
  Array *arr = extract_array(parent);
  int i;
  int i_begin = (shift > 0) ? start + count : start + shift;
  int i_end = (shift > 0) ? start + count + shift : start;
  int array_size = Array_size(arr);
  for (i = i_begin; i < i_end && i < array_size; i++) {
    Element element = Array_get(arr, i);
    if (OBJECT == element.type) {
      memory_graph_dec_edge(graph, parent.obj, element.obj);
    }
  }
  // TODO: Need to inc edges of members that are now present twice.
  Array_shift_amount(arr, start, count, shift);
  Element array_len = create_int(Array_size(arr));
  memory_graph_set_field_ptr(graph, parent.obj, LENGTH_KEY, &array_len);
}

void memory_graph_tuple_add(MemoryGraph *graph, const Element tuple,
                            const Element elt) {
  ASSERT(NOT_NULL(graph), OBJECT == tuple.type, TUPLE == tuple.obj->type);
  tuple_add(tuple.obj->tuple, elt);
  if (OBJECT == elt.type) {
    memory_graph_inc_edge(graph, tuple.obj, elt.obj);
  }

  Element tuple_len = create_int(tuple_size(tuple.obj->tuple));
  memory_graph_set_field_ptr(graph, tuple.obj, LENGTH_KEY, &tuple_len);
}

void memory_graph_print(const MemoryGraph *graph, FILE *file) {
  ASSERT_NOT_NULL(graph);
  void print_node_id(void *ptr) {
    ASSERT_NOT_NULL(ptr);
    fprintf(file, "%u ", ((Node *)ptr)->id.int_id);
  }
  fprintf(file, "roots={ ");
  set_iterate(&graph->roots, print_node_id);
  fprintf(file, "}\n");
  void print_node_id2(void *ptr) {
    ASSERT_NOT_NULL(ptr);
    fprintf(file, "%u ", ((Node *)ptr)->id.int_id);
  }
  fprintf(file, "nodes(%d)={ ", set_size(&graph->nodes));
  set_iterate(&graph->nodes, print_node_id2);
  fprintf(file, "}\n");
  void print_edges_for_child(void *ptr) {
    ASSERT_NOT_NULL(ptr);
    Node *parent = (Node *)ptr;
    Set *children = (Set *)&parent->children;
    void print_edge(void *ptr) {
      ASSERT_NOT_NULL(ptr);
      NodeEdge *edge = (NodeEdge *)ptr;
      int i;
      for (i = 0; i < edge->ref_count; i++) {
        fprintf(file, "%u-->%u ", parent->id.int_id, edge->node->id.int_id);
      }
    }
    set_iterate(children, print_edge);
  }
  fprintf(file, "edges={ ");
  set_iterate(&graph->nodes, print_edges_for_child);
  fprintf(file, "}\n\n");
}
