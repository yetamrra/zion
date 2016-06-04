#pragma once
#include <stdio.h>
#include "location.h"

enum log_level_t
{
	log_info = 1,
	log_warning = 2,
	log_error = 4,
	log_panic = 8,
};

void log_enable(int log_level);
void logv(log_level_t level, const char *format, va_list args);
void logv_location(log_level_t level, const location &location, const char *format, va_list args);
void log(log_level_t level, const char *format, ...);
void log_location(log_level_t level, const location &location, const char *format, ...);
void panic_(const char *filename, int line, std::string msg);
void log_stack(log_level_t level);

bool check_errno(const char *tag);
