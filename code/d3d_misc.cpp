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

//==================================================================================
// Misc functions
//==================================================================================

OPENGL_API void WINAPI glClear( GLbitfield mask )
{
	if (!D3DGlobal.initialized) {
		D3DGlobal.lastError = E_FAIL;
		return;
	}
	DWORD clearMask = 0;
	if (mask & GL_COLOR_BUFFER_BIT) clearMask |= D3DCLEAR_TARGET;
	if (mask & GL_DEPTH_BUFFER_BIT) clearMask |= D3DCLEAR_ZBUFFER;
	if (mask & GL_STENCIL_BUFFER_BIT) clearMask |= D3DCLEAR_STENCIL;

	HRESULT hr = D3DGlobal.pDevice->Clear( 0, nullptr, clearMask & ~(D3DGlobal.ignoreClearMask), D3DState.ColorBufferState.clearColor, D3DState.DepthBufferState.clearDepth, D3DState.StencilBufferState.clearStencil );
	if (FAILED(hr)) D3DGlobal.lastError = hr;
}
OPENGL_API void WINAPI glClearColor( GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha )
{
	DWORD da = (DWORD)(alpha * 255);
	DWORD dr = (DWORD)(red * 255);
	DWORD dg = (DWORD)(green * 255);
	DWORD db = (DWORD)(blue * 255);
	D3DState.ColorBufferState.clearColor = D3DCOLOR_ARGB( da, dr, dg, db );
}
OPENGL_API void WINAPI glClearDepth( GLclampd depth )
{
	D3DState.DepthBufferState.clearDepth = (float)depth;
}
OPENGL_API void WINAPI glClearStencil( GLint s )
{
	D3DState.StencilBufferState.clearStencil = s;
}
OPENGL_API void WINAPI glClearIndex( GLfloat )
{
	// d3d doesn't support color index mode
}
OPENGL_API void WINAPI glClearAccum( GLfloat, GLfloat, GLfloat, GLfloat )
{
	// d3d doesn't support accumulator
	logPrintf("WARNING: glClearAccum: accumulator is not supported\n");
}
OPENGL_API void WINAPI glColorMask( GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha )
{
	DWORD mask = 0;
	if (red) mask |= D3DCOLORWRITEENABLE_RED;
	if (green) mask |= D3DCOLORWRITEENABLE_GREEN;
	if (blue) mask |= D3DCOLORWRITEENABLE_BLUE;
	if (alpha) mask |= D3DCOLORWRITEENABLE_ALPHA;
	if (mask != D3DState.ColorBufferState.colorWriteMask) {
		D3DState.ColorBufferState.colorWriteMask = mask;
		D3DState_SetRenderState( D3DRS_COLORWRITEENABLE, mask );
	}
}
OPENGL_API void WINAPI glCullFace( GLenum mode )
{
	if (D3DState.PolygonState.cullMode != mode) {
		D3DState.PolygonState.cullMode = mode;
		D3DState_SetCullMode();
	}
}
OPENGL_API void WINAPI glFrontFace( GLenum mode )
{
	if (D3DState.PolygonState.frontFace != mode) {
		D3DState.PolygonState.frontFace = mode;
		D3DState_SetCullMode();
	}
}
OPENGL_API void WINAPI glDepthFunc( GLenum func )
{
	DWORD dfunc = UTIL_GLtoD3DCmpFunc(func);
	if (dfunc != D3DState.DepthBufferState.depthTestFunc) {
		D3DState.DepthBufferState.depthTestFunc = dfunc;
		D3DState_SetRenderState( D3DRS_ZFUNC, dfunc );
	}
}
OPENGL_API void WINAPI glDepthMask( GLboolean flag )
{
	if (D3DState.DepthBufferState.depthWriteMask != flag) {
		D3DState.DepthBufferState.depthWriteMask = flag;
		D3DState_SetRenderState( D3DRS_ZWRITEENABLE, flag );
	}
}
OPENGL_API void WINAPI glDepthRange( GLclampd zNear, GLclampd zFar )
{
	D3DState.viewport.MinZ = (float)zNear;
	D3DState.viewport.MaxZ = (float)zFar;
	if (!D3DGlobal.initialized) {
		D3DGlobal.lastError = E_FAIL;
		return;
	}
	HRESULT hr = D3DGlobal.pDevice->SetViewport(&D3DState.viewport);
	if (FAILED(hr)) D3DGlobal.lastError = hr;
}
OPENGL_API void WINAPI glIndexMask( GLuint )
{
	// d3d doesn't support color index mode
}
OPENGL_API void WINAPI glDrawBuffer( GLenum )
{
	// d3d doesn't like us messing with the front buffer, so here
	// we just silently ignore requests to change the draw buffer
}
OPENGL_API void WINAPI glReadBuffer( GLenum )
{
	// d3d doesn't like us messing with the front buffer, so here
	// we just silently ignore requests to change the draw buffer
}
OPENGL_API void WINAPI glPolygonMode( GLenum face, GLenum mode )
{
	if (face != GL_FRONT_AND_BACK) {
		logPrintf("WARNING: glPolygonMode: only GL_FRONT_AND_BACK is supported\n");
	}

	const DWORD dmode = mode + 1 - GL_POINT;

	if (D3DState.PolygonState.fillMode != dmode) {
		D3DState.PolygonState.fillMode = dmode;
		D3DState_SetRenderState( D3DRS_FILLMODE, dmode );
	}
}
OPENGL_API void WINAPI glPolygonOffset( GLfloat factor, GLfloat units )
{
	//WG: not sure about these values, but it solved the decals looking wrong from a distance in Wolf
	D3DState.PolygonState.depthBiasFactor = factor; //-factor * 0.0025f;
	D3DState.PolygonState.depthBiasUnits = units / 250000.0f; //units * 0.000125f;
	D3DState_SetDepthBias();
}
OPENGL_API void WINAPI glPolygonStipple( const GLubyte* )
{
	static bool warningPrinted = false;
	if (!warningPrinted) {
		warningPrinted = true;
		logPrintf("WARNING: glPolygonStipple is not supported\n");
	}
}
OPENGL_API void WINAPI glGetPolygonStipple( GLubyte* )
{
	static bool warningPrinted = false;
	if (!warningPrinted) {
		warningPrinted = true;
		logPrintf("WARNING: glGetPolygonStipple is not supported\n");
	}
}
OPENGL_API void WINAPI glLineStipple( GLint, GLushort )
{
	static bool warningPrinted = false;
	if (!warningPrinted) {
		warningPrinted = true;
		logPrintf("WARNING: glLineStipple is not supported\n");
	}
}
OPENGL_API void WINAPI glLineWidth( GLfloat )
{
	static bool warningPrinted = false;
	if (!warningPrinted) {
		warningPrinted = true;
		logPrintf("WARNING: glLineWidth is not supported\n");
	}
}
OPENGL_API void WINAPI glShadeModel( GLenum mode )
{
	const DWORD dmode = (mode == GL_FLAT) ? D3DSHADE_FLAT : D3DSHADE_GOURAUD;

	if (dmode != D3DState.LightingState.shadeMode) {
		D3DState.LightingState.shadeMode = dmode;
		D3DState_SetRenderState( D3DRS_SHADEMODE, dmode );
	}
}
OPENGL_API void WINAPI glPointSize( GLfloat size )
{
	size = QINDIEGL_MAX( QINDIEGL_MIN( size, D3DGlobal.hD3DCaps.MaxPointSize ), 1.0f );
	if (D3DState.PointState.pointSize != size) {
		D3DState.PointState.pointSize = size;
		D3DState_SetRenderState( D3DRS_POINTSIZE, UTIL_FloatToDword(size) );
	}
}
OPENGL_API void WINAPI glAccum( GLenum, GLfloat )
{
	// d3d doesn't support accumulator
	logPrintf("WARNING: glAccum: accumulator is not supported\n");
}
OPENGL_API void WINAPI glFlush()
{
	// ignore
}
OPENGL_API void WINAPI glFinish()
{
	// we force a Present in our SwapBuffers function so this is unneeded
}
OPENGL_API void WINAPI glViewport( GLint x, GLint y, GLsizei width, GLsizei height )
{
	// translate from OpenGL bottom-left to D3D top-left
	y = D3DGlobal.hCurrentMode.Height - (height + y);

	// WG: need a better way to handle this without producing artifacts
	D3DState.viewport.X = (x < 0) ? 0 : x;
	D3DState.viewport.Y = (y < 0) ? 0 : y;
	D3DState.viewport.Width = width;
	D3DState.viewport.Height = height;
	if (!D3DGlobal.initialized) {
		D3DGlobal.lastError = E_FAIL;
		return;
	}
	HRESULT hr = D3DGlobal.pDevice->SetViewport(&D3DState.viewport);
	if (FAILED(hr)) D3DGlobal.lastError = hr;

	if ( D3DGlobal.settings.game.orthovertexshader )
	{
		float texelOffset[4] =  {-1.0f/D3DState.viewport.Width, 1.0f/D3DState.viewport.Height, 0.0f, 0.0f};
		D3DGlobal.pDevice->SetVertexShaderConstantF(0, texelOffset, 1);
	}

	if (x < 0 || y < 0)
	{
		int tx = (x >= 0) ? 0 : x;
		int ty = (y >= 0) ? 0 : y;
		D3DMATRIX *pm = D3DGlobal.projectionMatrixStack->top();
		// for idtech3, in ortho case, first it sets glViewport then calls glOrtho, so we store the offsets,
		//  and they will be used there; the matrix already stored is old and does not make sense to be checked;
		//  for perspective case, matrix is loaded first, then Viewport is set so we can modify the mat here
		D3DState.viewport_offX = tx;
		D3DState.viewport_offY = ty;
		if (pm->m[2][3] >= 0)
		{
			//ortho case
			// We could handle the offs here, but in idtech3, they are setting Viewport before glOrtho,
			//   and that means it overwrites any set we do here, plus we have the old matrix anyway;
			//   if other games set glOrtho and then glViewport we need this activated
			//float tw = 2 / pm->m[0][0];
			//float th = -2 / pm->m[1][1];
			//float zn = pm->m[3][2] / pm->m[2][2];
			//float zf = zn - 1/pm->m[2][2];
			//D3DXMATRIX mat;
			//D3DXMatrixOrthoOffCenterRH(&mat, tx, tx + tw, ty + th, ty, zn, zf);
			//D3DGlobal.projectionMatrixStack->load(&mat.m[0][0]);
		}
		else
		{
			//perspective case
			//we need to adjust the projection matrix to an offcenter proj matrix
			// What if game loads a new matrix after this call?
			float x0 = 2.0f * (-(float)tx) / width;
			float x1 = 2.0f * ((float)ty) / height;
			pm->m[2][0] = x0;
			pm->m[2][1] = x1;
		}
	}
	else
	{
		D3DState.viewport_offX = 0;
		D3DState.viewport_offY = 0;
	}
}
OPENGL_API void WINAPI glScissor( GLint x, GLint y, GLsizei width, GLsizei height )
{
	// translate from OpenGL bottom-left to D3D top-left
	y = D3DGlobal.hCurrentMode.Height - (height + y);

	int newx = (x < 0) ? 0 : x;
	int newy = (y < 0) ? 0 : y;
	D3DState.ScissorState.scissorRect.left = newx;
	D3DState.ScissorState.scissorRect.right = newx + width;
	D3DState.ScissorState.scissorRect.top = newy;
	D3DState.ScissorState.scissorRect.bottom = newy + height;
	if (!D3DGlobal.initialized) {
		D3DGlobal.lastError = E_FAIL;
		return;
	}
	HRESULT hr = D3DGlobal.pDevice->SetScissorRect(&D3DState.ScissorState.scissorRect);
	if (FAILED(hr)) D3DGlobal.lastError = hr;
}
OPENGL_API void WINAPI glDebugEntry( DWORD, DWORD )
{
	//what the hell is that?
}
OPENGL_API void WINAPI glPushDebugGroup( GLenum source, GLuint id, GLsizei length, const char* message )
{
	_CRT_UNUSED( source ); _CRT_UNUSED( length );

	static WCHAR wmessage[2048];
	if ( D3DGlobal.dbgBeginEvent )
	{
		D3DCOLOR color = id;
		mbstowcs( wmessage, message, ARRAYSIZE( wmessage ) );
		D3DGlobal.dbgBeginEvent( color, wmessage );
	}
}
OPENGL_API void WINAPI glPopDebugGroup( void )
{
	if ( D3DGlobal.dbgEndEvent )
		D3DGlobal.dbgEndEvent();
}
