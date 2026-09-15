
#ifndef QINDIEGL_D3D_MATRIX_DETECTION_H
#define QINDIEGL_D3D_MATRIX_DETECTION_H

#include "d3d_wrapper.hpp"
#include "d3dx9.h"
#include <stdint.h>

#define MAT_DET_NUMADDR 4
#define MAT_DISP_NUMSLOTS 10
typedef struct matrix_display_s
{
	bool det_enabled;
	int det_mode;
	const void *addrs[MAT_DET_NUMADDR];
	int addr_count;
	int addr_selected;
	const void* addr_preferred;
	bool disp_enabled;
	int disp_rejects;
	float disp_threshold;
	struct mat_slot_s {
		uint8_t count;
		float matrix[16];
	} disp_slot[MAT_DISP_NUMSLOTS];
} matrix_detect_t;

bool matrix_detect_is_detection_enabled();
void matrix_detect_frame_ended();
void matrix_detect_configuration_reset();
void matrix_detect_process_upload(const float* mat, D3DXMATRIX *detected_model, D3DXMATRIX *detected_view);
void matrix_detect_on_world_retrieve(const float* mat, D3DXMATRIX *detected_model, D3DXMATRIX *detected_view);
bool matrix_detect_get_display(matrix_detect_t** out);
bool matrix_detect_are_equal(const float* a, const float* b, int count);
void matrix_print(const float* mat, int ordinal, int usage_count, unsigned int flags, const unsigned int * seq_nums, const void * const * seq_ptrs);

void matrix_print_s(const float* mat, const char* info);
void matrix_update_camera(const float* mat);
void matrix_preferred_address(const void* addr);

#endif