// the trace hooks jrhost.c defines (build.sh includes this ahead of each file)
#pragma once
#include "mcs48.h"
#include "z80.h"
void jr_z80_trace(const z80_t *z);
void jr_m48_trace(const m48_t *m);
