
#ifndef __HEAVY_METAL_H
#define __HEAVY_METAL_H

#include "hooking.h"

#ifdef __cplusplus
extern "C" {
#endif

void hm_init();
void hm_deinit();

void hm_frame_ended();

#ifdef __cplusplus
}
#endif

#endif
