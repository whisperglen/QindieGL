
#ifndef __SURFACE_SORTING_H
#define __SURFACE_SORTING_H

#include "hooking.h"

#ifdef __cplusplus
extern "C" {
#endif

void hook_surface_sorting_do_init();
void hook_surface_sorting_do_deinit();

void hook_surface_sorting_frame_ended();


#ifdef __cplusplus
}
#endif

#endif