
#ifndef __H2_REFGL_H
#define __H2_REFGL_H

#include "hooking.h"

#ifdef __cplusplus
extern "C" {
#endif

void h2_refgl_init();
void h2_refgl_deinit();

void h2_refgl_frame_ended();

#ifdef __cplusplus
}
#endif

#endif
