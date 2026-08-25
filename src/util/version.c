#include <stdio.h>
#include <sublimation.h>
#include "version.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

void version(void)
{
    mlwrite("%s VERSION %s (sublimation %s, ABI %d)",
            PROGRAM_NAME_LONG, VERSION,
            sublimation_version(), sublimation_api_version());
}
