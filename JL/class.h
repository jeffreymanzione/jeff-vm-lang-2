/*
 * class.h
 *
 *  Created on: Oct 3, 2016
 *      Author: Jeff
 */

#ifndef CLASS_H_
#define CLASS_H_

#include <stdbool.h>

#include "element.h"
#include "ltable/ltable.h"

#define ISOBJECT(elt) ((elt).type == OBJECT)
#define ISCLASS_OBJ(cobj) \
  (class_class.obj == obj_lookup((cobj), CKey_class).obj)
#define ISCLASS(elt) \
  (ISOBJECT(elt) && class_class.obj == obj_lookup((elt).obj, CKey_class).obj)
#define ISTYPE(elt, class)            \
  (ISCLASS(class) && ISOBJECT(elt) && \
   obj_lookup((elt).obj, CKey_class).obj == (class).obj)
#define ISVALUE(elt) ((elt).type == VALUE)

extern Element class_class;
extern Element class_object;
extern Element class_array;
extern Element class_string;

extern Element class_tuple;
extern Element class_function;
extern Element class_anon_function;
extern Element class_external_function;
extern Element class_method;
extern Element class_external_method;
extern Element class_methodinstance;
extern Element class_external_methodinstance;
extern Element class_module;
extern Element class_thread;

void class_init(VM *vm);
Element class_create(VM *vm, const char class_name[], Element parent_class,
                     Element module_or_class);
Element class_create_list(VM *vm, const char class_name[],
                          Expando *parent_classes, Element module_or_class);
void class_fill_list(VM *vm, Element class, const char class_name[],
                     Expando *parent_classes, Element module_or_class);

bool inherits_from(Object *class, Object *super);

#endif /* CLASS_H_ */
