/*
 * vm.c
 *
 *  Created on: Dec 8, 2016
 *      Author: Jeff
 */

#include "vm.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena/strings.h"
#include "class.h"
#include "codegen/tokenizer.h"
#include "datastructure/array.h"
#include "datastructure/map.h"
#include "datastructure/tuple.h"
#include "error.h"
#include "external/external.h"
#include "external/strings.h"
#include "file_load.h"
#include "graph/memory.h"
#include "graph/memory_graph.h"
#include "instruction.h"
#include "module.h"
#include "ops.h"
#include "shared.h"
#include "tape.h"

#ifdef DEBUG
#define BUILTIN_SRC "builtin.jl"
#define IO_SRC      "io.jl"
#define STRUCT_SRC   "struct.jl"
#define ERROR_SRC   "error.jl"
#else
#define BUILTIN_SRC "builtin.jb"
#define IO_SRC      "io.jb"
#define STRUCT_SRC   "struct.jb"
#define ERROR_SRC   "error.jb"
#endif

const char *PRELOADED[] = { IO_SRC, STRUCT_SRC, ERROR_SRC };

const Element vm_get_resval(VM *vm);
void vm_set_resval(VM *vm, const Element elt);
void call_new(VM *vm, Element class);
void call_fn(VM *vm, Element obj, Element func);
void vm_shift_ip(VM *vm, int num_ins);

int preloaded_size() {
  return sizeof(PRELOADED) / sizeof(PRELOADED[0]);
}

void vm_to_string(const VM *vm, Element elt, FILE *target);
void execute_tget(VM *vm, Ins ins, Element tuple, int64_t index);

Element current_block(const VM *vm) {
  return obj_get_field(vm->root, CURRENT_BLOCK);
}

uint32_t vm_get_ip(const VM *vm) {
  ASSERT_NOT_NULL(vm);
  Element block = current_block(vm);
  ASSERT(OBJECT == block.type);
  Element ip = obj_get_field(block, IP_FIELD);
  ASSERT(VALUE == ip.type, INT == ip.val.type);
  return (uint32_t) ip.val.int_val;
}

void vm_set_ip(VM *vm, uint32_t ip) {
  ASSERT_NOT_NULL(vm);
  Element block = current_block(vm);
  ASSERT(OBJECT == block.type);
  memory_graph_set_field(vm->graph, block, IP_FIELD, create_int(ip));
}

Element vm_get_module(const VM *vm) {
  ASSERT_NOT_NULL(vm);
  Element block = current_block(vm);
  ASSERT(OBJECT == block.type);
  Element module = obj_get_field(block, MODULE_FIELD);
  ASSERT(OBJECT == module.type, MODULE == module.obj->type);
  return module;
}

void vm_throw_error(VM *vm, Ins ins, const char fmt[], ...) {
  fflush(stdout);
  fflush(stderr);
  va_list args;
  va_start(args, fmt);
  char buffer[1024];
  vsprintf(buffer, fmt, args);
  va_end(args);
  Element error_msg = string_create(vm, buffer);
  Element error_module = vm_lookup_module(vm, strings_intern("error"));
  Element error_class = obj_get_field(error_module, strings_intern("Error"));
  Element curr_block = current_block(vm);
  memory_graph_set_field(vm->graph, curr_block, ERROR_KEY, create_int(1));
  vm_set_resval(vm, error_msg);
  call_new(vm, error_class);
}

void catch_error(VM *vm) {
  Element curr_block = create_none();
  Element catch_goto = create_none();
  while (true) {
    curr_block = current_block(vm);
    if (curr_block.type == NONE) {
      break;
    }
    catch_goto = obj_get_field(curr_block, strings_intern("$try_goto"));
    if (catch_goto.type != NONE) {
      break;
    }
    if (0 == Array_size(vm->saved_blocks.obj->array)) {
      break;
    }
    vm_back(vm);
  }
  if (catch_goto.type == NONE) {
    Element error_module = vm_lookup_module(vm, strings_intern("error"));
    Element raise_error = obj_get_field(error_module,
        strings_intern("raise_error"));
    // Simulate instruction advance.
//    vm_shift_ip(vm, 1);
    call_fn(vm, error_module, raise_error);
    vm_shift_ip(vm, 1);
    return;
  }
  vm_set_ip(vm, catch_goto.val.int_val);
  memory_graph_set_field(vm->graph, current_block(vm), ERROR_KEY,
      create_none());
}

Element vm_lookup_module(const VM *vm, const char module_name[]) {
  ASSERT(NOT_NULL(vm), NOT_NULL(module_name));
  Element module = obj_get_field(vm->modules, module_name);
  ASSERT(NONE != module.type);
  return module;
}

void vm_set_module(VM *vm, Element module_element, uint32_t ip) {
  ASSERT_NOT_NULL(vm);
  Element block = current_block(vm);
  ASSERT(OBJECT == block.type);
  memory_graph_set_field(vm->graph, block, MODULE_FIELD, module_element);
  memory_graph_set_field(vm->graph, block, IP_FIELD, create_int(ip));
}

void vm_shift_ip(VM *vm, int num_ins) {
  ASSERT_NOT_NULL(vm);
  Element block = current_block(vm);
  ASSERT(OBJECT == block.type);
  memory_graph_set_field(vm->graph, block, IP_FIELD,
      create_int(vm_get_ip(vm) + num_ins));
}

Ins vm_current_ins(const VM *vm) {
  ASSERT_NOT_NULL(vm);
  const Module *m = vm_get_module(vm).obj->module;
  uint32_t i = vm_get_ip(vm);
  ASSERT(NOT_NULL(m), i >= 0, i < module_size(m));
  return module_ins(m, i);
}

void vm_add_string_class(VM *vm) {
  merge_string_class(vm, class_string);
}

void vm_add_builtin(VM *vm) {
  Module *builtin = load_fn(strings_intern(BUILTIN_SRC), vm->store);
  ASSERT(NOT_NULL(vm), NOT_NULL(builtin));
  Element builtin_element = create_module(vm, builtin);
  memory_graph_set_field(vm->graph, vm->modules, module_name(builtin),
      builtin_element);
  memory_graph_set_field(vm->graph, builtin_element, PARENT, vm->root);
  memory_graph_set_field(vm->graph, builtin_element, INITIALIZED,
      element_false(vm));

  add_builtin_external(vm, builtin_element);

  void add_ref(Pair *pair) {
    Element function = create_function(vm, builtin_element,
        (uint32_t) pair->value, pair->key);
    memory_graph_set_field(vm->graph, builtin_element, pair->key, function);
    memory_graph_set_field(vm->graph, vm->root, pair->key, function);
  }
  map_iterate(module_refs(builtin), add_ref);
  void add_class(Pair *pair) {
    Element class;
    if ((class = obj_get_field(vm->root, pair->key)).type == NONE) {
      class = class_create(vm, pair->key, class_object);
    }
    memory_graph_set_field(vm->graph, builtin_element, pair->key, class);
    memory_graph_set_field(vm->graph, class, PARENT_MODULE, builtin_element);
    void add_method(Pair *pair2) {
      memory_graph_set_field(vm->graph, class, pair2->key,
          create_method(vm, builtin_element, (uint32_t) pair2->value, class,
              pair2->key));
    }
    map_iterate(pair->value, add_method);
  }
  map_iterate(module_classes(builtin), add_class);
}

Element vm_add_module(VM *vm, const Module *module) {
  ASSERT(NOT_NULL(vm), NOT_NULL(module));
  ASSERT(NONE == obj_get_field(vm->modules, module_name(module)).type);
  Element module_element = create_module(vm, module);
  memory_graph_set_field(vm->graph, vm->modules, module_name(module),
      module_element);
  memory_graph_set_field(vm->graph, module_element, PARENT, vm->root);
  memory_graph_set_field(vm->graph, module_element, INITIALIZED,
      element_false(vm));

  void add_ref(Pair *pair) {
    memory_graph_set_field(vm->graph, module_element, pair->key,
        create_function(vm, module_element, (uint32_t) pair->value, pair->key));
  }
  map_iterate(module_refs(module), add_ref);
  void add_class(Pair *pair) {
    ////DEBUGF("add_class %s", pair->key);
    Expando *parents = map_lookup(module_class_parents(module), pair->key);
    Element class;
    if (NULL == parents) {
      class = class_create(vm, pair->key, class_object);
    } else {
      Expando *parent_classes = expando(Object *, expando_len(parents));
      Element parent;
      if (parents != NULL) {
        void get_parent(void *ptr) {
          parent = obj_get_field(module_element, *((char **) ptr));
          ASSERT(NONE != parent.type);
          expando_append(parent_classes, &parent.obj);
        }
        expando_iterate(parents, get_parent);
      } else {
        parent = class_object;
      }
      class = class_create_list(vm, pair->key, parent_classes);
      expando_delete(parent_classes);
    }
    memory_graph_set_field(vm->graph, module_element, pair->key, class);
    void add_method(Pair *pair2) {
      memory_graph_set_field(vm->graph, class, pair2->key,
          create_function(vm, module_element, (uint32_t) pair2->value,
              pair->key));
    }
    map_iterate(pair->value, add_method);
  }
  map_iterate(module_classes(module), add_class);
  return module_element;
}

void vm_merge_module(VM *vm, const char fn[]) {
  Module *module = load_fn(strings_intern(fn), vm->store);
  ASSERT(NOT_NULL(vm), NOT_NULL(module));
  Element module_element = create_module(vm, module);
  memory_graph_set_field(vm->graph, vm->modules, module_name(module),
      module_element);
  memory_graph_set_field(vm->graph, module_element, PARENT, vm->root);
  memory_graph_set_field(vm->graph, module_element, INITIALIZED,
      element_false(vm));

  if (starts_with(fn, IO_SRC)) {
    add_io_external(vm, module_element);
  }

  void add_ref(Pair *pair) {
    Element function = create_function(vm, module_element,
        (uint32_t) pair->value, pair->key);
    memory_graph_set_field(vm->graph, module_element, pair->key, function);
  }
  map_iterate(module_refs(module), add_ref);
  void add_class(Pair *pair) {
    Element class;
    if ((class = obj_get_field(vm->root, pair->key)).type == NONE) {
      class = class_create(vm, pair->key, class_object);
    }
    memory_graph_set_field(vm->graph, module_element, pair->key, class);
    void add_method(Pair *pair2) {
      memory_graph_set_field(vm->graph, class, pair2->key,
          create_method(vm, module_element, (uint32_t) pair2->value, class,
              pair2->key));
    }
    map_iterate(pair->value, add_method);
  }
  map_iterate(module_classes(module), add_class);
}

VM *vm_create(ArgStore *store) {
  VM *vm = ALLOC(VM);
  vm->store = store;
  vm->graph = memory_graph_create();
  vm->root = memory_graph_create_root_element(vm->graph);
  class_init(vm);
  vm_add_string_class(vm);

  memory_graph_set_field(vm->graph, vm->root, ROOT, vm->root);
  memory_graph_set_field(vm->graph, vm->root, CURRENT_BLOCK, vm->root);
  memory_graph_set_field(vm->graph, vm->root, THIS, vm->root);
  memory_graph_set_field(vm->graph, vm->root, RESULT_VAL, create_none());
  memory_graph_set_field(vm->graph, vm->root, OLD_RESVALS,
      create_array(vm->graph));
  memory_graph_set_field(vm->graph, vm->root, STACK,
      (vm->stack = create_array(vm->graph)));
  memory_graph_set_field(vm->graph, vm->root, SAVED_BLOCKS, (vm->saved_blocks =
      create_array(vm->graph)));
  memory_graph_set_field(vm->graph, vm->root, MODULES, (vm->modules =
      create_obj(vm->graph)));
  memory_graph_set_field(vm->graph, vm->root, NIL_KEYWORD, create_none());
  memory_graph_set_field(vm->graph, vm->root, FALSE_KEYWORD, create_none());
  memory_graph_set_field(vm->graph, vm->root, TRUE_KEYWORD, create_int(1));

  vm_add_builtin(vm);
  int i;
  for (i = 0; i < preloaded_size(); ++i) {
    vm_merge_module(vm, PRELOADED[i]);
  }
  return vm;
}

void vm_delete(VM *vm) {
  ASSERT_NOT_NULL(vm->graph);
  void delete_module(Pair *kv) {
    ASSERT(NOT_NULL(kv));
    Element *e = ((Element *) kv->value);
    if (e->type != OBJECT || e->obj->type != MODULE) {
      return;
    }
    Module *module = (Module*) e->obj->module;
    module_delete(module);
  }
  map_iterate(&vm->modules.obj->fields, delete_module);
  memory_graph_delete(vm->graph);
  vm->graph = NULL;
  DEALLOC(vm);
}

void vm_pushstack(VM *vm, Element element) {
  ASSERT_NOT_NULL(vm);
  ASSERT(vm->stack.type == OBJECT);
  ASSERT(vm->stack.obj->type == ARRAY);
  ASSERT_NOT_NULL(vm->stack.obj->array);
  memory_graph_array_enqueue(vm->graph, vm->stack, element);
}

Element vm_popstack(VM *vm) {
  ASSERT_NOT_NULL(vm);
  ASSERT(vm->stack.type == OBJECT);
  ASSERT(vm->stack.obj->type == ARRAY);
  ASSERT_NOT_NULL(vm->stack.obj->array);
  ASSERT(!Array_is_empty(vm->stack.obj->array));
  return memory_graph_array_dequeue(vm->graph, vm->stack);
}

Element vm_peekstack(VM *vm, int distance) {
  ASSERT_NOT_NULL(vm);
  ASSERT(vm->stack.type == OBJECT);
  ASSERT(vm->stack.obj->type == ARRAY);
  Array *array = vm->stack.obj->array;
  ASSERT_NOT_NULL(array);
  ASSERT(!Array_is_empty(array), (Array_size(array) - 1 - distance) >= 0);
  return Array_get(array, Array_size(array) - 1 - distance);
}

Element vm_new_block(VM *vm, Element parent, Element new_this) {
  ASSERT_NOT_NULL(vm);
  ASSERT(OBJECT == new_this.type);
  ASSERT_NOT_NULL(new_this.obj);
  Element old_block = current_block(vm);

  memory_graph_array_push(vm->graph, vm->saved_blocks, old_block);

  Element new_block = create_obj(vm->graph);

  Element module = vm_get_module(vm);
  uint32_t ip = vm_get_ip(vm);
//  Element resval = vm_get_resval(vm);
  memory_graph_set_field(vm->graph, vm->root, CURRENT_BLOCK, new_block);
  memory_graph_set_field(vm->graph, new_block, PARENT, parent);
  memory_graph_set_field(vm->graph, new_block, THIS, new_this);
//  memory_graph_set_field(vm->graph, new_block, RESULT_VAL, resval);
  vm_set_module(vm, module, ip);
  return new_block;
}

void vm_back(VM *vm) {
  ASSERT_NOT_NULL(vm);
  Element parent_block = memory_graph_array_pop(vm->graph, vm->saved_blocks);
  memory_graph_set_field(vm->graph, vm->root, CURRENT_BLOCK, parent_block);
}

const MemoryGraph *vm_get_graph(const VM *vm) {
  return vm->graph;
}

Element vm_object_lookup(VM *vm, Element obj, const char name[]) {
  if (OBJECT != obj.type) {
    return create_none();
  }
  return obj_deep_lookup(obj.obj, name);
}

Element vm_object_get(VM *vm, const char name[], bool *has_error) {
  Element resval = vm_get_resval(vm);
  if (OBJECT != resval.type) {
    vm_throw_error(vm, vm_current_ins(vm), "Cannot get field '%s' from Nil.",
        name);
    *has_error = true;
    return create_none();
//    ERROR("Cannot access field(%s) of Nil.", name);
  }
  return vm_object_lookup(vm, resval, name);
}

Element vm_lookup(VM *vm, const char name[]) {
//////DEBUGF("Looking for '%s'", name);
  Element block = current_block(vm);
  Element lookup;
  while (NONE != block.type
      && NONE == (lookup = obj_get_field(block, name)).type) {
    block = obj_get_field(block, PARENT);
    //////DEBUGF("Looking for '%s'", name);
  }
  if (NONE == block.type) {
    return create_none();
  }
  return lookup;
}

void vm_set_resval(VM *vm, const Element elt) {
//  elt_to_str(elt, stdout);
//  printf(" <-- " RESULT_VAL "\n");
//  fflush(stdout);
  memory_graph_set_field(vm->graph, vm->root, RESULT_VAL, elt);
}

const Element vm_get_resval(VM *vm) {
  return obj_get_field(vm->root, RESULT_VAL);
}

const Element vm_get_old_resvals(VM *vm) {
  return obj_get_field(vm->root, OLD_RESVALS);
}

void call_external_fn(VM *vm, Element obj, Element external_func) {
  if (external_func.type != OBJECT
      || external_func.obj->type != EXTERNAL_FUNCTION) {
    vm_throw_error(vm, vm_current_ins(vm),
        "Cannot call ExternalFunction on something not ExternalFunction.");
    return;
  }
  ASSERT(NOT_NULL(external_func.obj->external_fn));
  Element resval = vm_get_resval(vm);
  ExternalData *ed = (obj.obj->is_external) ? obj.obj->external_data : NULL;
  Element returned = external_func.obj->external_fn(vm, ed, resval);
  vm_set_resval(vm, returned);
}

void call_fn(VM *vm, Element obj, Element func) {
  if (func.type != OBJECT
      || (func.obj->type != FUNCTION && func.obj->type != EXTERNAL_FUNCTION)) {
    vm_throw_error(vm, vm_current_ins(vm),
        "Attempted to call something not a function of Class.");
    return;
  }
  if (func.obj->type == EXTERNAL_FUNCTION) {
    call_external_fn(vm, obj, func);
    return;
  }
  Element parent = obj_get_field(func, PARENT_MODULE);
  ASSERT(OBJECT == parent.type, MODULE == parent.obj->type);
  vm_maybe_initialize_and_execute(vm, parent.obj->module);

  Element new_block = vm_new_block(vm, parent, obj);
  memory_graph_set_field(vm->graph, new_block, CALLER_KEY, func);
//////DEBUGF("new_line=%d", obj_get_field(elt, INS_INDEX).val.int_val);
  vm_set_module(vm, parent, obj_get_field(func, INS_INDEX).val.int_val - 1);
}

void call_new(VM *vm, Element class) {
  ////DEBUGF("call_new");
  ASSERT(ISCLASS(class));
  Element new_obj;
  if (NONE != obj_get_field(class, IS_EXTERNAL_KEY).type) {
    new_obj = create_external_obj(vm, class);
  } else {
    new_obj = create_obj_of_class(vm->graph, class);
  }
  Element new_func = obj_get_field(class, CONSTRUCTOR_KEY);
  if (new_func.type == OBJECT
      && (new_func.obj->type == FUNCTION
          || new_func.obj->type == EXTERNAL_FUNCTION)) {
    call_fn(vm, new_obj, new_func);
  } else {
    vm_set_resval(vm, new_obj);
  }
}

void vm_to_string(const VM *vm, Element elt, FILE *target) {
//  if (OBJECT == elt.type) {
//    Element to_s = vm_object_lookup(vm, elt, "to_s");
//    if (OBJECT == to_s.type && FUNCTION == to_s.obj->type) {
//      // Since we need to call prnt again
//      vm_shift_ip(vm, -1);
//      call_fn(vm, elt, to_s);
//      return;
//    }
//  }
  elt_to_str(elt, target);
}

bool execute_no_param(VM *vm, Ins ins) {
  Element elt, index, new_val, class;
  switch (ins.op) {
  case NOP:
    return true;
  case EXIT:
//    vm_set_resval(vm, vm_popstack(vm));
    fflush(stdout);
    fflush(stderr);
    return false;
  case RAIS:
    memory_graph_set_field(vm->graph, current_block(vm), ERROR_KEY,
        create_int(1));
    catch_error(vm);
    // Counter shift forward at end of instruction execution.
    vm_shift_ip(vm, -2);
    return true;
  case PUSH:
    vm_pushstack(vm, (Element) vm_get_resval(vm));
    return true;
  case CALL:
    elt = vm_popstack(vm);
    if (elt.type == OBJECT
        && (elt.obj->type == FUNCTION || elt.obj->type == EXTERNAL_FUNCTION)) {
      call_fn(vm, vm_lookup(vm, THIS), elt);
    } else if (elt.type == OBJECT && elt.obj->type == OBJ
        && class_class.obj == obj_get_field(elt, CLASS_KEY).obj) {
      call_new(vm, elt);
    } else {
      vm_throw_error(vm, ins,
          "Cannot execute call something not a Function or Class.");
      return true;
    }
    return true;
  case ASET:
    elt = vm_popstack(vm);
    new_val = vm_popstack(vm);
    if (elt.type != OBJECT) {
      vm_throw_error(vm, ins,
          "Cannot perform array operation on something not an Object.");
      return true;
    }
    index = vm_get_resval(vm);
    if (elt.obj->type == ARRAY) {
      if (index.type != VALUE || index.val.type != INT) {
        vm_throw_error(vm, ins,
            "Cannot index an array with something not an int.");
        return true;
      }
      memory_graph_array_set(vm->graph, elt, index.val.int_val, new_val);
    } else {
      Element set_fn = vm_object_lookup(vm, elt, ARRAYLIKE_SET_KEY);
      if (NONE == set_fn.type) {
        vm_throw_error(vm, ins,
            "Cannot perform array operation on something not Arraylike.");
        return true;
      }
      Element args = create_tuple(vm->graph);
      memory_graph_tuple_add(vm->graph, args, index);
      memory_graph_tuple_add(vm->graph, args, new_val);
      vm_set_resval(vm, args);
      call_fn(vm, elt, set_fn);
      vm_set_resval(vm, new_val);
    }
    return true;
  case AIDX:
    elt = vm_popstack(vm);
    index = vm_get_resval(vm);
    if (elt.type != OBJECT) {
      vm_throw_error(vm, ins, "Indexing on something not Arraylike.");
      return true;
    }
    if (elt.obj->type == TUPLE || elt.obj->type == ARRAY) {
      if (index.type != VALUE || index.val.type != INT) {
        vm_throw_error(vm, ins, "Array indexing with something not an int.");
        return true;
      }
      if (elt.obj->type == TUPLE) {
        execute_tget(vm, ins, elt, index.val.int_val);
      } else {
        if (index.val.int_val < 0
            || index.val.int_val >= Array_size(elt.obj->array)) {
          vm_throw_error(vm, ins,
              "Array Index out of bounds. Index=%d, Array.len=%d.",
              index.val.int_val, Array_size(elt.obj->array));
          return true;
        }
        vm_set_resval(vm, Array_get(elt.obj->array, index.val.int_val));
      }
      return true;
    } else {
      Element index_fn = vm_object_lookup(vm, elt, ARRAYLIKE_INDEX_KEY);
      if (NONE == index_fn.type) {
        vm_throw_error(vm, ins,
            "Cannot perform array operation on something not Arraylike.");
        return true;
      }
      vm_set_resval(vm, index);
      call_fn(vm, elt, index_fn);
    }
    return true;
  case RES:
    vm_set_resval(vm, vm_popstack(vm));
    return true;
  case PEEK:
    vm_set_resval(vm, vm_peekstack(vm, 0));
    return true;
  case DUP:
    elt = vm_peekstack(vm, 0);
    vm_pushstack(vm, elt);
    return true;
  case RET:
    vm_back(vm);
    return true;
  case PRNT:
    elt = vm_get_resval(vm);
    vm_to_string(vm, elt, stdout);
    if (DBG) {
      fflush(stdout);
    }
    return true;
  case ANEW:
    vm_set_resval(vm, create_array(vm->graph));
    return true;
  case NOTC:
    vm_set_resval(vm, operator_notc(vm_get_resval(vm)));
    return true;
  case NOT:
    vm_set_resval(vm, element_not(vm, vm_get_resval(vm)));
    return true;
  case ADR:
    elt = vm_get_resval(vm);
    if (OBJECT != elt.type) {
      vm_throw_error(vm, ins, "Cannot get the address of a non-object.");
      return true;
    }
    vm_set_resval(vm, create_int((int32_t) elt.obj));
    return true;
  default:
    break;
  }

  Element res;
  Element rhs = vm_popstack(vm);
  Element lhs = vm_popstack(vm);
  switch (ins.op) {
  case ADD:
    if ( ISTYPE(lhs, class_string) && ISTYPE(rhs, class_string)) {
      res = string_add(vm, lhs, rhs);
      break;
    }
    res = operator_add(lhs, rhs);
    break;
  case SUB:
    res = operator_sub(lhs, rhs);
    break;
  case MULT:
    res = operator_mult(lhs, rhs);
    break;
  case DIV:
    res = operator_div(lhs, rhs);
    break;
  case MOD:
    res = operator_mod(lhs, rhs);
    break;
  case EQ:
    res = operator_eq(vm, lhs, rhs);
    break;
  case NEQ:
    res = operator_neq(vm, lhs, rhs);
    break;
  case GT:
    res = operator_gt(vm, lhs, rhs);
    break;
  case LT:
    res = operator_lt(vm, lhs, rhs);
    break;
  case GTE:
    res = operator_gte(vm, lhs, rhs);
    break;
  case LTE:
    res = operator_lte(vm, lhs, rhs);
    break;
  case AND:
    res = operator_and(vm, lhs, rhs);
    break;
  case OR:
    res = operator_or(vm, lhs, rhs);
    break;
  case XOR:
    res = operator_or(vm, operator_and(vm, lhs, element_not(vm, rhs)),
        operator_and(vm, element_not(vm, lhs), rhs));
    break;
  case IS:
    if (!ISCLASS(rhs)) {
      vm_throw_error(vm, ins,
          "Cannot perfom type-check against a non-object type.");
      return true;
    }
    if (lhs.type != OBJECT) {
      res = element_false(vm);
      break;
    }
    class = obj_get_field(lhs, CLASS_KEY);
    if (inherits_from(class, rhs)) {
//      ////DEBUGF("TRUE");
      res = element_true(vm);
    } else {
//      ////DEBUGF("FALSE");
      res = element_false(vm);
    }
    break;
  default:
    ERROR("Instruction op was not a no_param");
  }
  vm_set_resval(vm, res);

  return true;
}

bool execute_id_param(VM *vm, Ins ins) {
  ASSERT_NOT_NULL(ins.str);
  Element block = current_block(vm);
  Element module, new_res_val, obj;
  int32_t ip;
  bool has_error = false;
  switch (ins.op) {
  case SET:
    memory_graph_set_field(vm->graph, block, ins.str, vm_get_resval(vm));
    break;
  case MDST:
    memory_graph_set_field(vm->graph, vm_get_module(vm), ins.str,
        vm_get_resval(vm));
    break;
  case FLD:
    new_res_val = vm_popstack(vm);
    memory_graph_set_field(vm->graph, vm_get_resval(vm), ins.str, new_res_val);
    vm_set_resval(vm, new_res_val);
    break;
  case PUSH:
    vm_pushstack(vm, vm_lookup(vm, ins.str));
    break;
  case PSRS:
    new_res_val = vm_lookup(vm, ins.str);
    vm_pushstack(vm, new_res_val);
    vm_set_resval(vm, new_res_val);
    break;
  case RES:
    vm_set_resval(vm, vm_lookup(vm, ins.str));
    break;
  case GET:
    new_res_val = vm_object_get(vm, ins.str, &has_error);
    if (!has_error) {
      vm_set_resval(vm, new_res_val);
    }
    break;
  case GTSH:
    vm_pushstack(vm, vm_object_get(vm, ins.str, &has_error));
    break;
  case INC:
    new_res_val = vm_object_get(vm, ins.str, &has_error);
    if (has_error) {
      return true;
    }
    if (VALUE != new_res_val.type) {
      vm_throw_error(vm, ins,
          "Cannot increment '%s' because it is not a value-type.", ins.str);
      return true;
    }
    switch (new_res_val.val.type) {
    case INT:
      new_res_val.val.int_val++;
      break;
    case FLOAT:
      new_res_val.val.float_val++;
      break;
    case CHAR:
      new_res_val.val.char_val++;
      break;
    }
    memory_graph_set_field(vm->graph, block, ins.str, new_res_val);
    vm_set_resval(vm, new_res_val);
    break;
  case DEC:
    new_res_val = vm_object_get(vm, ins.str, &has_error);
    if (has_error) {
      return true;
    }
    if (VALUE != new_res_val.type) {
      vm_throw_error(vm, ins,
          "Cannot increment '%s' because it is not a value-type.", ins.str);
      return true;
    }
    switch (new_res_val.val.type) {
    case INT:
      new_res_val.val.int_val--;
      break;
    case FLOAT:
      new_res_val.val.float_val--;
      break;
    case CHAR:
      new_res_val.val.char_val--;
      break;
    }
    memory_graph_set_field(vm->graph, block, ins.str, new_res_val);
    vm_set_resval(vm, new_res_val);
    break;
  case CALL:
    obj = vm_popstack(vm);
    if (obj.type == NONE) {
      //DEBUGF("CALL NONE");
      vm_throw_error(vm, ins, "Cannot deference Nil.");
      return true;
    }
    if (obj.type != OBJECT) {
      //DEBUGF("CALL NON-OBJECT");
      vm_throw_error(vm, ins, "Cannot call a non-object.");
      return true;
    }
    Element target = vm_object_lookup(vm, obj, ins.str);
    if (OBJECT != target.type) {
      elt_to_str(obj_get_field(obj, CLASS_KEY), stdout);
      printf("\n");
      fflush(stdout);
      //DEBUGF("CALL NO SUCH FUNCTION");
      vm_throw_error(vm, ins, "Object has no such function '%s'.", ins.str);
      return true;
    }
    if (target.obj->type == FUNCTION || target.obj->type == EXTERNAL_FUNCTION) {
      call_fn(vm, obj, target);
    } else if (target.obj->type == OBJ
        && class_class.obj == obj_get_field(target, CLASS_KEY).obj) {
      call_new(vm, target);
    } else {
      //DEBUGF("CALL SOMETHING ELSE");
      vm_throw_error(vm, ins,
          "Cannot execute call something not a Function or Class.");
      return true;
    }
    break;
  case RMDL:
    module = vm_lookup_module(vm, ins.str);
    vm_set_resval(vm, module);
    break;
  case MCLL:
    module = vm_popstack(vm);
    ASSERT(OBJECT == module.type, MODULE == module.obj->type)
    ;
    vm_new_block(vm, block, block);
    ip = module_ref(module.obj->module, ins.str);
    ASSERT(ip > 0);
    vm_set_module(vm, module, ip - 1);
    break;
  case PRNT:
    elt_to_str(vm_lookup(vm, ins.str), stdout);
    if (DBG) {
      fflush(stdout);
    }
    break;
  default:
    ERROR("Instruction op was not a id_param");
  }
  return true;
}

void execute_tget(VM *vm, Ins ins, Element tuple, int64_t index) {
  if (tuple.type != OBJECT || tuple.obj->type != TUPLE) {
    vm_throw_error(vm, ins, "Attempted to index something not a tuple.");
    return;
  }
  if (index < 0 || index >= tuple_size(tuple.obj->tuple)) {
    vm_throw_error(vm, ins,
        "Tuple Index out of bounds. Index=%d, Tuple.len=%d.", index,
        tuple_size(tuple.obj->tuple));
    return;
  }
  vm_set_resval(vm, tuple_get(tuple.obj->tuple, index));
}

bool execute_val_param(VM *vm, Ins ins) {
  Element elt = val_to_elt(ins.val);
  Element tuple, array;
  int i;
  switch (ins.op) {
  case EXIT:
    vm_set_resval(vm, elt);
    return false;
  case RES:
    vm_set_resval(vm, elt);
    break;
  case PUSH:
    vm_pushstack(vm, elt);
    break;
  case PEEK:
    vm_set_resval(vm, vm_peekstack(vm, elt.val.int_val));
    break;
  case SINC:
    vm_pushstack(vm, create_int(vm_popstack(vm).val.int_val + elt.val.int_val));
    break;
  case TUPL:
    ASSERT(elt.type == VALUE, elt.val.type == INT)
    ;
    tuple = create_tuple(vm->graph);
    vm_set_resval(vm, tuple);
    for (i = 0; i < elt.val.int_val; i++) {
      memory_graph_tuple_add(vm->graph, tuple, vm_popstack(vm));
    }
    break;
  case TGET:
    if (elt.type != VALUE || elt.val.type != INT) {
      vm_throw_error(vm, ins,
          "Attempted to index a tuple with something not an int.");
      return true;
    }
    tuple = vm_get_resval(vm);
    execute_tget(vm, ins, tuple, elt.val.int_val);
    break;
  case JMP:
    ASSERT(elt.type == VALUE, elt.val.type == INT)
    ;
    vm_shift_ip(vm, elt.val.int_val);
    break;
  case IF:
    ASSERT(elt.type == VALUE, elt.val.type == INT)
    ;
    if (NONE != vm_get_resval(vm).type) {
      vm_shift_ip(vm, elt.val.int_val);
    }
    break;
  case IFN:
    ASSERT(elt.type == VALUE, elt.val.type == INT)
    ;
    if (NONE == vm_get_resval(vm).type) {
      vm_shift_ip(vm, elt.val.int_val);
    }
    break;
  case PRNT:
    vm_to_string(vm, elt, stdout);
    if (DBG) {
      fflush(stdout);
    }
    break;
  case ANEW:
    ASSERT(elt.type == VALUE, elt.val.type == INT)
    ;
    array = create_array(vm->graph);
    vm_set_resval(vm, array);
    for (i = 0; i < elt.val.int_val; i++) {
      memory_graph_array_enqueue(vm->graph, array, vm_popstack(vm));
    }
    break;
  case CTCH:
    ASSERT(elt.type == VALUE, elt.val.type == INT)
    ;
    uint32_t ip = vm_get_ip(vm);
    memory_graph_set_field(vm->graph, current_block(vm),
        strings_intern("$try_goto"), create_int(ip + elt.val.int_val + 1));
    break;
  default:
    ERROR("Instruction op was not a val_param. op=%s",
        instructions[(int ) ins.op]);
  }
  return true;
}

//bool execute_goto_param(VM *vm, Ins ins) {
//  Element block = obj_get_field(vm->root, CURRENT_BLOCK);
//  switch (ins.op) {
//  case CALL:
//    vm_new_block(vm, block, block);
//    vm_set_ip(vm, ins.go_to);
//    break;
//  case GOTO:
//    vm_set_ip(vm, ins.go_to);
//    break;
//  default:
//    ERROR("Instruction op was not a goto_param. op=%s", instructions[ins.op]);
//  }
//  return true;
//}

bool execute_str_param(VM *vm, Ins ins) {
  Element str_array = string_create(vm, ins.str);
  switch (ins.op) {
  case PUSH:
    vm_pushstack(vm, str_array);
    break;
  case RES:
    vm_set_resval(vm, str_array);
    break;
  case PRNT:
    elt_to_str(str_array, stdout);
    if (DBG) {
      fflush(stdout);
    }
    break;
  case PSRS:
    vm_pushstack(vm, str_array);
    vm_set_resval(vm, str_array);
    break;
  default:
    ERROR("Instruction op was not a str_param. op=%s",
        instructions[(int ) ins.op]);
  }
  return true;
}

// returns whether or not the program should continue
bool execute(VM *vm) {
  ASSERT_NOT_NULL(vm);
  Element has_error = obj_get_field(current_block(vm), ERROR_KEY);
  if (NONE != has_error.type) {
//    ////DEBUGF("catch_error");
    catch_error(vm);
    return true;
  }

  Ins ins = vm_current_ins(vm);
//  fprintf(stdout, "module(%s) ", module_name(vm_get_module(vm).obj->module));
//  fflush(stdout);
//  ins_to_str(ins, stdout);
//  fprintf(stdout, "\n");
//  fflush(stdout);

  bool status;
  switch (ins.param) {
  case ID_PARAM:
    status = execute_id_param(vm, ins);
    break;
  case VAL_PARAM:
    status = execute_val_param(vm, ins);
    break;
//  case GOTO_PARAM:
//    status = execute_goto_param(vm, ins);
//    break;
  case STR_PARAM:
    status = execute_str_param(vm, ins);
    break;
  default:
    status = execute_no_param(vm, ins);
  }
  vm_shift_ip(vm, 1);
  return status;
}

void vm_maybe_initialize_and_execute(VM *vm, const Module *module) {
  Element module_element = vm_lookup_module(vm, module_name(module));
  if (is_true(obj_get_field(module_element, INITIALIZED))) {
    return;
  }
  memory_graph_set_field(vm->graph, module_element, INITIALIZED,
      element_true(vm));

  memory_graph_array_push(vm->graph, vm_get_old_resvals(vm), vm_get_resval(vm));

  ASSERT(NONE != module_element.type);
  vm_new_block(vm, module_element, module_element);
  vm_set_module(vm, module_element, 0);
//  int i = 0;
  while (execute(vm)) {
//    ////DEBUGF("EXECUTING INSTRUCTION #%d", i);
//    if (0 == ((i+1) % 100)) {
//      ////DEBUGF("FREEING SPACE YAY (%d)", i);
//      memory_graph_free_space(vm->graph);
//      ////DEBUGF("DONE FREEING SPACE", i);
//    }
//    i++;
  }
  vm_back(vm);

  vm_set_resval(vm, memory_graph_array_pop(vm->graph, vm_get_old_resvals(vm)));
}

