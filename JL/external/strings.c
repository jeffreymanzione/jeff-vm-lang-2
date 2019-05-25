/*
 * strings.c
 *
 *  Created on: Jun 3, 2018
 *      Author: Jeff
 */

#include "strings.h"

#include <inttypes.h>
#include <stdio.h>

#include "../arena/strings.h"
#include "../class.h"
#include "../codegen/tokenizer.h"
#include "../datastructure/array.h"
#include "../datastructure/arraylike.h"
#include "../datastructure/map.h"
#include "../datastructure/tuple.h"
#include "../element.h"
#include "../error.h"
#include "../graph/memory.h"
#include "../graph/memory_graph.h"
#include "../vm.h"
#include "external.h"

Element stringify__(VM *vm, Thread *t, ExternalData *ed, Element *argument) {
  ASSERT(argument->type == VALUE);
  Value val = argument->val;
  static const int BUFFER_SIZE = 128;
  char buffer[BUFFER_SIZE];
  int num_written = 0;
  switch (val.type) {
    case INT:
      num_written = snprintf(buffer, BUFFER_SIZE, "%" PRId64, val.int_val);
      break;
    case FLOAT:
      num_written = snprintf(buffer, BUFFER_SIZE, "%f", val.float_val);
      break;
    default /*CHAR*/:
      num_written = snprintf(buffer, BUFFER_SIZE, "%c", val.char_val);
      break;
  }
  ASSERT(num_written > 0);
  return string_create_len_unescape(vm, buffer, num_written);
}

IMPL_ARRAYLIKE(String, char);

const char *String_cstr(const String *const string) { return string->table; }

void String_append_unescape(String *string, const char str[], size_t len) {
  int i;
  for (i = 0; i < len; i++) {
    char c = str[i];
    if (c == '\\') {
      ++i;
      c = char_unesc(str[i]);
    }
    String_enqueue(string, c);
  }
}

void String_insert(String *string, int index_in_string, const char src[],
                   size_t len) {
  String_shift(string, index_in_string, len);
  memmove(string->table + sizeof(char) * index_in_string, src,
          sizeof(char) * len);
  string->num_elts += len;
}

void String_append_cstr(String *string, const char src[], size_t len) {
  String_insert(string, string->num_elts, src, len);
}

String *String_of(VM *vm, ExternalData *data, const char *src, size_t len) {
  ASSERT(NOT_NULL(src));
  String *string = String_create_copy(src, len);
  String_fill(vm, data, string);
  return string;
}

void String_fill(VM *vm, ExternalData *data, String *string) {
  map_insert(&data->state, STRING_NAME, string);
  memory_graph_set_field(vm->graph, data->object, LENGTH_KEY,
                         create_int(String_size(string)));
}

Element string_constructor(VM *vm, Thread *t, ExternalData *data,
                           Element *arg) {
  ASSERT(NOT_NULL(data));
  String *string;
  if (NONE == arg->type) {
    string = String_create();
    String_fill(vm, data, string);
    return data->object;
  }
  if (OBJECT != arg->type) {
    string = String_create();
    String_fill(vm, data, string);
    return throw_error(vm, t, "Non-object input to String()");
  }
  if (ISTYPE(*arg, class_string)) {
    string = String_copy(String_extract(*arg));
    String_fill(vm, data, string);
  } else if (ISTYPE(*arg, class_array)) {
    string = String_create();
    Array *arr = extract_array(*arg);
    int i;
    for (i = 0; i < Array_size(arr); ++i) {
      Element e = Array_get(arr, i);
      if (VALUE == e.type && CHAR == e.val.type) {
        String_enqueue(string, e.val.char_val);
      } else if (ISTYPE(e, class_string)) {
        String *tail = String_extract(e);
        String_append(string, tail);
      } else {
        return throw_error(vm, t, "Invalid Array input to String()");
      }
    }
    String_fill(vm, data, string);
  } else {
    return throw_error(vm, t, "Invalid Object input to String()");
  }
  return data->object;
}

Element string_deconstructor(VM *vm, Thread *t, ExternalData *data,
                             Element *arg) {
  String *string = map_lookup(&data->state, STRING_NAME);
  if (NULL != string) {
    String_delete(string);
  }
  return create_none();
}

Element string_index(VM *vm, Thread *t, ExternalData *data, Element *arg) {
  if (!is_value_type(arg, INT)) {
    return throw_error(vm, t, "Indexing String with something not an Int.");
  }
  String *string = map_lookup(&data->state, STRING_NAME);
  ASSERT(NOT_NULL(string));
  return create_char(String_get(string, arg->val.int_val));
}

Element string_find(VM *vm, Thread *t, ExternalData *data, Element *arg) {
  if (!is_object_type(arg, TUPLE)) {
    return throw_error(vm, t, "Expected more than one arg.");
  }
  Tuple *args = arg->obj->tuple;
  if (tuple_size(args) != 2) {
    return throw_error(vm, t, "Expected 2 arguments.");
  }
  Element string_arg = tuple_get(args, 0);
  Element index = tuple_get(args, 1);
  if (!ISTYPE(string_arg, class_string)) {
    return throw_error(vm, t, "Only a String can be in a String.");
  }
  if (!is_value_type(&index, INT)) {
    return throw_error(vm, t, "Expected a starting index.");
  }
  String *string = map_lookup(&data->state, STRING_NAME);
  ASSERT(NOT_NULL(string));
  String *substr = String_extract(string_arg);
  ASSERT(NOT_NULL(substr));

  if ((index.val.int_val + String_size(substr)) > String_size(string)) {
    return throw_error(vm, t, "Expected a starting index.");
  }
  char *start_index = string->table + index.val.int_val;
  size_t size_after_start = String_size(string) - index.val.int_val;

  char *found_index = find_str(start_index, size_after_start, substr->table,
                               String_size(substr));
  if (NULL == found_index) {
    return create_none();
  }
  return create_int((int64_t)(found_index - start_index));
}

Element string_set(VM *vm, Thread *t, ExternalData *data, Element *arg) {
  if (!is_object_type(arg, TUPLE)) {
    ERROR("Unknown input.");
    return create_none();
  }
  Tuple *args = arg->obj->tuple;
  Element index = tuple_get(args, 0);
  Element val = tuple_get(args, 1);
  //  elt_to_str(val, stdout);
  //  printf("\n");fflush(stdout);
  ASSERT(index.type == VALUE, index.val.type == INT);
  String *string = map_lookup(&data->state, STRING_NAME);
  ASSERT(NOT_NULL(string));
  if (val.type == VALUE && val.val.type == CHAR) {
    String_set(string, index.val.int_val, val.val.char_val);
  } else if (OBJECT == val.type &&
             obj_lookup(val.obj, CKey_class).obj == class_string.obj) {
    String_append(string, String_extract(val));
  } else {
    ERROR("BAD STRING.");
  }
  memory_graph_set_field(vm->graph, data->object, LENGTH_KEY,
                         create_int(String_size(string)));
  return create_none();
}

Element string_extend(VM *vm, Thread *t, ExternalData *data, Element *arg) {
  if (!ISTYPE(*arg, class_string)) {
    return throw_error(vm, t, "Cannot extend something not a String.");
  }
  String *head = map_lookup(&data->state, STRING_NAME);
  ASSERT(NOT_NULL(head));
  String *tail = String_extract(*arg);
  ASSERT(NOT_NULL(tail));
  String_append(head, tail);
  memory_graph_set_field(vm->graph, data->object, LENGTH_KEY,
                         create_int(String_size(head)));
  return data->object;
}

Element string_extend_range(VM *vm, Thread *t, ExternalData *data,
                            Element *arg) {
  if (!is_object_type(arg, TUPLE)) {
    return throw_error(vm, t, "Expected more than one arg.");
  }
  Tuple *args = arg->obj->tuple;
  if (tuple_size(args) != 3) {
    return throw_error(vm, t, "Expected 3 arguments.");
  }
  Element string_arg = tuple_get(args, 0);
  Element start = tuple_get(args, 1);
  Element end = tuple_get(args, 2);
  if (!ISTYPE(string_arg, class_string)) {
    return throw_error(vm, t, "Only a String can be in a String.");
  }
  if (!is_value_type(&start, INT)) {
    return throw_error(vm, t, "Expected a starting index.");
  }
  if (!is_value_type(&end, INT)) {
    return throw_error(vm, t, "Expected an ending index.");
  }
  String *string = map_lookup(&data->state, STRING_NAME);
  ASSERT(NOT_NULL(string));
  String *substr = String_extract(string_arg);
  ASSERT(NOT_NULL(substr));

  String_append_range(string, substr, start.val.int_val, end.val.int_val);
  memory_graph_set_field(vm->graph, data->object, LENGTH_KEY,
                         create_int(String_size(string)));
  return data->object;
}

String *String_extract(Element elt) {
  ASSERT(obj_lookup(elt.obj, CKey_class).obj == class_string.obj);
  String *string = map_lookup(&elt.obj->external_data->state, STRING_NAME);
  ASSERT(NOT_NULL(string));
  return string;
}

Element string_ltrim(VM *vm, Thread *t, ExternalData *data, Element *arg) {
  String *string = map_lookup(&data->state, STRING_NAME);
  ASSERT(NOT_NULL(string));
  int i = 0;
  while (is_any_space(string->table[i])) {
    ++i;
  }
  String_lshrink(string, i);
  memory_graph_set_field(vm->graph, data->object, LENGTH_KEY,
                         create_int(String_size(string)));
  return data->object;
}

Element string_rtrim(VM *vm, Thread *t, ExternalData *data, Element *arg) {
  String *string = map_lookup(&data->state, STRING_NAME);
  ASSERT(NOT_NULL(string));
  int i = 0;
  while (is_any_space(string->table[String_size(string) - 1 - i])) {
    ++i;
  }
  String_rshrink(string, i);
  memory_graph_set_field(vm->graph, data->object, LENGTH_KEY,
                         create_int(String_size(string)));
  return data->object;
}

Element string_trim(VM *vm, Thread *t, ExternalData *data, Element *arg) {
  String *string = map_lookup(&data->state, STRING_NAME);
  ASSERT(NOT_NULL(string));
  int i = 0;
  while (is_any_space(string->table[i])) {
    ++i;
  }
  String_lshrink(string, i);
  i = 0;
  while (is_any_space(string->table[String_size(string) - 1 - i])) {
    ++i;
  }
  String_rshrink(string, i);
  memory_graph_set_field(vm->graph, data->object, LENGTH_KEY,
                         create_int(String_size(string)));
  return data->object;
}

Element string_lshrink(VM *vm, Thread *t, ExternalData *data, Element *arg) {
  if (!is_value_type(arg, INT)) {
    return throw_error(vm, t, "Trimming String with something not an Int.");
  }
  String *string = map_lookup(&data->state, STRING_NAME);
  ASSERT(NOT_NULL(string));
  if (arg->val.int_val > String_size(string)) {
    return throw_error(vm, t, "Cannot shrink more than the entire size.");
  }
  String_lshrink(string, arg->val.int_val);
  memory_graph_set_field(vm->graph, data->object, LENGTH_KEY,
                         create_int(String_size(string)));
  return data->object;
}

Element string_rshrink(VM *vm, Thread *t, ExternalData *data, Element *arg) {
  if (!is_value_type(arg, INT)) {
    return throw_error(vm, t, "Trimming String with something not an Int.");
  }
  String *string = map_lookup(&data->state, STRING_NAME);
  ASSERT(NOT_NULL(string));
  if (arg->val.int_val > String_size(string)) {
    return throw_error(vm, t, "Cannot shrink more than the entire size.");
  }
  String_rshrink(string, arg->val.int_val);
  memory_graph_set_field(vm->graph, data->object, LENGTH_KEY,
                         create_int(String_size(string)));
  return data->object;
}

Element string_clear(VM *vm, Thread *t, ExternalData *data, Element *arg) {
  String *string = map_lookup(&data->state, STRING_NAME);
  ASSERT(NOT_NULL(string));
  String_clear(string);
  memory_graph_set_field(vm->graph, data->object, LENGTH_KEY,
                         create_int(String_size(string)));
  return data->object;
}

// Element string_hash(VM *vm, ExternalData *data, Element arg) {
//  String *string = map_lookup(&data->state, STRING_NAME);
//  ASSERT(NOT_NULL(string));
//  return create_int((int64_t) (int32_t) String_cstr(string));
//}

void merge_string_class(VM *vm, Element string_class) {
  merge_external_class(vm, string_class, string_constructor,
                       string_deconstructor);
  add_external_method(vm, string_class, strings_intern("__index__"),
                      string_index);
  add_external_method(vm, string_class, strings_intern("__set__"), string_set);
  add_external_method(vm, string_class, strings_intern("find__"), string_find);
  add_external_method(vm, string_class, strings_intern("extend__"),
                      string_extend);
  add_external_method(vm, string_class, strings_intern("extend_range__"),
                      string_extend_range);
  add_external_method(vm, string_class, strings_intern("trim"), string_trim);
  add_external_method(vm, string_class, strings_intern("ltrim"), string_ltrim);
  add_external_method(vm, string_class, strings_intern("rtrim"), string_rtrim);
  add_external_method(vm, string_class, strings_intern("lshrink"),
                      string_lshrink);
  add_external_method(vm, string_class, strings_intern("rshrink"),
                      string_rshrink);
  add_external_method(vm, string_class, strings_intern("clear"), string_clear);
  //  add_external_function(vm, string_class, strings_intern("hash"),
  //  string_hash);
}
