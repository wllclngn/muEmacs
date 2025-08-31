#include "usage.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "error.h"

static void report(const char* prefix, const char *err, va_list params)
{
	char msg[4096];
	vsnprintf(msg, sizeof(msg), err, params);
	REPORT_ERROR(ERR_COMMAND_UNKNOWN, msg);
}

void die(const char* err, ...)
{
	va_list params;

	va_start(params, err);
	report("fatal: ", err, params);
	va_end(params);
	exit(128);
}
