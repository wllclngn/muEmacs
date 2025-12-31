#include <stdio.h>
#include "version.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

void version(void)
{
    mlwrite("%s VERSION %s", PROGRAM_NAME_LONG, VERSION);
}
