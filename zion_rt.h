#pragma once

/* for now let's go big and not worry about it */
typedef double zion_float_t;

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <assert.h>
#include <signal.h>
#include "colors.h"

typedef int64_t zion_int_t;
typedef int64_t zion_bool_t;
typedef int32_t type_id_t;

#define int do_not_use_int
#define float do_not_use_float
#define double do_not_use_double

struct var_t;

typedef void (*dtor_fn_t)(struct var_t *var);
typedef void (*mark_fn_t)(struct var_t *var);


struct type_info_t {
	/* the id for the type - a unique number */
	type_id_t type_id;

	/* refs_count gives the type-map for memory management/ref counting. */
	int16_t refs_count;

	/* ref_offsets is the list of offsets to managed members */
	int16_t *ref_offsets;

	/* a helpful name for this type */
	const char *name;

	/* the size of the allocation for memory profiling purposes */
	int64_t size;

	/* the destructor for this type, if one exists. NB: if you change the index
	 * of this dimension, update DTOR_INDEX */
	dtor_fn_t dtor_fn;

	/* the mark function for this type, if one exists. NB: if you change the index
	 * of this dimension, update MARK_FN_INDEX */
	mark_fn_t mark_fn;
};

