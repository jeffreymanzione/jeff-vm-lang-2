/*
 * array.c
 *
 *  Created on: Sep 28, 2016
 *      Author: Jeff
 */

#include "array.h"

#include <string.h>

#include "arraylike.h"
#include "../error.h"
#include "../graph/memory.h"

IMPL_ARRAYLIKE(Array, Element);
