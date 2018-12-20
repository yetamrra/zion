#pragma once
#include <cstdlib>
#include <cctype>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <math.h>
#include <glob.h>
#include <algorithm> 
#include <locale>
#include <memory>
#include "logger_decls.h"
#include "ptr.h"
#include "zion_assert.h"
#include "builtins.h"

#define ZION 1
#define GLOBAL_SCOPE_NAME "std"

#define DEFAULT_INT_BITSIZE 64
#define DEFAULT_INT_SIGNED true
#define ZION_BITSIZE_STR "64"
#define ZION_TYPEID_BITSIZE_STR "16"

#define SCOPE_TK tk_dot
#define SCOPE_SEP_CHAR '.'
#define SCOPE_SEP "."

#define MATHY_SYMBOLS "!@#$%^&*()+-_=><.,/|[]`~\\"

