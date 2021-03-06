/*
 * element.h
 *
 *  Created on: Sep 27, 2016
 *      Author: Jeff
 */

#ifndef ELEMENT_H_
#define ELEMENT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "datastructure/expando.h"
#include "datastructure/map.h"
#include "datastructure/queue2.h"
#include "datastructure/set.h"
#include "ltable/ltable.h"

#define VALUE_OF(val)  \
  (((val).type == INT) \
       ? val.int_val   \
       : (((val).type == FLOAT) ? val.float_val : val.char_val))

typedef struct Array_ Array;
typedef struct Tuple_ Tuple;
typedef struct Module_ Module;
typedef struct VM_ VM;
typedef struct MemoryGraph_ MemoryGraph;
typedef struct Node_ Node;

typedef struct Element_ Element;
typedef struct ElementContainer_ ElementContainer;
typedef struct Thread_ Thread;
typedef struct Object_ Objectt;
typedef struct ExternalData_ ExternalData;
typedef Element (*ExternalFunction)(VM *, Thread *, ExternalData *, Element *);

// Do not manually access any of these =(
typedef enum { INT, FLOAT, CHAR } ValType;
typedef struct Value_ {
  char type;
  union {
    int8_t char_val;
    int64_t int_val;
    double float_val;
  };
} Value;

typedef enum { NONE, OBJECT, VALUE } ElementType;

typedef struct Element_ {
  char type;
  union {
    struct Object_ *obj;
    Value val;
  };
} Element;

typedef enum { OBJ, ARRAY, TUPLE, MODULE } ObjectType;

typedef struct Object_ {
  char type;
  // Pointer to node owner.
  Node *node;
  Element ltable[CKey_END];
  Map fields;
  bool is_external, is_const;
  Expando *parent_objs;

  union {
    Array *array;
    Tuple *tuple;
    const Module *module;
    ExternalFunction external_fn;
    ExternalData *external_data;
  };
} Object;

typedef struct ElementContainer_ {
  bool is_const : 1;
  bool is_private : 1;
  Element elt;
} ElementContainer;

extern const Element ELEMENT_NONE;

Element create_none();
Element create_int(int64_t val);
Element create_float(double val);
Element create_char(int8_t val);
Element create_obj(MemoryGraph *graph);
Element create_obj_unsafe(MemoryGraph *graph);
Element create_obj_of_class(MemoryGraph *graph, Element class);

Element create_class_stub(MemoryGraph *graph);
void fill_object_unsafe(MemoryGraph *graph, Element element, Element class);

Element create_external_obj(VM *vm, Element class);
Element create_array(MemoryGraph *graph);

Element string_create_len(VM *vm, const char *str, size_t len);
Element string_create_len_unescape(VM *vm, const char *str, size_t len);
Element string_create(VM *vm, const char *str);
Element string_add(VM *vm, Element str1, Element str2);

Element create_tuple(MemoryGraph *graph);
Element create_module(VM *vm, const Module *module);
Element create_function(VM *vm, Element module, uint32_t ins, const char name[],
                        Q *args);
Element create_external_function(VM *vm, Element module, const char name[],
                                 ExternalFunction external_fn);
Element create_external_method(VM *vm, Element class, const char name[],
                               ExternalFunction external_fn);
Element create_method(VM *vm, Element module, uint32_t ins, Element class,
                      const char name[], Q *args);
Element create_method_instance(MemoryGraph *graph, Element object,
                               Element method);
Element create_external_method_instance(MemoryGraph *graph, Element object,
                                        Element method);
Element create_anonymous_function(VM *vm, Thread *t, Element func);

Element val_to_elt(Value val);
Value value_negate(Value val);

Element obj_lookup(Object *obj, CommonKey key);
Element *obj_lookup_ptr(Object *obj, CommonKey key);
void obj_set_field(Object *elt, const char field_name[],
                   const Element *const field_val);
ElementContainer *obj_get_field_obj_raw(const Object *const obj,
                                        const char field_name[]);
Element obj_get_field_obj(const Object *constobj, const char field_name[]);
Element obj_get_field(Element elt, const char field_name[]);
Element *obj_get_field_ptr(const Object *const obj, const char field_name[]);
Element *obj_deep_lookup(const Object *const obj, const char name[]);
Element *obj_deep_lookup_ckey(const Object *const obj, CommonKey key);
void obj_delete_ptr(Object *obj, bool free_mem);

void class_parents(Element child_class, Set *classes);
typedef bool (*ObjectActionUntil)(Object *);
void class_parents_action(Object *child_class, ObjectActionUntil process);

// Will fail if there is a cycle
void obj_to_str(Object *obj, FILE *file);
void elt_to_str(Element elt, FILE *file);
void val_to_str(Value val, FILE *file);

Element value_fmt(VM *vm, Value val, Element fmt);

Element element_true(VM *vm);
Element element_false(VM *vm);
Element element_not(VM *vm, Element elt);

Element element_from_obj(Object *const obj);

bool is_true(Element elt);
bool is_false(Element elt);

char *string_to_cstr(Element str);

Element make_const(Element elt);
void make_const_ref(Object *obj, const char field_name[]);
bool is_const_ref(Object *obj, const char field_name[]);

#endif /* ELEMENT_H_ */
