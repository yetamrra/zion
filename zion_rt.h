#pragma once
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include "colors.h"

/* for now let's go big and not worry about it */
typedef int64_t zion_int_t;
typedef int64_t zion_bool_t;
typedef float zion_float_t;
typedef int32_t type_id_t;

#define int do_not_use_int
#define float do_not_use_float

struct var_t;

