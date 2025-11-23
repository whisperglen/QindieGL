
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <immintrin.h>
#include "tests.h"

typedef float GLfloat;

static float objectPlane[4];
static float eyePlane[4];

static void TexGenFunc_ObjectLinear(const GLfloat* vertex, const GLfloat* /*normal*/, float* output_texcoord)
{
	const float* p = objectPlane;
	float out_coord = 0.0f;

	for (int i = 0; i < 4; ++i, ++vertex, ++p)
		out_coord += (*vertex) * (*p);
	*output_texcoord = out_coord;
}

static void TexGenFunc_ObjectLinear_SSE(const GLfloat* vertex, const GLfloat* /*normal*/, float* output_texcoord)
{
	__m128 x0, x1;
	x0 = _mm_loadu_ps(vertex);
	x0 = _mm_mul_ps(x0, _mm_loadu_ps(objectPlane));
	x1 = _mm_shuffle_ps(x0, x0, 0b11110101);
	x0 = _mm_add_ps(x0, x1);
	x1 = _mm_shuffle_ps(x0, x0, 0b00000010);
	x0 = _mm_add_ss(x0, x1);
	_mm_store_ss(output_texcoord, x0);
}

static void TexGenFunc_ObjectLinear_SSE_asm(const GLfloat* vertex, const GLfloat* /*normal*/, float* output_texcoord)
{
	_declspec(align(16)) float sse_vertex[4];
	_declspec(align(16)) float sse_plane[4];
	float out_coord = 0.0f;

	memcpy(sse_vertex, vertex, sizeof(float) * 4);
	memcpy(sse_plane, objectPlane, sizeof(float) * 4);

	_asm {
		movaps	xmm0, xmmword ptr[sse_vertex]
		mulps	xmm0, xmmword ptr[sse_plane]
		movaps	xmm1, xmm0
		shufps	xmm1, xmm1, 11110101b
		addps	xmm0, xmm1
		movaps	xmm1, xmm0
		shufps	xmm1, xmm1, 00000010b
		addss	xmm0, xmm1
		movss	out_coord, xmm0
	}
	*output_texcoord = out_coord;
}

static void TexGenFunc_EyeLinear(const GLfloat* vertex, const GLfloat* /*normal*/, float* output_texcoord)
{
	const float* p = eyePlane;
	float out_coord = 0.0f;
	for (int i = 0; i < 4; ++i, ++vertex, ++p)
		out_coord += (*vertex) * (*p);
	*output_texcoord = out_coord;
}

static void TexGenFunc_EyeLinear_SSE(const GLfloat* vertex, const GLfloat* /*normal*/, float* output_texcoord)
{
	__m128 x0, x1;
	x0 = _mm_loadu_ps(vertex);
	x0 = _mm_mul_ps(x0, _mm_loadu_ps(eyePlane));
	x1 = _mm_shuffle_ps(x0, x0, 0b11110101);
	x0 = _mm_add_ps(x0, x1);
	x1 = _mm_shuffle_ps(x0, x0, 0b00000010);
	x0 = _mm_add_ss(x0, x1);
	_mm_store_ss(output_texcoord, x0);
}

static void TexGenFunc_ReflectionMap(int coord, const GLfloat* vertex, const GLfloat* normal, float* output_texcoord)
{
	if (coord == 3) {
		*output_texcoord = 1.0f;
	}
	else {
		float fdot = 2.0f * (normal[0] * vertex[0] + normal[1] * vertex[1] + normal[2] * vertex[2]);
		*output_texcoord = vertex[coord] - normal[coord] * fdot;
	}
}

static void TexGenFunc_ReflectionMap_SSE(int coord, const GLfloat* vertex, const GLfloat* normal, float* output_texcoord)
{
	if (coord == 3) {
		*output_texcoord = 1.0f;
	}
	else {
		__m128 sse_vertex = { vertex[0], vertex[1], vertex[2], 0 };
		__m128 sse_normal = { normal[0], normal[1], normal[2], 0 };
		__m128 sse_scale_2x = { 2.0f, 2.0f, 2.0f, 0.0f };
		__m128 x0, x1, x2, x3;

		//xmm0[0,1,2] = 2.0f * (normal[0]*vertex[0], normal[1]*vertex[1], normal[2]*vertex[2])
		x0 = _mm_mul_ps(sse_scale_2x, sse_vertex);
		x0 = _mm_mul_ps(x0, sse_normal);
		//xmm0[0,1,2] = 2.0f * (normal[0]*vertex[0] + normal[1]*vertex[1] + normal[2]*vertex[2])
		x3 = _mm_shuffle_ps(x0, x0, 0b10010101);
		x0 = _mm_shuffle_ps(x0, x0, 0b11000000);
		x0 = _mm_add_ps(x0, x3);
		x3 = _mm_shuffle_ps(x3, x3, 0b11111111);
		x0 = _mm_add_ps(x0, x3);
		//xmm1[0,1,2] = vertex[0,1,2] - normal[0,1,2] * fdot;
		x2 = _mm_mul_ps(sse_normal, x0);
		x1 = _mm_sub_ps(sse_vertex, x2);
		*output_texcoord = x1.m128_f32[coord];
	}
}

static void TexGenFunc_SphereMap(int coord, const GLfloat* vertex, const GLfloat* normal, float* output_texcoord)
{
	if (coord == 3) {
		*output_texcoord = 1.0f;
	}
	else if (coord == 2) {
		*output_texcoord = 0.0f;
	}
	else {
		float r[3];
		float fdot;
		fdot = 2.0f * (normal[0] * vertex[0] + normal[1] * vertex[1] + normal[2] * vertex[2]);
		r[0] = vertex[0] - normal[0] * fdot;
		r[1] = vertex[1] - normal[1] * fdot;
		r[2] = vertex[2] - normal[2] * fdot + 1.0f;
		fdot = 2.0f * sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
		*output_texcoord = r[coord] / fdot + 0.5f;
	}
}

static void TexGenFunc_SphereMap_SSE(int coord, const GLfloat* vertex, const GLfloat* normal, float* output_texcoord)
{
	if (coord == 3) {
		*output_texcoord = 1.0f;
	}
	else if (coord == 2) {
		*output_texcoord = 0.0f;
	}
	else {
		__m128 sse_vertex = { vertex[0], vertex[1], vertex[2], 0 };
		__m128 sse_normal = { normal[0], normal[1], normal[2], 0 };
		__m128 sse_scale_2x = { 2.0f, 2.0f, 2.0f, 0.0f };
		__m128 sse_bias_0_0_1 = { 0.0f, 0.0f, 1.0f, 0.0f };
		__m128 sse_bias_one_half = { 0.5f, 0.5f, 0.5f, 0.0f };
		__m128 x0, x1, x2, x3;

		//xmm0[0,1,2] = 2.0f * (normal[0]*vertex[0], normal[1]*vertex[1], normal[2]*vertex[2])
		x0 = _mm_mul_ps(sse_scale_2x, sse_vertex);
		x0 = _mm_mul_ps(x0, sse_normal);
		//xmm0[0,1,2] = 2.0f * (normal[0]*vertex[0] + normal[1]*vertex[1] + normal[2]*vertex[2])
		x3 = _mm_shuffle_ps(x0, x0, 0b10010101);
		x0 = _mm_shuffle_ps(x0, x0, 0b11000000);
		x0 = _mm_add_ps(x0, x3);
		x3 = _mm_shuffle_ps(x3, x3, 0b11111111);
		x0 = _mm_add_ps(x0, x3);
		//xmm1[0,1,2] = vertex[0,1,2] - normal[0,1,2] * fdot + { 0, 0, 1 };
		x2 = _mm_mul_ps(sse_normal, x0);
		x1 = _mm_sub_ps(sse_vertex, x2);
		x1 = _mm_add_ps(x1, sse_bias_0_0_1);
		//xmm0[0,1,2] = 2.0f * sqrtf(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
		x0 = _mm_mul_ps(x1, x1);
		x3 = _mm_shuffle_ps(x0, x0, 0b11001001);
		x0 = _mm_add_ss(x0, x3);
		x3 = _mm_shuffle_ps(x3, x3, 0b11000001);
		x0 = _mm_add_ss(x0, x3);
		x0 = _mm_rsqrt_ss(x0);
		x0 = _mm_mul_ss(x0, sse_bias_one_half);
		x0 = _mm_shuffle_ps(x0, x0, 0b11000000);
		x1 = _mm_mul_ps(x1, x0);
		x1 = _mm_add_ps(x1, sse_bias_one_half);
		*output_texcoord = x1.m128_f32[coord];
	}
}

static void TexGenFunc_SphereMap_SSE_asm(int coord, const GLfloat* vertex, const GLfloat* normal, float* output_texcoord)
{
	if (coord == 3) {
		*output_texcoord = 1.0f;
	}
	else if (coord == 2) {
		*output_texcoord = 0.0f;
	}
	else {
		_declspec(align(16)) float sse_vertex[4];
		_declspec(align(16)) float sse_normal[4];
		_declspec(align(16)) float sse_scale_2x[4] = { 2.0f, 2.0f, 2.0f, 0.0f };
		_declspec(align(16)) float sse_bias_0_0_1[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
		_declspec(align(16)) float sse_bias_one_half[4] = { 0.5f, 0.5f, 0.5f, 0.0f };
		_declspec(align(16)) float out_coord[4];

		memcpy(sse_vertex, vertex, sizeof(float) * 3);
		memcpy(sse_normal, normal, sizeof(float) * 3);
		*(int*)&sse_vertex[3] = 0;
		*(int*)&sse_normal[3] = 0;

		_asm {
			movaps	xmm1, xmmword ptr[sse_vertex]
			movaps	xmm2, xmmword ptr[sse_normal]
			movaps	xmm4, xmmword ptr[sse_bias_one_half]

			//xmm0[0,1,2] = 2.0f * (normal[0]*vertex[0], normal[1]*vertex[1], normal[2]*vertex[2])
			movaps	xmm0, xmmword ptr[sse_scale_2x]
			mulps	xmm0, xmm1
			mulps	xmm0, xmm2
			//xmm0[0,1,2] = 2.0f * (normal[0]*vertex[0] + normal[1]*vertex[1] + normal[2]*vertex[2])
			movaps	xmm3, xmm0
			shufps	xmm0, xmm0, 11000000b
			shufps	xmm3, xmm3, 10010101b
			addps	xmm0, xmm3
			shufps	xmm3, xmm3, 11111111b
			addps	xmm0, xmm3
			//xmm1[0,1,2] = vertex[0,1,2] - normal[0,1,2] * fdot + { 0, 0, 1 };
			mulps	xmm2, xmm0
			subps	xmm1, xmm2
			addps	xmm1, xmmword ptr[sse_bias_0_0_1]
			//xmm0[0,1,2] = 2.0f * sqrtf(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
			movaps	xmm0, xmm1
			mulps	xmm0, xmm0
			movaps	xmm3, xmm0
			shufps	xmm3, xmm3, 11001001b
			addss	xmm0, xmm3
			shufps	xmm3, xmm3, 11000001b
			addss	xmm0, xmm3
			rsqrtss	xmm0, xmm0
			mulss	xmm0, xmm4
			shufps	xmm0, xmm0, 11000000b
			mulps	xmm1, xmm0
			addps	xmm1, xmm4
			movaps	xmmword ptr[out_coord], xmm1
		}
		*output_texcoord = out_coord[coord];
	}
}

static float get_float()
{
	int16_t a = 0, b = 0;
	random_bytes((uc8_t*)&a, sizeof(a));
	random_bytes((uc8_t*)&b, sizeof(b));
	return (float)a / b;
}

static float get_float2()
{
	float ret;
	int16_t a, b;
	random_bytes((uc8_t*)&a, sizeof(a));
	random_bytes((uc8_t*)&b, sizeof(b));
	if (a < b)
		ret = (float)a / b;
	else
		ret = (float)b / a;
	return ret;
}

void do_texgen_tests()
{
	random_init();

	objectPlane[0] = get_float();
	objectPlane[1] = get_float();
	objectPlane[2] = get_float();
	objectPlane[3] = get_float();

	eyePlane[0] = get_float();
	eyePlane[1] = get_float();
	eyePlane[2] = get_float();
	eyePlane[3] = get_float();

	float vertex[4] = { get_float() ,get_float() ,get_float() ,get_float() };
	float normal[4] = { get_float() ,get_float() ,get_float() ,get_float() };

	float scalar = 0;
	float vectorial = 0;
	TexGenFunc_ObjectLinear(vertex, NULL, &scalar);
	TexGenFunc_ObjectLinear_SSE(vertex, NULL, &vectorial);
	printf("ObjectLinear:\n%f\n%f\n", scalar, vectorial);

	TexGenFunc_EyeLinear(vertex, NULL, &scalar);
	TexGenFunc_EyeLinear_SSE(vertex, NULL, &vectorial);
	printf("EyeLinear:\n%f\n%f\n", scalar, vectorial);

	printf("ReflectionMap:\n");
	for (int i = 0; i < 3; i++)
	{
		TexGenFunc_ReflectionMap(i, vertex, normal, &scalar);
		printf("%f ", scalar);
	}
	printf("\n");
	for (int i = 0; i < 3; i++)
	{
		TexGenFunc_ReflectionMap_SSE(i, vertex, normal, &vectorial);
		printf("%f ", vectorial);
	}
	printf("\n");

	printf("SphereMap:\n");
	for (int i = 0; i < 2; i++)
	{
		TexGenFunc_SphereMap_SSE_asm(i, vertex, normal, &scalar);
		printf("%f ", scalar);
	}
	printf("\n");
	for (int i = 0; i < 2; i++)
	{
		TexGenFunc_SphereMap_SSE(i, vertex, normal, &vectorial);
		printf("%f ", vectorial);
	}
	printf("\n");
}