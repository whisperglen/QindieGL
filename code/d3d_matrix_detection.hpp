
#ifndef QINDIEGL_D3D_MATRIX_DETECTION_H
#define QINDIEGL_D3D_MATRIX_DETECTION_H

#include "d3d_wrapper.hpp"

#include "d3dx9.h"

bool matrix_detect_is_detection_enabled();
void matrix_detect_frame_ended();
void matrix_detect_configuration_reset();
void matrix_detect_process_upload(const float* mat, D3DXMATRIX *detected_model, D3DXMATRIX *detected_view);
void matrix_detect_on_world_retrieve(const float* mat, D3DXMATRIX *detected_model, D3DXMATRIX *detected_view);
bool matrix_detect_are_equal(const float* a, const float* b, int count);
void matrix_print(const float* mat, int ordinal, int usage_count, unsigned int flags, const unsigned int * seq_nums, const void * const * seq_ptrs);
OPENGL_API void WINAPI matrix_print_s(const float* mat, const char* info);

#endif