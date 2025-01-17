
#include "d3d_matrix_detection.hpp"
#include "d3d_helpers.hpp"
#include "d3d_wrapper.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <stdio.h>
#include <string.h>

struct matrix_log_data
{
	float mat[16];
	int usage;
	unsigned int flags;
	std::vector<unsigned int> seq_num;
	std::vector<void*> seq_ptr;
};

enum detection_status
{
	MDET_SKY_OR_CAMERA = 0,
	MDET_CAMERA = 1,
	MDET_END = 2
};

typedef struct mat_detection
{
	D3DXMATRIX mat;
	enum detection_status detected;
	int usage;

	mat_detection() : mat(), detected(MDET_END), usage(0)
	{
		D3DXMatrixIdentity(&mat);
	};
	void clear()
	{
		D3DXMatrixIdentity(&mat);
		detected = MDET_END;
		usage = 0;
	}
} mat_detection_t;

static bool matrix_is_flippingmat(const float* mat);

static struct matrix_log_data g_mat_log_data[100];
static int g_mat_log_idx = 0;
static int g_mat_log_global_count = 0;

static bool g_mat_log_print_one_round = false;

void* g_mat_addrs[3];
int g_mat_addr_count = 0;
int g_mat_addr_selected = 0;
static float g_camera_backup[16];

static float g_mat_identity[16] =
{
	1.0, 0.0, 0.0, 0.0,
	0.0, 1.0, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0
};

mat_detection_t g_mat_camera;
bool g_mat_detection_valid = true;

bool matrix_detect_is_detection_valid()
{
	return g_mat_detection_valid;
}

inline bool matrix_is_identity(const float* mat)
{
	return matrix_detect_are_equal(g_mat_identity, mat, 0);
}

inline bool matrix_is_transposed(const float* a, const float* b)
{
	return matrix_detect_are_equal(a, b, 12);
}

enum det_flags
{
	MFLG_FLIP = 1 << 0,
	MFLG_FIRST = 1 << 1,
	MFLG_SAME = 1 << 2,
	MFLG_TRANSL = 1 << 3,
	MFLG_INV = 1 << 4
};

void matrix_detect_process_upload(const float* mat, D3DXMATRIX* detected_model, D3DXMATRIX* detected_view)
{
	unsigned int flags = 0;

	if (g_mat_addr_count == 0)
	{
		D3DXMatrixIdentity(detected_model);
		memcpy(&detected_view->m[0][0], mat, 16*sizeof(float));
		if (g_mat_log_print_one_round)
		{
			matrix_print_s(&detected_model->m[0][0], "detected model 0");
			matrix_print_s(&detected_view->m[0][0], "detected view 0");
		}
	}
	else
	{
		if (matrix_is_identity(mat))
		{
			D3DXMatrixIdentity(detected_model);
			D3DXMatrixIdentity(detected_view);
		}
		else if (matrix_is_flippingmat(mat))
		{
			D3DXMatrixIdentity(detected_model);
			memcpy(&detected_view->m[0][0], mat, 16*sizeof(float));
			//memcpy(&detected_model->m[0][0], mat, 16*sizeof(float));
			//D3DXMatrixIdentity(detected_view);
		}
		else if (mat == g_mat_addrs[g_mat_addr_selected])
		{
			D3DXMatrixIdentity(detected_model);
			memcpy(&detected_view->m[0][0], mat, 16*sizeof(float));

			if (g_mat_log_print_one_round)
			{
				matrix_print_s(&detected_model->m[0][0], "detected model 1");
				matrix_print_s(&detected_view->m[0][0], "detected view 1");
			}
		}
		else if (mat != g_mat_addrs[g_mat_addr_selected])
		{
			D3DXMATRIX camera = D3DXMATRIX((const float*)(g_mat_addrs[g_mat_addr_selected])), camera_inv, local = D3DXMATRIX(mat);
			D3DXMatrixInverse(&camera_inv, NULL, &camera);
			D3DXMatrixMultiply(detected_model, &local, &camera_inv);
			memcpy(&detected_view->m[0][0], g_mat_addrs[g_mat_addr_selected], sizeof(detected_view->m));

			if (g_mat_log_print_one_round)
			{
				matrix_print_s(&detected_model->m[0][0], "detected model inv");
				matrix_print_s(&detected_view->m[0][0], "detected view inv");
			}
		}
	}
#if 0
	if (matrix_is_flippingmat(mat))
	{
		//ignore this one
		flags |= MFLG_FLIP;

		D3DXMatrixIdentity(detected_model);
		memcpy(&detected_view->m[0][0], mat, 16*sizeof(float));
	}
	else
	{
		if (g_mat_camera.usage == 0)
		{
			//first upload; make this sky or camera
			flags |= MFLG_FIRST;

			memcpy(g_mat_camera.mat, mat, sizeof(g_mat_camera.mat));
			g_mat_camera.detected = MDET_SKY_OR_CAMERA;
			g_mat_camera.usage = 1;

			D3DXMatrixIdentity(detected_model);
			memcpy(&detected_view->m[0][0], mat, sizeof(detected_view->m));
		}
		else
		{
			bool is_same = matrix_detect_are_equal(g_mat_camera.mat, mat, 16);
			bool is_translated = !is_same && matrix_is_transposed(g_mat_camera.mat, mat);

			if (is_same)
			{
				flags |= MFLG_SAME;

				g_mat_camera.usage++;

				D3DXMatrixIdentity(detected_model);
				memcpy(&detected_view->m[0][0], mat, sizeof(detected_view->m));
			}
			else if (/*g_mat_camera.detected == MDET_SKY_OR_CAMERA &&*/ is_translated)
			{
				flags |= MFLG_TRANSL;

				memcpy(g_mat_camera.mat, mat, sizeof(g_mat_camera.mat));
				g_mat_camera.detected = MDET_CAMERA;

				D3DXMatrixIdentity(detected_model);
				memcpy(&detected_view->m[0][0], mat, sizeof(detected_view->m));
			}
			else
			{
				//we now assume that this is the world matrix multiplied with the camera
				flags |= MFLG_INV;

				D3DXMATRIX g_camera_inv, local = D3DXMATRIX(mat);
				D3DXMatrixInverse(&g_camera_inv, NULL, &g_mat_camera.mat);
				D3DXMatrixMultiply(detected_model, &local, &g_camera_inv);
				memcpy(&detected_view->m[0][0], &g_mat_camera.mat.m[0][0], sizeof(detected_view->m));
			}
		}
	}
#endif
	bool mat_already_stored = false;
	for (int i = 0; i < g_mat_addr_count; i++)
	{
		if (g_mat_addrs[i] == mat)
		{
			mat_already_stored = true;
			break;
		}
	}
	if (!mat_already_stored && g_mat_addr_count < ARRAYSIZE(g_mat_addrs))
	{
		bool msg_newDefault = false;
		g_mat_addrs[g_mat_addr_count] = (void*)mat;
		if (mat < g_mat_addrs[g_mat_addr_selected])
		{
			memcpy(g_camera_backup, mat, sizeof(g_camera_backup));
			g_mat_addr_selected = g_mat_addr_count;
			msg_newDefault = true;
		}
		logPrintf("MatrixDetection new pointer stored[%d]: %p. %s\n", g_mat_addr_count, mat, (msg_newDefault ? "It has been autoselected as the new camera." : ""));
		g_mat_addr_count++;
	}
	for (int i = 0; i < g_mat_log_idx; i++)
	{
		if (matrix_detect_are_equal(mat, g_mat_log_data[i].mat, 0))
		{
			g_mat_log_data[i].usage++;
			g_mat_log_data[i].flags |= flags;
			g_mat_log_data[i].seq_num.push_back(g_mat_log_global_count);
			g_mat_log_data[i].seq_ptr.push_back((void*)mat);
			g_mat_log_global_count++;
			return;
		}
	}
	if (g_mat_log_idx < ARRAYSIZE(g_mat_log_data))
	{
		memcpy(g_mat_log_data[g_mat_log_idx].mat, mat, sizeof(g_mat_log_data[0].mat));
		g_mat_log_data[g_mat_log_idx].usage = 1;
		g_mat_log_data[g_mat_log_idx].flags = flags;
		g_mat_log_data[g_mat_log_idx].seq_num.clear();
		g_mat_log_data[g_mat_log_idx].seq_num.push_back(g_mat_log_global_count);
		g_mat_log_data[g_mat_log_idx].seq_ptr.clear();
		g_mat_log_data[g_mat_log_idx].seq_ptr.push_back((void*)mat);
		g_mat_log_global_count++;
		g_mat_log_idx++;
	}
}

void matrix_detect_frame_ended()
{
	key_inputs_t keys = keypress_get();

	if (keys.o && (keys.ctrl || keys.alt))
	{
		g_mat_detection_valid = !g_mat_detection_valid;
	}

	if (keys.pgdwn && (keys.ctrl || keys.alt))
	{
		g_mat_addr_selected++;
		if (g_mat_addr_selected >= g_mat_addr_count)
		{
			g_mat_addr_selected = 0;
		}
	}
	if (keys.pgup && (keys.ctrl || keys.alt))
	{
		g_mat_addr_selected--;
		if (g_mat_addr_selected < 0)
		{
			g_mat_addr_selected = g_mat_addr_count > 0 ? g_mat_addr_count - 1 : 0;
		}
	}

	if ((keys.i && (keys.ctrl || keys.alt)) || g_mat_log_print_one_round)
	{
		for (int i = 0; i < g_mat_log_idx; i++)
		{
			matrix_print(g_mat_log_data[i].mat, i, g_mat_log_data[i].usage, g_mat_log_data[i].flags, g_mat_log_data[i].seq_num.data(), g_mat_log_data[i].seq_ptr.data());
		}

	}
	g_mat_camera.clear();

	g_mat_log_idx = 0;
	g_mat_log_global_count = 0;

	g_mat_log_print_one_round = false;

	if (keys.u && (keys.ctrl || keys.alt))
	{
		if (!g_mat_log_print_one_round)
		{
			g_mat_log_print_one_round = true;
		}
	}
}

void matrix_detect_configuration_reset()
{
	matrix_detect_frame_ended();
	g_mat_addr_count = 0;
	g_mat_addr_selected = 0;
}

bool matrix_detect_are_equal(const float *a, const float *b, int count)
{
	int diff0 = 0;

	if ((count <= 0) || (count > 16))
	{
		count = 16;
	}

	for (int i = 0; i < count; i++)
	{
		if (fabsf(a[i] - b[i]) > 0.001f)
		{
			diff0++;
		}
	}

	return diff0 == 0;
}

static bool matrix_is_flippingmat(const float *mat)
{

	for (int i = 0; i < 16; i++)
	{
		float val = fabsf(mat[i]);
		if (val != 0 && val != 1)
		{
			return false;
		}
	}

	return true;
}

void matrix_print(const float* mat, int ordinal, int usage_count, unsigned int flags, const unsigned int * seq_nums, const void * const * seq_ptrs)
{
	logPrintf("matrix #%d usage_count:%d flags:0x%x\nusages[idx]{memadr}:",
		ordinal, usage_count, flags);
	for (int i = 0; i < usage_count; i++)
	{
		logPrintf(" [%d]{%p}", seq_nums[i], seq_ptrs[i]);
	}
	logPrintf("\n  %f %f %f %f\n  %f %f %f %f\n  %f %f %f %f\n  %f %f %f %f\n",
		mat[0], mat[1], mat[2], mat[3],
		mat[4], mat[5], mat[6], mat[7],
		mat[8], mat[9], mat[10], mat[11],
		mat[12], mat[13], mat[14], mat[15]);
}

OPENGL_API void WINAPI matrix_print_s(const float* mat, const char *info)
{
	if (g_mat_log_print_one_round)
	{
		logPrintf("matrix simple (%s)\n  %f %f %f %f\n  %f %f %f %f\n  %f %f %f %f\n  %f %f %f %f\n", info,
			mat[0], mat[1], mat[2], mat[3],
			mat[4], mat[5], mat[6], mat[7],
			mat[8], mat[9], mat[10], mat[11],
			mat[12], mat[13], mat[14], mat[15]);
	}
}
