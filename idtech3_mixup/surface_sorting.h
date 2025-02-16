
#ifndef __SURFACE_SORTING_H
#define __SURFACE_SORTING_H

#include "hooking.h"

#ifdef __cplusplus
extern "C" {
#endif

void hook_surface_sorting_do_init();

//intercept qsortFast to provide stable sorting of surfaces
void hook_qsortFast(void* base, unsigned num, unsigned width);




#ifdef __cplusplus
}
#endif

#endif