
#include "d3d_matrix_detection.hpp"
#include "d3d_helpers.hpp"
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_utils.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <stdio.h>
#include <string.h>

#include "ini.h"

struct matrix_log_data
{
	float mat[16];
	int usage;
	unsigned int flags;
	std::vector<unsigned int> seq_num;
	std::vector<void*> seq_ptr;
};

#if 0
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

enum det_flags
{
	MFLG_FLIP = 1 << 0,
	MFLG_FIRST = 1 << 1,
	MFLG_SAME = 1 << 2,
	MFLG_TRANSL = 1 << 3,
	MFLG_INV = 1 << 4
};

mat_detection_t g_mat_camera;
#endif

static bool matrix_is_flippingmat(const float* mat);

static struct matrix_log_data g_mat_log_data[100];
static int g_mat_log_idx = 0;
static int g_mat_log_global_count = 0;

static int g_mat_log_print_one_round = 0;

void* g_mat_addrs[3];
int g_mat_addr_count = 0;
int g_mat_addr_selected = 0;

static float g_mat_identity[16] =
{
	1.0, 0.0, 0.0, 0.0,
	0.0, 1.0, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0
};

static D3DXMATRIX g_mat_cache;
static D3DXMATRIX g_mat_cache_inverse;

enum detection_mode_e
{
	DETECTION_NONE = 0,
	DETECTION_IDTECH2 = 1,
	DETECTION_IDTECH3 = 2
};

bool g_mat_detection_enabled = false;
int g_mat_detection_mode = 0;

bool matrix_detect_is_detection_enabled()
{
	return g_mat_detection_enabled;
}

inline bool matrix_is_identity(const float* mat)
{
	return matrix_detect_are_equal(g_mat_identity, mat, 0);
}

inline bool matrix_is_transposed(const float* a, const float* b)
{
	return matrix_detect_are_equal(a, b, 12);
}

D3DXMATRIX *matrix_get_inverse( const float* mat )
{
	if ( 0 == memcmp( &g_mat_cache.m[0][0], mat, sizeof( g_mat_cache.m ) ) )
	{
		return &g_mat_cache_inverse;
	}
	memcpy( &g_mat_cache.m[0][0], mat, sizeof( g_mat_cache.m ) );
	void *tmp = D3DXMatrixInverse(&g_mat_cache_inverse, NULL, &g_mat_cache);
	if ( tmp == NULL )
	{
		char out[145];
		unsigned int* ptr = (unsigned int*)mat;
		snprintf( out, sizeof( out ), "%x %x %x %x\n%x %x %x %x\n%x %x %x %x\n%x %x %x %x",
			ptr[0], ptr[1], ptr[2], ptr[3],
			ptr[4], ptr[5], ptr[6], ptr[7],
			ptr[8], ptr[9], ptr[10], ptr[11],
			ptr[12], ptr[13], ptr[14], ptr[15] );
		PRINT_ONCE("WARNING: matrix inverse failed: %s\n", out);
	}
	return &g_mat_cache_inverse;
}

void matrix_detect_process_upload(const float* mat, D3DXMATRIX* detected_model, D3DXMATRIX* detected_view)
{
	unsigned int flags = 0;
#if 1
	if (g_mat_detection_enabled == FALSE || g_mat_detection_mode == DETECTION_NONE)
	{
		memcpy(&detected_model->m[0][0], mat, 16*sizeof(float));
		D3DXMatrixIdentity(detected_view);
		if (g_mat_log_print_one_round & 2)
		{
			//matrix_print_s(&detected_model->m[0][0], "detected model N");
			//matrix_print_s(&detected_view->m[0][0], "detected view N");
		}
	}
	else if (g_mat_addr_count == 0 || g_mat_detection_mode == DETECTION_IDTECH2)
	{
		D3DXMatrixIdentity(detected_model);
		memcpy(&detected_view->m[0][0], mat, 16*sizeof(float));
		if (g_mat_log_print_one_round & 2)
		{
			//matrix_print_s(&detected_model->m[0][0], "detected model ID2");
			matrix_print_s(&detected_view->m[0][0], "detected view ID2");
		}
	}
	else if(g_mat_detection_mode == DETECTION_IDTECH3)
	{
		if (matrix_is_identity(mat))
		{
			D3DXMatrixIdentity(detected_model);
			D3DXMatrixIdentity(detected_view);
			if ( g_mat_log_print_one_round & 2 )
			{
				logPrintf( "matrix simple (detected identity)\n" );
			}
		}
		else if (matrix_is_flippingmat(mat))
		{
			D3DXMatrixIdentity(detected_model);
			memcpy(&detected_view->m[0][0], mat, 16*sizeof(float));
			//memcpy(&detected_model->m[0][0], mat, 16*sizeof(float));
			//D3DXMatrixIdentity(detected_view);
			if ( g_mat_log_print_one_round & 2 )
			{
				logPrintf( "matrix simple (detected flipping)\n" );
			}
		}
		else if (mat == g_mat_addrs[g_mat_addr_selected])
		{
			D3DXMatrixIdentity(detected_model);
			memcpy(&detected_view->m[0][0], mat, 16*sizeof(float));

			if (g_mat_log_print_one_round & 2)
			{
				//matrix_print_s(&detected_model->m[0][0], "detected model ID3");
				matrix_print_s(&detected_view->m[0][0], "detected view ID3");
			}
		}
		else if (mat != g_mat_addrs[g_mat_addr_selected])
		{
			D3DXMATRIX local = D3DXMATRIX(mat);
			D3DXMatrixMultiply(detected_model, &local, matrix_get_inverse((const float*)(g_mat_addrs[g_mat_addr_selected])));
			memcpy(&detected_view->m[0][0], g_mat_addrs[g_mat_addr_selected], sizeof(detected_view->m));

			if (g_mat_log_print_one_round & 2)
			{
				matrix_print_s(&detected_model->m[0][0], "detected model ID3inv");
				matrix_print_s(&detected_view->m[0][0], "detected view ID3inv");
			}
		}
	}
#else
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
			g_mat_addr_selected = g_mat_addr_count;
			msg_newDefault = true;
		}
		logPrintf("MatrixDetection new pointer stored[%d]: %p. %s\n", g_mat_addr_count, mat, (msg_newDefault ? "Marked as active camera." : ""));
		g_mat_addr_count++;
	}
	if ( g_mat_log_print_one_round )
	{
		for ( int i = 0; i < g_mat_log_idx; i++ )
		{
			if ( matrix_detect_are_equal( mat, g_mat_log_data[i].mat, 0 ) )
			{
				g_mat_log_data[i].usage++;
				g_mat_log_data[i].flags |= flags;
				g_mat_log_data[i].seq_num.push_back( g_mat_log_global_count );
				g_mat_log_data[i].seq_ptr.push_back( (void*)mat );
				g_mat_log_global_count++;
				return;
			}
		}
		if ( g_mat_log_idx < ARRAYSIZE( g_mat_log_data ) )
		{
			memcpy( g_mat_log_data[g_mat_log_idx].mat, mat, sizeof( g_mat_log_data[0].mat ) );
			g_mat_log_data[g_mat_log_idx].usage = 1;
			g_mat_log_data[g_mat_log_idx].flags = flags;
			g_mat_log_data[g_mat_log_idx].seq_num.clear();
			g_mat_log_data[g_mat_log_idx].seq_num.push_back( g_mat_log_global_count );
			g_mat_log_data[g_mat_log_idx].seq_ptr.clear();
			g_mat_log_data[g_mat_log_idx].seq_ptr.push_back( (void*)mat );
			g_mat_log_global_count++;
			g_mat_log_idx++;
		}
	}
}

void matrix_detect_on_world_retrieve(const float* mat, D3DXMATRIX* detected_model, D3DXMATRIX* detected_view)
{
	if (g_mat_detection_mode == DETECTION_IDTECH2)
	{
		//store modelview in view matrix
		D3DXMatrixIdentity(detected_model);
		memcpy(&(detected_view->m[0][0]), mat, sizeof(detected_view->m));
	}
}

void matrix_detect_frame_ended()
{
	key_inputs_t keys = keypress_get();

	if (keys.o && (keys.ctrl || keys.alt))
	{
		g_mat_detection_enabled = !g_mat_detection_enabled;
		logPrintf( "MatrixDetection changed:%d mode:%d\n", g_mat_detection_enabled, g_mat_detection_mode );
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

	if ( g_mat_log_print_one_round )
	{
		for (int i = 0; i < g_mat_log_idx; i++)
		{
			matrix_print(g_mat_log_data[i].mat, i, g_mat_log_data[i].usage, g_mat_log_data[i].flags, g_mat_log_data[i].seq_num.data(), g_mat_log_data[i].seq_ptr.data());
		}

	}

	g_mat_log_idx = 0;
	g_mat_log_global_count = 0;

	g_mat_log_print_one_round = 0;

	if ( keys.i && (keys.ctrl || keys.alt) )
	{
		g_mat_log_print_one_round |= 1;
	}

	if (keys.u && (keys.ctrl || keys.alt))
	{
		g_mat_log_print_one_round |= 2;
	}
}

static int read_confval(const char* valname, mINI::INIStructure &ini)
{
	int ret = 0;
	const char* gamename = D3DGlobal_GetGameName();
	for(int tries = 0; tries < 2; tries++)
	{
		if (gamename && ini.has(gamename) && ini[gamename].has(valname))
		{
			try {
				ret = std::stoi(ini[gamename][valname]);
			} catch (const std::exception &e) {
				logPrintf("MatrixDetection: EXCEPTION %s\n", e.what());
			}
		}
		else
		{
			gamename = GLOBAL_GAMENAME;
		}
	}
	return ret;
}

void matrix_detect_configuration_reset()
{
	matrix_detect_frame_ended();
	g_mat_addr_count = 0;
	g_mat_addr_selected = 0;

	mINI::INIStructure &ini = *((mINI::INIStructure *)D3DGlobal_GetIniHandler());
	g_mat_detection_enabled = read_confval("enable_camera_detection", ini);
	g_mat_detection_mode = read_confval("camera_detection_mode", ini);

	logPrintf("MatrixDetection enabled:%d mode:%d\n", g_mat_detection_enabled, g_mat_detection_mode);
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
