/*
 * ops.c
 *
 *  Created on: Dec 11, 2016
 *      Author: Jeff
 */

#include "ops.h"

#include "../element.h"
#include "../vm/vm.h"

OPFUNC(+, add)
OPFUNC(-, sub)
OPFUNC(*, mult)
OPFUNC(/, div)
OPFUNC_INTTYPE(%, mod)
OPFUNC_BOOLTYPE(==, eq)
OPFUNC_BOOLTYPE(!=, neq)
OPFUNC_BOOLTYPE(>, gt)
OPFUNC_BOOLTYPE(>=, gte)
OPFUNC_BOOLTYPE(<, lt)
OPFUNC_BOOLTYPE(<=, lte)
OPFUNC_BOOLTYPE_NULLABLE(&&, and);
OPFUNC_BOOLTYPE_NULLABLE(||, or);
// OPFUNC_BOOLTYPE_SINGLE_NIL(element_not, not);
OPFUNC_BOOLTYPE_SINGLE(!, notc);
