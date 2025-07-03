
#ifndef __H2_RSURF_H
#define __H2_RSURF_H

#include "hooking.h"

#ifdef __cplusplus
extern "C" {
#endif

void h2_rsurf_init();
void h2_rsurf_deinit();

void h2_rsurf_frame_ended();

#ifdef __cplusplus
}
#endif

#endif
