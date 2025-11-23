/***************************************************************************
* Copyright (C) 2011-2016, Crystice Softworks.
* 
* This file is part of QindieGL source code.
* Please note that QindieGL is not driver, it's emulator.
* 
* QindieGL source code is free software; you can redistribute it and/or 
* modify it under the terms of the GNU General Public License as 
* published by the Free Software Foundation; either version 2 of 
* the License, or (at your option) any later version.
* 
* QindieGL source code is distributed in the hope that it will be 
* useful, but WITHOUT ANY WARRANTY; without even the implied 
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
* See the GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software 
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
***************************************************************************/
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_state.hpp"
#include "d3d_utils.hpp"
#include "d3d_matrix_stack.hpp"
#include <immintrin.h>

//==================================================================================
// Texture generation
//----------------------------------------------------------------------------------
// We implement OpenGL texgen functionality in software since D3D's is very poor, 
// and we don't want to use vertex shaders
//==================================================================================

typedef void (*pfnTrVertex)( const GLfloat *vertex, float *output );
typedef void (*pfnTrNormal)( const GLfloat *normal, float *output );

static void TrVertexFunc_Copy( const GLfloat *vertex, float *output )
{
	memcpy( output, vertex, sizeof(GLfloat)*4 );
}

static void TrVertexFunc_TransformByModelview( const GLfloat *vertex, float *output )
{
	D3DXVec3TransformCoord((D3DXVECTOR3*)output, (D3DXVECTOR3*)vertex, D3DGlobal.modelviewMatrixStack->top());
}

static void TrVertexFunc_TransformByModelviewAndNormalize( const GLfloat *vertex, float *output )
{
	D3DXVec3TransformCoord((D3DXVECTOR3*)output, (D3DXVECTOR3*)vertex, D3DGlobal.modelviewMatrixStack->top());
	D3DXVec3Normalize((D3DXVECTOR3*)output, (D3DXVECTOR3*)output);
}

static void TrNormalFunc_TransformByModelview( const GLfloat *normal, float *output )
{
	D3DXVec3TransformNormal((D3DXVECTOR3*)output, (D3DXVECTOR3*)normal, D3DGlobal.modelviewMatrixStack->top().invtrans());
}

static void TexGenFunc_None( int /*stage*/, int coord, const GLfloat* /*vertex*/, const GLfloat* /*normal*/, float *output_texcoord )
{
	*output_texcoord = (coord == 3) ? 1.0f : 0.0f;
}

static void TexGenFunc_ObjectLinear( int stage, int coord, const GLfloat* vertex, const GLfloat* /*normal*/, float *output_texcoord )
{
	const float *p = D3DState.TextureState.TexGen[stage][coord].objectPlane;
	float out_coord = 0.0f;

	for (int i = 0; i < 4; ++i, ++vertex, ++p)
		out_coord += (*vertex) * (*p);
	*output_texcoord = out_coord;
}

static void TexGenFunc_ObjectLinear_SSE( int stage, int coord, const GLfloat* vertex, const GLfloat* /*normal*/, float *output_texcoord )
{
	__m128 x0, x1;
	x0 = _mm_loadu_ps(vertex);
	x0 = _mm_mul_ps(x0, _mm_loadu_ps((float*)D3DState.TextureState.TexGen[stage][coord].objectPlane));
	x1 = _mm_shuffle_ps(x0, x0, 0b11110101);
	x0 = _mm_add_ps(x0, x1);
	x1 = _mm_shuffle_ps(x0, x0, 0b00000010);
	x0 = _mm_add_ss(x0, x1);
	_mm_store_ss(output_texcoord, x0);
}

static void TexGenFunc_EyeLinear( int stage, int coord, const GLfloat* vertex, const GLfloat* /*normal*/, float *output_texcoord )
{
	const float *p = D3DState.TextureState.TexGen[stage][coord].eyePlane;
	float out_coord = 0.0f;
	for (int i = 0; i < 4; ++i, ++vertex, ++p)
		out_coord += (*vertex) * (*p);
	*output_texcoord = out_coord;
}

static void TexGenFunc_EyeLinear_SSE( int stage, int coord, const GLfloat* vertex, const GLfloat* /*normal*/, float *output_texcoord )
{
	__m128 x0, x1;
	x0 = _mm_loadu_ps(vertex);
	x0 = _mm_mul_ps(x0, _mm_loadu_ps((float*)D3DState.TextureState.TexGen[stage][coord].eyePlane));
	x1 = _mm_shuffle_ps(x0, x0, 0b11110101);
	x0 = _mm_add_ps(x0, x1);
	x1 = _mm_shuffle_ps(x0, x0, 0b00000010);
	x0 = _mm_add_ss(x0, x1);
	_mm_store_ss(output_texcoord, x0);
}

static void TexGenFunc_SphereMap( int /*stage*/, int coord, const GLfloat* vertex, const GLfloat* normal, float *output_texcoord )
{
	if (coord == 3) {
		*output_texcoord = 1.0f;
	} else if (coord == 2) {
		*output_texcoord = 0.0f;
	} else {
		float r[3];
		float fdot;
		fdot = 2.0f * (normal[0]*vertex[0] + normal[1]*vertex[1] + normal[2]*vertex[2]);
		r[0] = vertex[0] - normal[0] * fdot;
		r[1] = vertex[1] - normal[1] * fdot;
		r[2] = vertex[2] - normal[2] * fdot + 1.0f;
		fdot = 2.0f * sqrtf(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
		*output_texcoord = r[coord]/fdot + 0.5f;
	}
}

static void TexGenFunc_SphereMap_SSE( int /*stage*/, int coord, const GLfloat* vertex, const GLfloat* normal, float *output_texcoord )
{
	if (coord == 3) {
		*output_texcoord = 1.0f;
	} else if (coord == 2) {
		*output_texcoord = 0.0f;
	} else {
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

static void TexGenFunc_ReflectionMap( int /*stage*/, int coord, const GLfloat* vertex, const GLfloat* normal, float *output_texcoord )
{
	if (coord == 3) {
		*output_texcoord = 1.0f;
	} else {
		float fdot = 2.0f * (normal[0]*vertex[0] + normal[1]*vertex[1] + normal[2]*vertex[2]);
		*output_texcoord = vertex[coord] - normal[coord] * fdot;
	}
}

static void TexGenFunc_ReflectionMap_SSE( int /*stage*/, int coord, const GLfloat* vertex, const GLfloat* normal, float *output_texcoord )
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

static void TexGenFunc_NormalMap( int /*stage*/, int coord, const GLfloat* /*vertex*/, const GLfloat* normal, float *output_texcoord )
{
	if (coord == 3) {
		*output_texcoord = 1.0f;
	} else {
		*output_texcoord = normal[coord];
	}
}

void SelectTexGenFunc( int stage, int coord )
{
	switch (D3DState.TextureState.TexGen[stage][coord].mode) {
	case GL_OBJECT_LINEAR:
		D3DState.TextureState.TexGen[stage][coord].func = (D3DGlobal.settings.useSSE ? TexGenFunc_ObjectLinear_SSE : TexGenFunc_ObjectLinear);
		D3DState.TextureState.TexGen[stage][coord].trVertex = TrVertexFunc_Copy;
		D3DState.TextureState.TexGen[stage][coord].trNormal = nullptr;
		break;
	case GL_EYE_LINEAR:
		D3DState.TextureState.TexGen[stage][coord].func = (D3DGlobal.settings.useSSE ? TexGenFunc_EyeLinear_SSE : TexGenFunc_EyeLinear);
		D3DState.TextureState.TexGen[stage][coord].trVertex = TrVertexFunc_TransformByModelview;
		D3DState.TextureState.TexGen[stage][coord].trNormal = nullptr;
		break;
	case GL_SPHERE_MAP:
		D3DState.TextureState.TexGen[stage][coord].func = (D3DGlobal.settings.useSSE ? TexGenFunc_SphereMap_SSE : TexGenFunc_SphereMap);
		D3DState.TextureState.TexGen[stage][coord].trVertex = TrVertexFunc_TransformByModelviewAndNormalize;
		D3DState.TextureState.TexGen[stage][coord].trNormal = TrNormalFunc_TransformByModelview;
		break;
	case GL_REFLECTION_MAP_ARB:
		D3DState.TextureState.TexGen[stage][coord].func = (D3DGlobal.settings.useSSE ? TexGenFunc_ReflectionMap_SSE : TexGenFunc_ReflectionMap);
		D3DState.TextureState.TexGen[stage][coord].trVertex = TrVertexFunc_TransformByModelviewAndNormalize;
		D3DState.TextureState.TexGen[stage][coord].trNormal = TrNormalFunc_TransformByModelview;
		break;
	case GL_NORMAL_MAP_ARB:
		D3DState.TextureState.TexGen[stage][coord].func = TexGenFunc_NormalMap;
		D3DState.TextureState.TexGen[stage][coord].trVertex = nullptr;
		D3DState.TextureState.TexGen[stage][coord].trNormal = TrNormalFunc_TransformByModelview;
		break;
	default:
		D3DState.TextureState.TexGen[stage][coord].func = TexGenFunc_None;
		D3DState.TextureState.TexGen[stage][coord].trNormal = nullptr;
		D3DState.TextureState.TexGen[stage][coord].trNormal = nullptr;
		break;
	}
}

template<typename T>
static void SetupTexGen( int stage, int coord, GLenum pname, const T *params )
{
	switch (pname) {
	case GL_TEXTURE_GEN_MODE:
		D3DState.TextureState.TexGen[stage][coord].mode = (GLenum)params[0];
		SelectTexGenFunc( stage, coord );
		break;
	case GL_OBJECT_PLANE:
		D3DState.TextureState.TexGen[stage][coord].objectPlane[0] = (FLOAT)params[0];
		D3DState.TextureState.TexGen[stage][coord].objectPlane[1] = (FLOAT)params[1];
		D3DState.TextureState.TexGen[stage][coord].objectPlane[2] = (FLOAT)params[2];
		D3DState.TextureState.TexGen[stage][coord].objectPlane[3] = (FLOAT)params[3];
		break;
	case GL_EYE_PLANE:
		{
			D3DXPLANE d3dxPlane((GLfloat)params[0],(GLfloat)params[1],(GLfloat)params[2],(GLfloat)params[3]);
			D3DXPLANE d3dxTPlane;
			D3DXPlaneTransform( &d3dxTPlane, &d3dxPlane, D3DGlobal.modelviewMatrixStack->top().invtrans() );
			D3DState.TextureState.TexGen[stage][coord].eyePlane[0] = (FLOAT)d3dxTPlane.a;
			D3DState.TextureState.TexGen[stage][coord].eyePlane[1] = (FLOAT)d3dxTPlane.b;
			D3DState.TextureState.TexGen[stage][coord].eyePlane[2] = (FLOAT)d3dxTPlane.c;
			D3DState.TextureState.TexGen[stage][coord].eyePlane[3] = (FLOAT)d3dxTPlane.d;
		}
		break;
	default:
		logPrintf("WARNING: unknown TexGen pname - 0x%x\n", pname);
		break;
	}
}
template<typename T>
static void GetTexGen( int stage, int coord, GLenum pname, T *params )
{
	switch (pname) {
	case GL_TEXTURE_GEN_MODE:
		params[0] = (T)D3DState.TextureState.TexGen[stage][coord].mode;
		//TODO: update func
		break;
	case GL_OBJECT_PLANE:
		params[0] = (T)D3DState.TextureState.TexGen[stage][coord].objectPlane[0];
		params[1] = (T)D3DState.TextureState.TexGen[stage][coord].objectPlane[1];
		params[2] = (T)D3DState.TextureState.TexGen[stage][coord].objectPlane[2];
		params[3] = (T)D3DState.TextureState.TexGen[stage][coord].objectPlane[3];
		break;
	case GL_EYE_PLANE:
		params[0] = (T)D3DState.TextureState.TexGen[stage][coord].eyePlane[0];
		params[1] = (T)D3DState.TextureState.TexGen[stage][coord].eyePlane[1];
		params[2] = (T)D3DState.TextureState.TexGen[stage][coord].eyePlane[2];
		params[3] = (T)D3DState.TextureState.TexGen[stage][coord].eyePlane[3];
		break;
	default:
		logPrintf("WARNING: unknown TexGen pname - 0x%x\n", pname);
		break;
	}
}

OPENGL_API void WINAPI glTexGenf( GLenum coord,  GLenum pname,  GLfloat param )
{
	SetupTexGen( D3DState.TextureState.currentTMU, coord - GL_S, pname, &param );
}
OPENGL_API void WINAPI glTexGend( GLenum coord,  GLenum pname,  GLdouble param )
{
	SetupTexGen( D3DState.TextureState.currentTMU, coord - GL_S, pname, &param );
}
OPENGL_API void WINAPI glTexGeni( GLenum coord,  GLenum pname,  GLint param )
{
	SetupTexGen( D3DState.TextureState.currentTMU, coord - GL_S, pname, &param );
}
OPENGL_API void WINAPI glTexGenfv( GLenum coord,  GLenum pname,  const GLfloat *params )
{
	SetupTexGen( D3DState.TextureState.currentTMU, coord - GL_S, pname, params );
}
OPENGL_API void WINAPI glTexGendv( GLenum coord,  GLenum pname,  const GLdouble *params )
{
	SetupTexGen( D3DState.TextureState.currentTMU, coord - GL_S, pname, params );
}
OPENGL_API void WINAPI glTexGeniv( GLenum coord,  GLenum pname,  const GLint *params )
{
	SetupTexGen( D3DState.TextureState.currentTMU, coord - GL_S, pname, params );
}
OPENGL_API void WINAPI glGetTexGenfv( GLenum coord,  GLenum pname,  GLfloat *params )
{
	GetTexGen( D3DState.TextureState.currentTMU, coord - GL_S, pname, params );
}
OPENGL_API void WINAPI glGetTexGendv( GLenum coord,  GLenum pname,  GLdouble *params )
{
	GetTexGen( D3DState.TextureState.currentTMU, coord - GL_S, pname, params );
}
OPENGL_API void WINAPI glGetTexGeniv( GLenum coord,  GLenum pname,  GLint *params )
{
	GetTexGen( D3DState.TextureState.currentTMU, coord - GL_S, pname, params );
}