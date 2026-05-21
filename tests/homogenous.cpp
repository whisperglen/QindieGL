
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <immintrin.h>
#include "tests.h"
#include "d3dx9.h"

static void run_with_mat_inverse(const D3DXMATRIX* inmat, D3DXMATRIX* shiftmat)
{
	float skyboxScale = 3000.0f;
	D3DXMATRIX mvmat;
	D3DXMatrixInverse(&mvmat, NULL, inmat);
	D3DXMatrixScaling(shiftmat, skyboxScale, skyboxScale, skyboxScale);
	D3DXMATRIX scratch;
	D3DXMatrixTranslation(&scratch, mvmat.m[3][0], mvmat.m[3][1], mvmat.m[3][2]);
	D3DXMatrixMultiply(shiftmat, shiftmat, &scratch);
}

static void run_with_manual_calc(const D3DXMATRIX* inmat, D3DXMATRIX* shiftmat)
{
	float skyboxScale = 3000.0f;
	const D3DXMATRIX* mvmat = inmat;

	// Extract camera world position without a 4x4 Inverse
	float camX = -(mvmat->m[3][0] * mvmat->m[0][0] + mvmat->m[3][1] * mvmat->m[0][1] + mvmat->m[3][2] * mvmat->m[0][2]);
	float camY = -(mvmat->m[3][0] * mvmat->m[1][0] + mvmat->m[3][1] * mvmat->m[1][1] + mvmat->m[3][2] * mvmat->m[1][2]);
	float camZ = -(mvmat->m[3][0] * mvmat->m[2][0] + mvmat->m[3][1] * mvmat->m[2][1] + mvmat->m[3][2] * mvmat->m[2][2]);

	// Manually construct the Scale * Translate matrix
	*shiftmat = D3DXMATRIX(
		skyboxScale, 0.0f, 0.0f, 0.0f,
		0.0f, skyboxScale, 0.0f, 0.0f,
		0.0f, 0.0f, skyboxScale, 0.0f,
		camX, camY, camZ, 1.0f
	);
}

typedef void (*test_func)(const D3DXMATRIX* inmat, D3DXMATRIX* shiftmat);

static test_func functions[] =
{
	run_with_mat_inverse,
	run_with_manual_calc,
};

static void fill_matrix_valid_orthonormal(D3DXMATRIX* out_mat);
static bool compare_matrices_epsilon(const D3DXMATRIX* mat1, const D3DXMATRIX* mat2, float epsilon);

#define TEST_SZ 100
static D3DXMATRIX test_mats[TEST_SZ];
static D3DXMATRIX out_mats[ARRAYSIZE(functions)][TEST_SZ];

void do_texgen_homogenous()
{
	random_init();

	for (int i = 0; i < TEST_SZ; i++)
	{
		fill_matrix_valid_orthonormal(&test_mats[i]);
	}

	for (int i = 0; i < ARRAYSIZE(functions); i++)
	{
		for (int j = 0; j < TEST_SZ; j++)
			functions[i](&test_mats[j], &out_mats[i][j]);
	}

	for (int i = 1; i < ARRAYSIZE(functions); i++)
	{
		for (int j = 0; j < TEST_SZ; j++)
		{
			bool result = compare_matrices_epsilon(&out_mats[0][j], &out_mats[i][j], 0.0001f);
			assertloop(result, j);
			if (!result)
			{
				const D3DXMATRIX* mat1 = &out_mats[0][j];
				const D3DXMATRIX* mat2 = &out_mats[i][j];
				const D3DXMATRIX* mat3 = &test_mats[j];
				printf("Failed algo %d on matrix %d\n", i, j);
				printf("               Matrix T                  \n");
				printf("-----------------------------------------\n");
				for (int r = 0; r < 4; r++) {
					printf("[ %7.2f  %7.2f  %7.2f  %7.2f ]\n",
						mat3->m[r][0], mat3->m[r][1], mat3->m[r][2], mat3->m[r][3]);
				}
				printf("               Matrix A                                      Matrix B\n");
				printf("-----------------------------------------     -----------------------------------------\n");

				for (int r = 0; r < 4; r++) {
					// Print row 'r' for Matrix A
					printf("[ %7.2f  %7.2f  %7.2f  %7.2f ]",
						mat1->m[r][0], mat1->m[r][1], mat1->m[r][2], mat1->m[r][3]);

					// Visual separator between the two matrices
					printf("     ");

					// Print row 'r' for Matrix B
					printf("[ %7.2f  %7.2f  %7.2f  %7.2f ]\n",
						mat2->m[r][0], mat2->m[r][1], mat2->m[r][2], mat2->m[r][3]);
				}
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

static void fill_matrix_valid_orthonormal(D3DXMATRIX* out_mat) {
	if (!out_mat) return;

	// 1. Generate three random angles in radians (e.g., between 0 and 2*Pi)
	// Adjust this range if your get_float() already outputs a specific range
	float yaw = get_float() * 2.0f * 3.14159265f;
	float pitch = get_float() * 2.0f * 3.14159265f;
	float roll = get_float() * 2.0f * 3.14159265f;

	// 2. Generate a pure, orthonormal rotation matrix
	D3DXMatrixRotationYawPitchRoll(out_mat, yaw, pitch, roll);

	// 2. Fill the bottom row (X, Y, Z Translation / Position)
	out_mat->m[3][0] = get_float(); // Translation X
	out_mat->m[3][1] = get_float(); // Translation Y
	out_mat->m[3][2] = get_float(); // Translation Z

	// 3. Fix the final column for standard W-coordinate behavior
	out_mat->m[0][3] = 0.0f;
	out_mat->m[1][3] = 0.0f;
	out_mat->m[2][3] = 0.0f;
	out_mat->m[3][3] = 1.0f;
}

/**
 * Compares two D3DXMATRIX structures element-by-element.
 * Returns true if they match within the allowed epsilon tolerance, false otherwise.
 */
static bool compare_matrices_epsilon(const D3DXMATRIX* mat1, const D3DXMATRIX* mat2, float epsilon) {
	// If both are NULL, they match. If only one is NULL, they don't.
	if (mat1 == mat2) return true;
	if (!mat1 || !mat2) return false;

	for (int row = 0; row < 4; row++) {
		for (int col = 0; col < 4; col++) {
			// Calculate the absolute difference between the two elements
			float diff = fabsf(mat1->m[row][col] - mat2->m[row][col]);

			// If any single element exceeds the tolerance, the matrices do not match
			if (diff > epsilon) {
				return false;
			}
		}
	}

	return true;
}