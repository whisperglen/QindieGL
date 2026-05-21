
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <immintrin.h>
#include "tests.h"
#include "d3dx9.h"

static float FLASHLIGHT_POSITION_OFFSET[3] = { -9, 21, 16 };
static float FLASHLIGHT_DIRECTION_OFFSET[3] = { 0.095f, -0.08f, 0 };

static void apply_flashlight_offsets_manual(const float* position, const float* direction, D3DXVECTOR3* outpos, D3DXVECTOR3* outdir)
{
	// 1. Setup the player's base vectors
	D3DXVECTOR3 playerDir(direction[0], direction[1], direction[2]);
	D3DXVec3Normalize(&playerDir, &playerDir);

	// id Tech uses Z-Up
	D3DXVECTOR3 worldUp(0.0f, 0.0f, 1.0f);

	// 2. Calculate the Local Axes (Right, Up, Forward)
	D3DXVECTOR3 right, localUp;

	// Right vector is perpendicular to Forward and WorldUp
	D3DXVec3Cross(&right, &playerDir, &worldUp);
	D3DXVec3Normalize(&right, &right);

	// True local Up vector is perpendicular to Right and Forward
	D3DXVec3Cross(&localUp, &right, &playerDir);
	D3DXVec3Normalize(&localUp, &localUp);

	// NEW: Emulate OpenGL / id Tech Right-Handed space (-Z is Forward)
	D3DXVECTOR3 engineZ(-playerDir.x, -playerDir.y, -playerDir.z);

	// 3. Apply the Position Offset
	D3DXVECTOR3 posOffset = *((D3DXVECTOR3*)FLASHLIGHT_POSITION_OFFSET);

	// Use engineZ so a positive offset pushes backward, matching original game data
	outpos->x = position[0] + (right.x * posOffset.x) + (localUp.x * posOffset.y) + (engineZ.x * posOffset.z);
	outpos->y = position[1] + (right.y * posOffset.x) + (localUp.y * posOffset.y) + (engineZ.y * posOffset.z);
	outpos->z = position[2] + (right.z * posOffset.x) + (localUp.z * posOffset.y) + (engineZ.z * posOffset.z);

	// 4. Direction Offset (Treating as Radians)
	float pitch = ((float*)FLASHLIGHT_DIRECTION_OFFSET)[0]; // Up/Down rotation
	float yaw = ((float*)FLASHLIGHT_DIRECTION_OFFSET)[1]; // Left/Right rotation
	float roll = ((float*)FLASHLIGHT_DIRECTION_OFFSET)[2]; // Twist

	// Create a rotation matrix from the radians
	D3DXMATRIX localRot;
	D3DXMatrixRotationYawPitchRoll(&localRot, yaw, pitch, roll);

	// The flashlight points "forward" (Z=-1) by default in OpenGL local space
	D3DXVECTOR3 localForward(0.0f, 0.0f, -1.0f);
	D3DXVECTOR3 rotatedLocalForward;

	// Apply the radian angles to the local forward vector
	D3DXVec3TransformNormal(&rotatedLocalForward, &localForward, &localRot);

	// 5. Convert that local rotated direction into World Space using the engine's Z
	outdir->x = (right.x * rotatedLocalForward.x) + (localUp.x * rotatedLocalForward.y) + (engineZ.x * rotatedLocalForward.z);
	outdir->y = (right.y * rotatedLocalForward.x) + (localUp.y * rotatedLocalForward.y) + (engineZ.y * rotatedLocalForward.z);
	outdir->z = (right.z * rotatedLocalForward.x) + (localUp.z * rotatedLocalForward.y) + (engineZ.z * rotatedLocalForward.z);
	D3DXVec3Normalize(outdir, outdir);
}

static void apply_flashlight_offsets_inverse(const float* position, const float* direction, D3DXVECTOR3* outpos, D3DXVECTOR3* outdir)
{
	D3DXMATRIX invview, viewrot;
	const D3DXVECTOR3 updir(0, 0, 1);
	const D3DXVECTOR3 zero(0, 0, 0);
	D3DXVECTOR3 playerDir(direction[0], direction[1], direction[2]);
	D3DXMatrixLookAtRH(&viewrot, &zero, &playerDir, &updir);
	//D3DGlobal_GetCamera( &viewrot );
	viewrot._41 = 0.0;
	viewrot._42 = 0.0;
	viewrot._43 = 0.0;
	D3DXMatrixInverse(&invview, NULL, (D3DXMATRIX*)&viewrot);
	D3DXVec3TransformCoord(outpos, (D3DXVECTOR3*)FLASHLIGHT_POSITION_OFFSET, &invview);
	outpos->x += position[0];
	outpos->y += position[1];
	outpos->z += position[2];
	D3DXVec3TransformNormal(outdir, (D3DXVECTOR3*)FLASHLIGHT_DIRECTION_OFFSET, &invview);
	outdir->x += direction[0];
	outdir->y += direction[1];
	outdir->z += direction[2];
	D3DXVec3Normalize(outdir, outdir);
}

typedef void (*test_func)(const float* position, const float* direction, D3DXVECTOR3* outpos, D3DXVECTOR3* outdir);

static test_func functions[] =
{
	apply_flashlight_offsets_inverse,
	apply_flashlight_offsets_manual,
};

static void GenPlayerData(D3DXVECTOR3* outPosition, D3DXVECTOR3* outDirection);
static int CompareVectorsEpsilon(const D3DXVECTOR3* v1, const D3DXVECTOR3* v2, float epsilon);

#define TEST_SZ 100
static D3DXVECTOR3 test_vec_pos[TEST_SZ];
static D3DXVECTOR3 test_vec_dir[TEST_SZ];
static D3DXVECTOR3 out_vec_pos[ARRAYSIZE(functions)][TEST_SZ];
static D3DXVECTOR3 out_vec_dir[ARRAYSIZE(functions)][TEST_SZ];

void do_texgen_flashl()
{
	random_init();

	for (int i = 0; i < TEST_SZ; i++)
	{
		GenPlayerData(&test_vec_pos[i], &test_vec_dir[i]);
	}

	for (int i = 0; i < ARRAYSIZE(functions); i++)
	{
		for (int j = 0; j < TEST_SZ; j++)
			functions[i](test_vec_pos[j], test_vec_dir[j], &out_vec_pos[i][j], &out_vec_dir[i][j]);
	}

	for (int i = 1; i < ARRAYSIZE(functions); i++)
	{
		for (int j = 0; j < TEST_SZ; j++)
		{
			bool resultpos = CompareVectorsEpsilon(&out_vec_pos[0][j], &out_vec_pos[i][j], 0.1f);
			assertloop(resultpos, j);
			if ( !resultpos )
			{
				const D3DXVECTOR3* vec1 = &test_vec_pos[j];
				const D3DXVECTOR3* vec2 = &out_vec_pos[0][j];
				const D3DXVECTOR3* vec3 = &out_vec_pos[i][j];
				printf("Failed algo %d on vector %d\n", i, j);
				printf("           Vector POS          \n");
				printf("-------------------------------\n");
				printf("[ %7.2f  %7.2f  %7.2f ]\n", vec1->x, vec1->y, vec1->z);

				printf("           Vector A            \n");
				printf("-------------------------------\n");
				printf("[ %7.2f  %7.2f  %7.2f ]\n", vec2->x, vec2->y, vec2->z);

				printf("           Vector B            \n");
				printf("-------------------------------\n");
				printf("[ %7.2f  %7.2f  %7.2f ]\n", vec3->x, vec3->y, vec3->z);

				printf("\n");
			}
			bool resultdir = CompareVectorsEpsilon(&out_vec_dir[0][j], &out_vec_dir[i][j], 0.2f);
			assertloop(resultdir, j);
			if ( !resultdir)
			{
				const D3DXVECTOR3* vec1 = &test_vec_dir[j];
				const D3DXVECTOR3* vec2 = &out_vec_dir[0][j];
				const D3DXVECTOR3* vec3 = &out_vec_dir[i][j];
				printf("Failed algo %d on vector %d\n", i, j);
				printf("           Vector DIR\n");
				printf("-------------------------------\n");
				printf("[ %7.2f  %7.2f  %7.2f ]\n", vec1->x, vec1->y, vec1->z);

				printf("           Vector A\n");
				printf("-------------------------------\n");
				printf("[ %7.2f  %7.2f  %7.2f ]\n", vec2->x, vec2->y, vec2->z);

				printf("           Vector B\n");
				printf("-------------------------------\n");
				printf("[ %7.2f  %7.2f  %7.2f ]\n", vec3->x, vec3->y, vec3->z);

				printf("\n");
			}
		}
	}
}



static float get_float()
{
	int16_t a = 0, b = 0;
	random_bytes((uc8_t*)&a, sizeof(a));
	random_bytes((uc8_t*)&b, sizeof(b));
	return (float)a / b;
}

static void GenPlayerData(D3DXVECTOR3* outPosition, D3DXVECTOR3* outDirection)
{
	outPosition->x = get_float();
	outPosition->y = get_float();
	outPosition->z = get_float();

	float yaw = get_float() * 2.0f * 3.14159265f;
	float pitch = get_float() * 2.0f * 3.14159265f;
	float roll = get_float() * 2.0f * 3.14159265f;

	outDirection->x = yaw;
	outDirection->y = pitch;
	outDirection->z = 0;
}

// Returns 1 if the vectors are equal within the epsilon, 0 otherwise.
int CompareVectorsEpsilon(const D3DXVECTOR3* v1, const D3DXVECTOR3* v2, float epsilon)
{
	if (!v1 || !v2)
		return 0;

	// Use fabsf (float absolute value) to check the distance on each axis
	if (fabsf(v1->x - v2->x) > epsilon) return 0;
	if (fabsf(v1->y - v2->y) > epsilon) return 0;
	if (fabsf(v1->z - v2->z) > epsilon) return 0;

	return 1;
}