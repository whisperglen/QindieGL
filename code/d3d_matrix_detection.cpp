
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
static void process_camera_mat(const float* camera);

static struct matrix_log_data g_mat_log_data[100];
static int g_mat_log_idx = 0;
static int g_mat_log_global_count = 0;

static int g_mat_log_print_one_round = 0;

static const float g_mat_identity[16] =
{
	1.0, 0.0, 0.0, 0.0,
	0.0, 1.0, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0
};

enum detection_mode_e
{
	DETECTION_NONE = 0,
	DETECTION_IDTECH2 = 1,
	DETECTION_IDTECH3 = 2
};

matrix_detect_t g_md = { false, DETECTION_NONE, {}, 0, 0, 0, false, 0, 175.f, {} };

#define PROCESS_CAMERA_MAT(CAMERA)  do { if (g_md.disp_enabled) process_camera_mat(CAMERA); } while(0)

bool matrix_detect_is_detection_enabled()
{
	return g_md.det_enabled;
}

inline bool matrix_is_identity(const float* mat)
{
	return matrix_detect_are_equal(g_mat_identity, mat, 0);
}

D3DXMATRIX *matrix_get_inverse( const float* mat )
{
	static D3DXMATRIX g_mat_cache;
	static D3DXMATRIX g_mat_cache_inverse;

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
	if (g_md.det_enabled == FALSE || g_md.det_mode == DETECTION_NONE)
	{
		memcpy(&detected_model->m[0][0], mat, 16*sizeof(float));
		D3DXMatrixIdentity(detected_view);
		if (g_mat_log_print_one_round & 2)
		{
			//matrix_print_s(&detected_model->m[0][0], "detected model N");
			//matrix_print_s(&detected_view->m[0][0], "detected view N");
		}
	}
	else if (g_md.addr_count == 0 || g_md.det_mode == DETECTION_IDTECH2)
	{
		D3DXMatrixIdentity(detected_model);
		memcpy(&detected_view->m[0][0], mat, 16 * sizeof(float)); PROCESS_CAMERA_MAT(mat);
		if (g_mat_log_print_one_round & 2)
		{
			//matrix_print_s(&detected_model->m[0][0], "detected model ID2");
			matrix_print_s(&detected_view->m[0][0], "detected view ID2");
		}
	}
	else if(g_md.det_mode == DETECTION_IDTECH3)
	{
		if (matrix_is_identity(mat))
		{
			D3DXMatrixIdentity(detected_model);
			D3DXMatrixIdentity(detected_view); PROCESS_CAMERA_MAT(g_mat_identity);
			if ( g_mat_log_print_one_round & 2 )
			{
				logPrintf( "matrix simple (detected identity)\n" );
			}
		}
		else if (matrix_is_flippingmat(mat))
		{
			D3DXMatrixIdentity(detected_model);
			memcpy(&detected_view->m[0][0], mat, 16*sizeof(float)); PROCESS_CAMERA_MAT(mat);
			//memcpy(&detected_model->m[0][0], mat, 16*sizeof(float));
			//D3DXMatrixIdentity(detected_view);
			if ( g_mat_log_print_one_round & 2 )
			{
				logPrintf( "matrix simple (detected flipping)\n" );
			}
		}
		else if (mat == g_md.addrs[g_md.addr_selected])
		{
			D3DXMatrixIdentity(detected_model);
			memcpy(&detected_view->m[0][0], mat, 16*sizeof(float)); PROCESS_CAMERA_MAT(&matrix_get_inverse(mat)->m[0][0]);

			if (g_mat_log_print_one_round & 2)
			{
				//matrix_print_s(&detected_model->m[0][0], "detected model ID3");
				matrix_print_s(&detected_view->m[0][0], "detected view ID3");
			}
		}
		else if (mat != g_md.addrs[g_md.addr_selected])
		{
			D3DXMATRIX local = D3DXMATRIX(mat);
			D3DXMATRIX* caminv = matrix_get_inverse((const float*)(g_md.addrs[g_md.addr_selected]));
			D3DXMatrixMultiply(detected_model, &local, caminv);
			memcpy(&detected_view->m[0][0], g_md.addrs[g_md.addr_selected], sizeof(detected_view->m)); PROCESS_CAMERA_MAT(&caminv->m[0][0]);

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
	for (int i = 0; i < g_md.addr_count; i++)
	{
		if (g_md.addrs[i] == mat)
		{
			mat_already_stored = true;
			break;
		}
	}
	if (!mat_already_stored && g_md.addr_count < ARRAYSIZE(g_md.addrs))
	{
		const char *msg = "";
		g_md.addrs[g_md.addr_count] = (void*)mat;
		if (!g_md.addr_preferred && mat < g_md.addrs[g_md.addr_selected])
		{
			g_md.addr_selected = g_md.addr_count;
			msg = "Marked as active camera.";
		}
		logPrintf("MatrixDetection new pointer stored[%d]: %p. %s\n", g_md.addr_count, mat, msg);
		g_md.addr_count++;
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
	if (g_md.det_mode == DETECTION_IDTECH2)
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
		g_md.det_enabled = !g_md.det_enabled;
		logPrintf( "MatrixDetection changed:%d mode:%d\n", g_md.det_enabled, g_md.det_mode );
	}

	if (keys.pgdwn && (keys.ctrl || keys.alt))
	{
		g_md.addr_selected++;
		if (g_md.addr_selected >= g_md.addr_count)
		{
			g_md.addr_selected = 0;
		}
	}
	if (keys.pgup && (keys.ctrl || keys.alt))
	{
		g_md.addr_selected--;
		if (g_md.addr_selected < 0)
		{
			g_md.addr_selected = g_md.addr_count > 0 ? g_md.addr_count - 1 : 0;
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

void matrix_detect_configuration_reset()
{
	matrix_detect_frame_ended();
	g_md.addr_count = 0;
	g_md.addr_selected = 0;

	g_md.det_enabled = D3DGlobal_ReadGameConf("enable_camera_detection");
	g_md.det_mode = D3DGlobal_ReadGameConf("camera_detection_mode");
	g_md.addr_preferred = D3DGlobal_ReadGameConfPtr("camera_detection_preferred");

	logPrintf("MatrixDetection enabled:%d mode:%d\n", g_md.det_enabled, g_md.det_mode);

	if (g_md.addr_preferred)
	{
		g_md.addr_selected = 0;
		g_md.addrs[0] = g_md.addr_preferred;
		g_md.addr_count = 1;
		logPrintf("MatrixDetection has preferred address:%p\n", g_md.addr_preferred);
	}
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

OPENGL_API void WINAPI matrix_update_camera(const float* mat)
{
	g_md.det_mode = DETECTION_NONE;

	_CRT_UNUSED(mat);
}

OPENGL_API void WINAPI matrix_preferred_address(const void* addr)
{
	g_md.addr_preferred = addr;
	bool mat_already_stored = false;
	for (int i = 0; i < g_md.addr_count; i++)
	{
		if (g_md.addrs[i] == addr)
		{
			g_md.addr_selected = i;
			mat_already_stored = true;
			break;
		}
	}
	if (!mat_already_stored)
	{
		if (g_md.addr_count < ARRAYSIZE(g_md.addrs))
		{
			g_md.addrs[g_md.addr_count] = addr;
			g_md.addr_selected = g_md.addr_count;
			g_md.addr_count++;
		}
		else
		{
			g_md.addrs[0] = addr;
			g_md.addr_selected = 0;
		}
	}
}

// This should be called one per frame so we update the display counter
bool matrix_detect_get_display(matrix_detect_t** out)
{
	matrix_detect_t::mat_slot_s* slot = g_md.disp_slot;
	for (int i = 0; i < MAT_DISP_NUMSLOTS; ++i, ++slot)
	{
		if (slot->count)
			slot->count--;
	}
	if (out) *out = &g_md;
	return g_md.disp_enabled;
}

int findBestSlot(const float* newMat, matrix_detect_t::mat_slot_s* slots, float thresholdSq) {
	int bestIndex = -1;
	float bestDistSq = thresholdSq;

	// Initialize with a large number for the scale difference tie-breaker
	float bestScaleDiff = 999999.0f;

	// Tolerance to consider translations "identical" (avoids float precision issues)
	const float tieThresholdSq = 0.001f;

	// Calculate the sum of the scale diagonal for the incoming matrix
	float newScale = newMat[0] + newMat[5] + newMat[10];

	for (int i = 0; i < MAT_DISP_NUMSLOTS; ++i) {
		// Assuming 'count > 0' means the slot is currently active and occupied
		if (slots[i].count == 0) continue;

		float dx = newMat[12] - slots[i].matrix[12];
		float dy = newMat[13] - slots[i].matrix[13];
		float dz = newMat[14] - slots[i].matrix[14];
		float distSq = (dx * dx) + (dy * dy) + (dz * dz);

		// If it's completely out of bounds, skip immediately
		if (distSq > thresholdSq) continue;

		// Calculate scale difference for the fallback check
		float slotScale = slots[i].matrix[0] +
			slots[i].matrix[5] +
			slots[i].matrix[10];
		float scaleDiff = std::abs(newScale - slotScale);

		bool isBetterMatch = false;

		if (bestIndex == -1) {
			// First valid slot found
			isBetterMatch = true;
		}
		else {
			// If both the current candidate and the previously found best slot 
			// share the exact same origin location...
			if (distSq < tieThresholdSq && bestDistSq < tieThresholdSq) {
				// ...break the tie by picking the one with the most similar scale.
				if (scaleDiff < bestScaleDiff) {
					isBetterMatch = true;
				}
			}
			// Otherwise, strictly prefer the physically closer matrix.
			else if (distSq < bestDistSq) {
				isBetterMatch = true;
			}
		}

		if (isBetterMatch) {
			bestDistSq = distSq;
			bestScaleDiff = scaleDiff;
			bestIndex = i;
		}
	}

	return bestIndex;
}

#define MAT_DET_FRAME_DELAY 5
static void process_camera_mat(const float* camera)
{
	int bestSlot = findBestSlot(camera, g_md.disp_slot, g_md.disp_threshold);

	matrix_detect_t::mat_slot_s* slot;
	if (bestSlot < 0)
	{
		slot = g_md.disp_slot;
		//int lowcount = 999;
		for (int i = 0; i < MAT_DISP_NUMSLOTS; i++, slot++)
		{
			if (slot->count == 0)
			{
				bestSlot = i;
				break;
			}
			//if (slot->count < lowcount)
			//{
			//	bestSlot = i;
			//	lowcount = slot->count;
			//}
		}
	}

	if (bestSlot >= 0)
	{
		slot = &g_md.disp_slot[bestSlot];
		slot->count = MAT_DET_FRAME_DELAY;
		memcpy(slot->matrix, camera, sizeof(float[16]));
	}
	else
	{
		g_md.disp_rejects++;
	}
}