/***************************************************************************
* Copyright ( C ) 2011-2016, Crystice Softworks.
* 
* This file is part of QindieGL source code.
* Please note that QindieGL is not driver, it's emulator.
* 
* QindieGL source code is free software; you can redistribute it and/or 
* modify it under the terms of the GNU General Public License as 
* published by the Free Software Foundation; either version 2 of 
* the License, or ( at your option ) any later version.
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
#include "d3d_immediate.hpp"
#include "d3d_lists.hpp"
#include "d3d_arb_program.hpp"
#include "d3d_extension.hpp"
#include "d3d_texture.hpp"

namespace {
	// Program 8 is YAE's full-screen pickup blur.  Keep a very small trace
	// window around it so that the following fixed-function composite can be
	// inspected without enabling per-draw TRACE logging for the whole level.
	uint64_t gYAEPostImmediateLastFrame = ~uint64_t( 0 );
	unsigned int gYAEPostImmediateLogs = 0;
}

//==================================================================================
// OpenGL Immediate Mode
//==================================================================================

D3DIMBuffer :: D3DIMBuffer( )
{
	m_maxVertexCount = c_IMBufferGrowSize;
	m_pBuffer = ( D3DIMBufferVertex* )UTIL_Alloc( m_maxVertexCount * sizeof( D3DIMBufferVertex ) );
	m_bBegan = false;
	m_bXYZW = false;
	for (int i = 0; i < c_MaxSwapFrame; ++i) {
		m_pVertexBuffer[i] = nullptr;
		m_vbAllocSize[i] = 0;
	}
	m_swapFrame = 0;
}

D3DIMBuffer :: ~D3DIMBuffer( )
{
	UTIL_Free( m_pBuffer );
	for ( int i = 0; i < c_MaxSwapFrame; ++i ) {
		if ( m_pVertexBuffer[i] ) {
			m_pVertexBuffer[i]->Release();
		}
	}
}

void D3DIMBuffer :: TraceYAEPostImmediateComposite()
{
	if ( !D3DGlobal.settings.game.yaeFallbackCompatibility || !m_pBuffer ||
		m_primitiveType != GL_QUADS || m_passedVertexCount != 4 || m_vertexCount < 6 ||
		gYAEPostImmediateLogs >= 384 )
		return;

	const uint64_t frame = QGL_DiagnosticsGetFrameId();
	const bool blurProgram = D3DState.EnableState.fragmentProgramEnabled &&
		ARB_GetBoundFragmentProgram() == 8;
	if ( blurProgram )
		gYAEPostImmediateLastFrame = frame;

	// Once program 8 has appeared, include the rest of this frame and the
	// first two draw frames after it.  Repeated blur frames naturally keep
	// the window alive while the pickup effect is active.
	if ( gYAEPostImmediateLastFrame == ~uint64_t( 0 ) ||
		frame > gYAEPostImmediateLastFrame + 2 )
		return;

	unsigned int minimumAlpha = 255;
	unsigned int maximumAlpha = 0;
	float minimumX = m_pBuffer[0].position[0];
	float maximumX = minimumX;
	float minimumY = m_pBuffer[0].position[1];
	float maximumY = minimumY;
	for ( int i = 0; i < m_vertexCount; ++i ) {
		const unsigned int alpha = ( m_pBuffer[i].color >> 24 ) & 0xFF;
		if ( alpha < minimumAlpha ) minimumAlpha = alpha;
		if ( alpha > maximumAlpha ) maximumAlpha = alpha;
		if ( m_pBuffer[i].position[0] < minimumX ) minimumX = m_pBuffer[i].position[0];
		if ( m_pBuffer[i].position[0] > maximumX ) maximumX = m_pBuffer[i].position[0];
		if ( m_pBuffer[i].position[1] < minimumY ) minimumY = m_pBuffer[i].position[1];
		if ( m_pBuffer[i].position[1] > maximumY ) maximumY = m_pBuffer[i].position[1];
	}

	// The blur itself is already traced in d3d_diagnostics.cpp.  Log only a
	// few anchor samples of it, but retain every plausible alpha composite.
	const bool alphaCandidate = D3DState.EnableState.alphaBlendEnabled || minimumAlpha < 255;
	if ( !alphaCandidate && ( !blurProgram || gYAEPostImmediateLogs >= 4 ) )
		return;

	DWORD d3dBlendEnabled = 0;
	DWORD d3dSourceBlend = 0;
	DWORD d3dDestinationBlend = 0;
	DWORD d3dBlendOperation = 0;
	DWORD d3dSeparateAlphaEnabled = 0;
	DWORD d3dSourceBlendAlpha = 0;
	DWORD d3dDestinationBlendAlpha = 0;
	D3DGlobal.pDevice->GetRenderState( D3DRS_ALPHABLENDENABLE, &d3dBlendEnabled );
	D3DGlobal.pDevice->GetRenderState( D3DRS_SRCBLEND, &d3dSourceBlend );
	D3DGlobal.pDevice->GetRenderState( D3DRS_DESTBLEND, &d3dDestinationBlend );
	D3DGlobal.pDevice->GetRenderState( D3DRS_BLENDOP, &d3dBlendOperation );
	D3DGlobal.pDevice->GetRenderState( D3DRS_SEPARATEALPHABLENDENABLE,
		&d3dSeparateAlphaEnabled );
	D3DGlobal.pDevice->GetRenderState( D3DRS_SRCBLENDALPHA, &d3dSourceBlendAlpha );
	D3DGlobal.pDevice->GetRenderState( D3DRS_DESTBLENDALPHA,
		&d3dDestinationBlendAlpha );

	++gYAEPostImmediateLogs;
	logPrintfLevel( QGL_LOG_INFO, "YAE_POST_COMPOSITE",
		"sample=%u frame=%llu draw=%llu blur=%u programs=%u/%u enabled=%u/%u bbox=(%.3f,%.3f)-(%.3f,%.3f) alpha=%u..%u colors=%08X/%08X/%08X/%08X glBlend=%u 0x%X/0x%X d3dBlend=%u %u/%u op=%u separate=%u %u/%u textureSamplers=%u",
		gYAEPostImmediateLogs,
		static_cast<unsigned long long>( frame ),
		static_cast<unsigned long long>( QGL_DiagnosticsGetDrawId() ),
		blurProgram ? 1u : 0u,
		ARB_GetBoundVertexProgram(), ARB_GetBoundFragmentProgram(),
		D3DState.EnableState.vertexProgramEnabled,
		D3DState.EnableState.fragmentProgramEnabled,
		minimumX, minimumY, maximumX, maximumY,
		minimumAlpha, maximumAlpha,
		m_pBuffer[0].color, m_pBuffer[1].color, m_pBuffer[2].color, m_pBuffer[5].color,
		D3DState.EnableState.alphaBlendEnabled,
		D3DState.ColorBufferState.glBlendSrc,
		D3DState.ColorBufferState.glBlendDst,
		d3dBlendEnabled, d3dSourceBlend, d3dDestinationBlend, d3dBlendOperation,
		d3dSeparateAlphaEnabled, d3dSourceBlendAlpha, d3dDestinationBlendAlpha,
		D3DState.TextureState.currentSamplerCount );
}

void D3DIMBuffer :: EnsureBufferSize( int numVerts )
{
	if ( m_maxVertexCount >= m_vertexCount + numVerts )
		return;
	m_maxVertexCount += c_IMBufferGrowSize;
	m_pBuffer = ( D3DIMBufferVertex* )UTIL_Realloc( m_pBuffer, m_maxVertexCount * sizeof( D3DIMBufferVertex ) );
}

UINT D3DIMBuffer :: ReorderBufferToFVF( int fvf, int fvfsz )
{
	const D3DIMBufferVertex *src = m_pBuffer;
	float *dst = nullptr;
	HRESULT hr;

	if ( m_vbAllocSize[m_swapFrame] < m_vertexCount * fvfsz )
	{
		if ( m_pVertexBuffer[m_swapFrame] ) {
			m_pVertexBuffer[m_swapFrame]->Release();
			m_pVertexBuffer[m_swapFrame] = nullptr;
		}

		m_vbAllocSize[m_swapFrame] = QINDIEGL_MAX( c_IMBufferGrowSize, m_vertexCount ) * fvfsz;
		hr = D3DGlobal.pDevice->CreateVertexBuffer( m_vbAllocSize[m_swapFrame], D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
			0, D3DPOOL_DEFAULT, &m_pVertexBuffer[m_swapFrame], nullptr );
		if ( FAILED( hr ) )
		{
			m_pVertexBuffer[m_swapFrame] = nullptr;
			m_vbAllocSize[m_swapFrame] = 0;
			QGL_SET_ERROR(hr);
			return 0;
		}
	}

	hr = m_pVertexBuffer[m_swapFrame]->Lock( 0, m_vertexCount * fvfsz, 
		(void**)&dst, D3DLOCK_DISCARD );
	if (FAILED(hr))
	{
		QGL_SET_ERROR(hr);
		return 0;
	}

	const D3DXMATRIX *softwareTransforms[MAX_D3D_TMU] = {};
	for ( int stage = 0; stage < D3DGlobal.maxActiveTMU; ++stage )
		softwareTransforms[stage] = D3DState_GetSoftwareTextureTransform( stage );

	for ( int i = 0; i < m_vertexCount; ++i ) {
		if ( m_bXYZW == false ) /* D3DFVF_XYZ */ {
			memcpy( dst, src->position, sizeof(FLOAT)*3 );
			dst += 3;
		} else /* D3DFVF_XYZW */ {
			memcpy( dst, src->position, sizeof(FLOAT)*4 );
			dst += 4;
		}
		if ( fvf & D3DFVF_NORMAL ) {
			memcpy( dst, src->normal, sizeof(FLOAT)*3 );
			dst += 3;
		}
		if ( fvf & D3DFVF_DIFFUSE ) {
			*( DWORD* )dst = src->color;
			++dst;
		}
		if ( fvf & D3DFVF_SPECULAR ) {
			*( DWORD* )dst = src->color2;
			++dst;
		}

		for ( int j = 0; j < D3DGlobal.maxActiveTMU; ++j ) {
			int numCoords = 4;
			if ( D3DState.TextureState.transformEnabled == FALSE )
			{
				numCoords = (DWORD( D3DState.CurrentState.isSet.bits.texcoord ) >> (j * 2)) & 0x3;
				numCoords++;
			}
			if ( m_samplerMask & ( 1 << j ) ) {
				memcpy( dst, src->texCoord[j], sizeof(FLOAT)*numCoords );
				const D3DXMATRIX *transform = softwareTransforms[j];
				if ( transform && numCoords >= 2 ) {
					const float s = dst[0];
					const float t = dst[1];
					dst[0] = s * transform->_11 + t * transform->_21 + transform->_41;
					dst[1] = s * transform->_12 + t * transform->_22 + transform->_42;
				}
				float rectangleScale[2];
				if ( D3DTex_GetFixedFunctionRectangleScale( j, rectangleScale ) ) {
					if ( numCoords > 0 ) dst[0] *= rectangleScale[0];
					if ( numCoords > 1 ) dst[1] *= rectangleScale[1];
				}
				dst += numCoords;
			}
		}
		++src;
	}

	m_pVertexBuffer[m_swapFrame]->Unlock();

	return 1;
}

void D3DIMBuffer :: Begin( GLenum primType )
{
	m_primitiveType = primType;
	m_vertexCount = 0;
	m_passedVertexCount = 0;
	m_bBegan = true;
	m_bXYZW = false;
}

void D3DIMBuffer :: End( bool recordDraw )
{
	if ( !m_vertexCount || !m_bBegan ) 
		return;
	if ( D3DGlobal.deviceLost ) {
		m_bBegan = false;
		m_vertexCount = 0;
		m_passedVertexCount = 0;
		return;
	}

	if (recordDraw && !QGL_DiagnosticsBeginDraw("glBegin/glEnd", m_primitiveType,
		m_passedVertexCount, 0, 0, nullptr)) {
		m_bBegan = false;
		m_vertexCount = 0;
		return;
	}

	if ( D3DGlobal.settings.game.orthoskipuntextureddraws && !D3DState.TextureState.currentSamplerCount )
	{
		if ( D3DGlobal_IsOrthoProjection() )
		{
			m_bBegan = false;
			return;
		}
	}

	if ( m_primitiveType == GL_LINE_LOOP ) {
		//close line
		EnsureBufferSize( 1 );
		D3DIMBufferVertex *pVertex = &m_pBuffer[m_vertexCount];
		++m_vertexCount;
		memcpy( pVertex, m_pBuffer, sizeof( D3DIMBufferVertex ) );
	}

	//build FVF
	int iFVF = 0;
	int sizeFVF = 0;
	if ( m_bXYZW )
	{
		PRINT_ONCE("WARNING: Homogenous coordinates are used in immediate mode\n");
		iFVF |= D3DFVF_XYZW;
		sizeFVF += 4 * sizeof( float );
	}
	else
	{
		iFVF |= D3DFVF_XYZ;
		sizeFVF += 3 * sizeof( float );
	}
	if ( D3DState.CurrentState.isSet.bits.norm )
	{
		iFVF |= D3DFVF_NORMAL;
		sizeFVF += 3 * sizeof( float );
	}
	//if ( D3DState.CurrentState.isSet.bits.color )
	//WG: always set color
	{
		iFVF |= D3DFVF_DIFFUSE;
		sizeFVF += sizeof( DWORD );
	}

	if ( ( D3DState.EnableState.fogEnabled && D3DState.FogState.fogCoordMode ) ||
		D3DState.EnableState.colorSumEnabled )
	{
		iFVF |= D3DFVF_SPECULAR;
		sizeFVF += sizeof( DWORD );
	}

	int numSamplers = 0;
	m_samplerMask = 0;
	const int arbTexCoordCount = ARB_GetRequiredVertexTexCoordCount();
	for ( int i = 0; i < D3DGlobal.maxActiveTMU; ++i ) {
		if ( i < arbTexCoordCount || D3DState.EnableState.textureEnabled[i] ) {
			m_samplerMask |= ( 1 << i );
			int numCoordsX = D3DState.TextureState.transformEnabled ? 3 :
				(DWORD( D3DState.CurrentState.isSet.bits.texcoord ) >> (i * 2)) & 0x3;
			switch ( numCoordsX )
			{
			case 0:
				iFVF |= D3DFVF_TEXCOORDSIZE1( numSamplers );
				sizeFVF += 1 * sizeof( float );
				break;
			case 1:
				iFVF |= D3DFVF_TEXCOORDSIZE2( numSamplers );
				sizeFVF += 2 * sizeof( float );
				break;
			case 2:
				iFVF |= D3DFVF_TEXCOORDSIZE3( numSamplers );
				sizeFVF += 3 * sizeof( float );
				break;
			case 3:
				iFVF |= D3DFVF_TEXCOORDSIZE4( numSamplers );
				sizeFVF += 4 * sizeof( float );
				break;
			}
			++numSamplers;
		}
	}
	iFVF |= ( numSamplers << D3DFVF_TEXCOUNT_SHIFT );
	if ( D3DGlobal.settings.game.yaeFallbackCompatibility &&
		D3DState.EnableState.fragmentProgramEnabled &&
		ARB_GetBoundFragmentProgram() == 8 ) {
		static unsigned int postFVFLogs = 0;
		if ( postFVFLogs++ < 2 ) {
			logPrintfLevel( QGL_LOG_INFO, "YAE_POST_EFFECT",
				"immediate FVF=0x%08X stride=%d samplers=%d mask=0x%X vertices=%d texcoordBits=0x%X",
				iFVF, sizeFVF, numSamplers, m_samplerMask, m_vertexCount,
				D3DState.CurrentState.isSet.bits.texcoord );
			for ( int vertex = 0; vertex < m_vertexCount; ++vertex ) {
				const D3DIMBufferVertex& v = m_pBuffer[vertex];
				logPrintfLevel( QGL_LOG_INFO, "YAE_POST_EFFECT",
					"v%d pos=(%.3f,%.3f,%.3f,%.3f) tc0=(%.3f,%.3f) tc1=(%.3f,%.3f) tc2=(%.3f,%.3f) tc3=(%.3f,%.3f) tc4=(%.3f,%.3f)",
					vertex, v.position[0], v.position[1], v.position[2], v.position[3],
					v.texCoord[0][0], v.texCoord[0][1], v.texCoord[1][0], v.texCoord[1][1],
					v.texCoord[2][0], v.texCoord[2][1], v.texCoord[3][0], v.texCoord[3][1],
					v.texCoord[4][0], v.texCoord[4][1] );
			}
		}
	}
	TraceYAEPostImmediateComposite();

	HRESULT hr = D3DGlobal.pDevice->SetFVF( iFVF );
	if ( FAILED( hr ) ) {
		QGL_DiagnosticsRecordD3DFailure("IDirect3DDevice9::SetFVF", hr);
		QGL_SET_ERROR(hr);
		m_vertexCount = 0;
		return;
	}

	//reorder buffer, so it will contain a properly aligned data according to FVF
	if ( ReorderBufferToFVF( iFVF, sizeFVF ) )
	{
		hr = D3DGlobal.pDevice->SetStreamSource( 0, m_pVertexBuffer[m_swapFrame], 0, sizeFVF );
		if (FAILED(hr)) {
			QGL_DiagnosticsRecordD3DFailure("IDirect3DDevice9::SetStreamSource", hr);
			QGL_SET_ERROR(hr);
		}

		switch ( m_primitiveType )
		{
		case GL_POINTS:
			hr = D3DGlobal.pDevice->DrawPrimitive( D3DPT_POINTLIST, 0, m_vertexCount );
			//hr = D3DGlobal.pDevice->DrawPrimitiveUP( D3DPT_POINTLIST, m_vertexCount, m_pBuffer, vertexSize );
			break;

		case GL_LINES:
			hr = D3DGlobal.pDevice->DrawPrimitive( D3DPT_LINELIST, 0, m_vertexCount / 2 );
			//hr = D3DGlobal.pDevice->DrawPrimitiveUP( D3DPT_LINELIST, m_vertexCount / 2, m_pBuffer, vertexSize );
			break;

		case GL_LINE_STRIP:
		case GL_LINE_LOOP:
			hr = D3DGlobal.pDevice->DrawPrimitive( D3DPT_LINESTRIP, 0, m_vertexCount - 1 );
			//hr = D3DGlobal.pDevice->DrawPrimitiveUP( D3DPT_LINESTRIP, m_vertexCount - 1, m_pBuffer, vertexSize );
			break;

		case GL_QUADS:
			// quads are converted to triangles while specifying vertices
		case GL_TRIANGLES:
			// D3DPT_TRIANGLELIST models GL_TRIANGLES when used for either a single triangle or multiple triangles
			hr = D3DGlobal.pDevice->DrawPrimitive( D3DPT_TRIANGLELIST, 0, m_vertexCount / 3 );
			//hr = D3DGlobal.pDevice->DrawPrimitiveUP( D3DPT_TRIANGLELIST, m_vertexCount / 3, m_pBuffer, vertexSize );
			break;

		case GL_QUAD_STRIP:
			// quadstrip is EXACT the same as tristrip
		case GL_TRIANGLE_STRIP:
			// regular tristrip
			hr = D3DGlobal.pDevice->DrawPrimitive( D3DPT_TRIANGLESTRIP, 0, m_vertexCount - 2 );
			//hr = D3DGlobal.pDevice->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, m_vertexCount - 2, m_pBuffer, vertexSize );
			break;

		case GL_POLYGON:
			// a GL_POLYGON has the same vertex layout and order as a trifan, and can be used interchangably in OpenGL
		case GL_TRIANGLE_FAN:
			// regular trifan
			hr = D3DGlobal.pDevice->DrawPrimitive( D3DPT_TRIANGLEFAN, 0, m_vertexCount - 2 );
			//hr = D3DGlobal.pDevice->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, m_vertexCount - 2, m_pBuffer, vertexSize );
			break;

		default:
			// unsupported mode
			logPrintf( "WARNING: glBegin - unsupported mode 0x%x\n", m_primitiveType );
			break;
		}

		if (FAILED(hr)) {
			QGL_DiagnosticsRecordD3DFailure("IDirect3DDevice9::DrawPrimitive", hr);
			QGL_SET_ERROR(hr);
		}
		QGL_DiagnosticsAfterDraw();
	}

	++m_swapFrame;
	if (m_swapFrame >= c_MaxSwapFrame)
		m_swapFrame = 0;

	m_bBegan = false;
}

void D3DIMBuffer :: SetupTexCoords( D3DIMBufferVertex *pVertex, int stage )
{
	if ( !D3DState.EnableState.texGenEnabled[stage] ) {
		memcpy( pVertex->texCoord[stage], D3DState.CurrentState.currentTexCoord[stage], sizeof(FLOAT)*4 );
		return;
	}

	const float *in_coords = D3DState.CurrentState.currentTexCoord[stage];
	float *out_coords = pVertex->texCoord[stage];

	GLenum currentGen( ~0u );
	float tr_position[4];
	float tr_normal[3];

	for ( int i = 0; i < 4; ++i, ++in_coords, ++out_coords ) {
		if ( !( D3DState.EnableState.texGenEnabled[stage] & ( 1 << i ) ) ) {
			*out_coords = *in_coords;
		} else {
			if ( currentGen != D3DState.TextureState.TexGen[stage][i].mode ) {
				if ( D3DState.TextureState.TexGen[stage][i].trVertex )
					D3DState.TextureState.TexGen[stage][i].trVertex( pVertex->position, tr_position );
				if ( D3DState.TextureState.TexGen[stage][i].trNormal )
					D3DState.TextureState.TexGen[stage][i].trNormal( pVertex->normal, tr_normal );
				currentGen = D3DState.TextureState.TexGen[stage][i].mode;
			}
			D3DState.TextureState.TexGen[stage][i].func( stage, i, tr_position, tr_normal, out_coords );
		}
	}
}

void D3DIMBuffer :: AddVertex( float x, float y, float z )
{
	if ( !m_bBegan ) return;

	//if we finalize a quad, add two additional vertices so we will
	//be able to render it as triangles
	if ( ( m_primitiveType == GL_QUADS ) && ( ( m_passedVertexCount % 4 ) == 3 ) ) {
		EnsureBufferSize( 3 );
		memcpy( m_pBuffer + m_vertexCount, m_pBuffer + m_vertexCount - 3, sizeof( D3DIMBufferVertex ) );
		memcpy( m_pBuffer + m_vertexCount + 1, m_pBuffer + m_vertexCount - 1, sizeof( D3DIMBufferVertex ) );
		m_vertexCount += 2;
	} else {
		EnsureBufferSize( 1 );
	}

	D3DIMBufferVertex *pVertex = &m_pBuffer[m_vertexCount];
	++m_vertexCount;

	pVertex->position[0] = x;// +0.5f / D3DState.viewport.Width;
	pVertex->position[1] = y;// -0.5f / D3DState.viewport.Height;
	pVertex->position[2] = z;
	pVertex->position[3] = 1.0f;
	pVertex->color = D3DState.CurrentState.currentColor;
	pVertex->color2 = D3DState.CurrentState.currentColor2;
	memcpy( pVertex->normal, D3DState.CurrentState.currentNormal, sizeof( D3DState.CurrentState.currentNormal ) );

	const int arbTexCoordCount = ARB_GetRequiredVertexTexCoordCount();
	for ( int i = 0; i < D3DGlobal.maxActiveTMU; ++i ) {
		if ( i < arbTexCoordCount || D3DState.EnableState.textureEnabled[i] ) {
			SetupTexCoords( pVertex, i );
		}
	}

	++m_passedVertexCount;
}

void D3DIMBuffer :: AddVertex( float x, float y, float z, float w )
{
	if ( !m_bBegan ) return;

	//if we finalize a quad, add two additional vertices so we will
	//be able to render it as triangles
	if ( ( m_primitiveType == GL_QUADS ) && ( ( m_passedVertexCount % 4 ) == 3 ) ) {
		EnsureBufferSize( 3 );
		memcpy( m_pBuffer + m_vertexCount, m_pBuffer + m_vertexCount - 3, sizeof( D3DIMBufferVertex ) );
		memcpy( m_pBuffer + m_vertexCount + 1, m_pBuffer + m_vertexCount - 1, sizeof( D3DIMBufferVertex ) );
		m_vertexCount += 2;
	} else {
		EnsureBufferSize( 1 );
	}

	D3DIMBufferVertex *pVertex = &m_pBuffer[m_vertexCount];
	++m_vertexCount;

	pVertex->position[0] = x;// +0.5f / D3DState.viewport.Width;
	pVertex->position[1] = y;// -0.5f / D3DState.viewport.Height;
	pVertex->position[2] = z;
	pVertex->position[3] = w;
	m_bXYZW = true;
	pVertex->color = D3DState.CurrentState.currentColor;
	pVertex->color2 = D3DState.CurrentState.currentColor2;
	memcpy( pVertex->normal, D3DState.CurrentState.currentNormal, sizeof( D3DState.CurrentState.currentNormal ) );

	const int arbTexCoordCount = ARB_GetRequiredVertexTexCoordCount();
	for ( int i = 0; i < D3DGlobal.maxActiveTMU; ++i ) {
		if ( i < arbTexCoordCount || D3DState.EnableState.textureEnabled[i] ) {
			SetupTexCoords( pVertex, i );
		}
	}

	++m_passedVertexCount;
}

//=========================================
// These immediate mode functions modify
// current state and do not draw anything
//=========================================
template<typename T> inline void D3D_SetColor( T red, T green, T blue )
{
	DWORD color = D3DCOLOR_ARGB( 
		0xFF,
		QINDIEGL_CLAMP( ( red / std::numeric_limits<T>::max( ) ) * 255 ),
		QINDIEGL_CLAMP( ( green / std::numeric_limits<T>::max( ) ) * 255 ),
		QINDIEGL_CLAMP( ( blue / std::numeric_limits<T>::max( ) ) * 255 )
	 );
	if ( gDLRecording ) {
		DL_RecordCommand( [color]() {
			D3DState.CurrentState.isSet.bits.color = 1;
			D3DState.CurrentState.currentColor = color;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.color = 1;
	D3DState.CurrentState.currentColor = color;
}
template<typename T> inline void D3D_SetColor( T red, T green, T blue, T alpha )
{
	DWORD color = D3DCOLOR_ARGB( 
		QINDIEGL_CLAMP( ( alpha / std::numeric_limits<T>::max( ) ) * 255 ),
		QINDIEGL_CLAMP( ( red / std::numeric_limits<T>::max( ) ) * 255 ),
		QINDIEGL_CLAMP( ( green / std::numeric_limits<T>::max( ) ) * 255 ),
		QINDIEGL_CLAMP( ( blue / std::numeric_limits<T>::max( ) ) * 255 )
	 );
	if ( gDLRecording ) {
		DL_RecordCommand( [color]() {
			D3DState.CurrentState.isSet.bits.color = 1;
			D3DState.CurrentState.currentColor = color;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.color = 1;
	D3DState.CurrentState.currentColor = color;
}
inline void D3D_SetColor( GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha )
{
	DWORD color = D3DCOLOR_ARGB( 
		( BYTE )alpha,
		( BYTE )red,
		( BYTE )green,
		( BYTE )blue
	 );
	if ( gDLRecording ) {
		DL_RecordCommand( [color]() {
			D3DState.CurrentState.isSet.bits.color = 1;
			D3DState.CurrentState.currentColor = color;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.color = 1;
	D3DState.CurrentState.currentColor = color;
}
inline void D3D_SetColor( GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha )
{
	DWORD color = D3DCOLOR_ARGB( alpha, red, green, blue );
	if ( gDLRecording ) {
		DL_RecordCommand( [color]() {
			D3DState.CurrentState.isSet.bits.color = 1;
			D3DState.CurrentState.currentColor = color;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.color = 1;
	D3DState.CurrentState.currentColor = color;
}
inline void D3D_SetColor( GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha )
{
	DWORD color = D3DCOLOR_ARGB( 
		QINDIEGL_CLAMP( alpha * 255.0f ),
		QINDIEGL_CLAMP( red * 255.0f ),
		QINDIEGL_CLAMP( green * 255.0f ),
		QINDIEGL_CLAMP( blue * 255.0f )
	 );
	if ( gDLRecording ) {
		DL_RecordCommand( [color]() {
			D3DState.CurrentState.isSet.bits.color = 1;
			D3DState.CurrentState.currentColor = color;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.color = 1;
	D3DState.CurrentState.currentColor = color;
}
inline void D3D_SetColor( GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha )
{
	DWORD color = D3DCOLOR_ARGB( 
		QINDIEGL_CLAMP( (FLOAT)alpha * 255.0f ),
		QINDIEGL_CLAMP( (FLOAT)red * 255.0f ),
		QINDIEGL_CLAMP( (FLOAT)green * 255.0f ),
		QINDIEGL_CLAMP( (FLOAT)blue * 255.0f )
	 );
	if ( gDLRecording ) {
		DL_RecordCommand( [color]() {
			D3DState.CurrentState.isSet.bits.color = 1;
			D3DState.CurrentState.currentColor = color;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.color = 1;
	D3DState.CurrentState.currentColor = color;
}
template<typename T> inline void D3D_SetColor2( T red, T green, T blue )
{
	DWORD rgb = (QINDIEGL_CLAMP( ( red / std::numeric_limits<T>::max( ) ) * 255 ) << 16) |
	            (QINDIEGL_CLAMP( ( green / std::numeric_limits<T>::max( ) ) * 255 ) << 8) |
	            QINDIEGL_CLAMP( ( blue / std::numeric_limits<T>::max( ) ) * 255 );
	if ( gDLRecording ) {
		DL_RecordCommand( [rgb]() {
			D3DState.CurrentState.isSet.bits.color2 = 1;
			D3DState.CurrentState.currentColor2 &= 0xFF000000;
			D3DState.CurrentState.currentColor2 |= rgb;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.color2 = 1;
	D3DState.CurrentState.currentColor2 &= 0xFF000000;
	D3DState.CurrentState.currentColor2 |= rgb;
}
inline void D3D_SetColor2( GLbyte red, GLbyte green, GLbyte blue )
{
	DWORD rgb = ((DWORD)( BYTE )red << 16) | ((DWORD)( BYTE )green << 8) | (DWORD)( BYTE )blue;
	if ( gDLRecording ) {
		DL_RecordCommand( [rgb]() {
			D3DState.CurrentState.isSet.bits.color2 = 1;
			D3DState.CurrentState.currentColor2 &= 0xFF000000;
			D3DState.CurrentState.currentColor2 |= rgb;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.color2 = 1;
	D3DState.CurrentState.currentColor2 &= 0xFF000000;
	D3DState.CurrentState.currentColor2 |= rgb;
}
inline void D3D_SetColor2( GLubyte red, GLubyte green, GLubyte blue )
{
	DWORD rgb = ((DWORD)red << 16) | ((DWORD)green << 8) | (DWORD)blue;
	if ( gDLRecording ) {
		DL_RecordCommand( [rgb]() {
			D3DState.CurrentState.isSet.bits.color2 = 1;
			D3DState.CurrentState.currentColor2 &= 0xFF000000;
			D3DState.CurrentState.currentColor2 |= rgb;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.color2 = 1;
	D3DState.CurrentState.currentColor2 &= 0xFF000000;
	D3DState.CurrentState.currentColor2 |= rgb;
}
inline void D3D_SetColor2( GLfloat red, GLfloat green, GLfloat blue )
{
	DWORD rgb = (QINDIEGL_CLAMP( red * 255.0f ) << 16) |
	            (QINDIEGL_CLAMP( green * 255.0f ) << 8) |
	            QINDIEGL_CLAMP( blue * 255.0f );
	if ( gDLRecording ) {
		DL_RecordCommand( [rgb]() {
			D3DState.CurrentState.isSet.bits.color2 = 1;
			D3DState.CurrentState.currentColor2 &= 0xFF000000;
			D3DState.CurrentState.currentColor2 |= rgb;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.color2 = 1;
	D3DState.CurrentState.currentColor2 &= 0xFF000000;
	D3DState.CurrentState.currentColor2 |= rgb;
}
inline void D3D_SetColor2( GLdouble red, GLdouble green, GLdouble blue )
{
	DWORD rgb = (QINDIEGL_CLAMP( (FLOAT)red * 255.0f ) << 16) |
	            (QINDIEGL_CLAMP( (FLOAT)green * 255.0f ) << 8) |
	            QINDIEGL_CLAMP( (FLOAT)blue * 255.0f );
	if ( gDLRecording ) {
		DL_RecordCommand( [rgb]() {
			D3DState.CurrentState.isSet.bits.color2 = 1;
			D3DState.CurrentState.currentColor2 &= 0xFF000000;
			D3DState.CurrentState.currentColor2 |= rgb;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.color2 = 1;
	D3DState.CurrentState.currentColor2 &= 0xFF000000;
	D3DState.CurrentState.currentColor2 |= rgb;
}
inline void D3D_SetFogCoord( GLfloat value )
{
	if ( gDLRecording ) {
		DL_RecordCommand( [value]() {
			D3DState.CurrentState.isSet.bits.fog = 1;
			GLubyte bv = 255 - static_cast<GLubyte>( QINDIEGL_CLAMP( value * 255.0f ) );
			D3DState.CurrentState.currentColor2 &= ( bv << 24 );
			D3DState.CurrentState.currentColor2 |= ( bv << 24 );
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.fog = 1;
	GLubyte byteValue = 255 - static_cast<GLubyte>( QINDIEGL_CLAMP( value * 255.0f ) );
	D3DState.CurrentState.currentColor2 &= ( byteValue << 24 );
	D3DState.CurrentState.currentColor2 |= ( byteValue << 24 );
}
template<typename T> inline void D3D_SetNormal( T x, T y, T z )
{
	FLOAT nx = (FLOAT)x / std::numeric_limits<T>::max( );
	FLOAT ny = (FLOAT)y / std::numeric_limits<T>::max( );
	FLOAT nz = (FLOAT)z / std::numeric_limits<T>::max( );
	if ( gDLRecording ) {
		DL_RecordCommand( [nx, ny, nz]() {
			D3DState.CurrentState.isSet.bits.norm = 1;
			D3DState.CurrentState.currentNormal[0] = nx;
			D3DState.CurrentState.currentNormal[1] = ny;
			D3DState.CurrentState.currentNormal[2] = nz;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.norm = 1;
	D3DState.CurrentState.currentNormal[0] = nx;
	D3DState.CurrentState.currentNormal[1] = ny;
	D3DState.CurrentState.currentNormal[2] = nz;
}
inline void D3D_SetNormal( GLfloat x, GLfloat y, GLfloat z )
{
	if ( gDLRecording ) {
		DL_RecordCommand( [x, y, z]() {
			D3DState.CurrentState.isSet.bits.norm = 1;
			D3DState.CurrentState.currentNormal[0] = x;
			D3DState.CurrentState.currentNormal[1] = y;
			D3DState.CurrentState.currentNormal[2] = z;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.norm = 1;
	D3DState.CurrentState.currentNormal[0] = x;
	D3DState.CurrentState.currentNormal[1] = y;
	D3DState.CurrentState.currentNormal[2] = z;
}
inline void D3D_SetNormal( GLdouble x, GLdouble y, GLdouble z )
{
	FLOAT nx = (FLOAT)x, ny = (FLOAT)y, nz = (FLOAT)z;
	if ( gDLRecording ) {
		DL_RecordCommand( [nx, ny, nz]() {
			D3DState.CurrentState.isSet.bits.norm = 1;
			D3DState.CurrentState.currentNormal[0] = nx;
			D3DState.CurrentState.currentNormal[1] = ny;
			D3DState.CurrentState.currentNormal[2] = nz;
		} );
		if ( !gDLExecute ) return;
	}
	D3DState.CurrentState.isSet.bits.norm = 1;
	D3DState.CurrentState.currentNormal[0] = nx;
	D3DState.CurrentState.currentNormal[1] = ny;
	D3DState.CurrentState.currentNormal[2] = nz;
}
template<typename T> inline void D3D_SetTexCoord( GLenum target, T s, T t, T r, T q, int num )
{
	//HACK: workaround for Quake3: it uses targets 0 and 1 instead of GL_TEXTURE0_ARB and GL_TEXTURE1_ARB
	//HACK: it seems that drivers were fixed after Carmack's code, not vise versa : )
	int stage = target;
	if ( stage >= GL_TEXTURE0_ARB ) stage -= GL_TEXTURE0_ARB;
	if ( stage < 0 || stage >= D3DGlobal.maxActiveTMU ) {
		QGL_SET_ERROR(E_INVALID_ENUM);
		return;
	}

	DWORD tcBits = DWORD( num - 1 ) << ( stage * 2 );
	FLOAT fs = (FLOAT)s, ft = (FLOAT)t, fr = (FLOAT)r, fq = (FLOAT)q;

	if ( D3DState.TransformState.texcoordFixEnabled ) {
		fs += D3DState.TransformState.texcoordFix[0];
		ft += D3DState.TransformState.texcoordFix[1];
	}

	if ( gDLRecording ) {
		DL_RecordCommand( [stage, tcBits, fs, ft, fr, fq]() {
			D3DState.CurrentState.isSet.bits.texcoord |= tcBits;
			D3DState.CurrentState.currentTexCoord[stage][0] = fs;
			D3DState.CurrentState.currentTexCoord[stage][1] = ft;
			D3DState.CurrentState.currentTexCoord[stage][2] = fr;
			D3DState.CurrentState.currentTexCoord[stage][3] = fq;
		} );
		if ( !gDLExecute ) return;
	}

	D3DState.CurrentState.isSet.bits.texcoord |= tcBits;
	D3DState.CurrentState.currentTexCoord[stage][0] = fs;
	D3DState.CurrentState.currentTexCoord[stage][1] = ft;
	D3DState.CurrentState.currentTexCoord[stage][2] = fr;
	D3DState.CurrentState.currentTexCoord[stage][3] = fq;

	QGL_SET_ERROR(S_OK);
}

OPENGL_API void WINAPI glColor3b( GLbyte red, GLbyte green, GLbyte blue )
{
	D3D_SetColor( red, green, blue, SCHAR_MAX );
}
OPENGL_API void WINAPI glColor3bv( const GLbyte *v )
{
	D3D_SetColor( v[0], v[1], v[2], SCHAR_MAX );
}
OPENGL_API void WINAPI glColor3d( GLdouble red, GLdouble green, GLdouble blue )
{
	D3D_SetColor( red, green, blue, 1.0 );
}
OPENGL_API void WINAPI glColor3dv( const GLdouble *v )
{
	D3D_SetColor( v[0], v[1], v[2], 1.0 );
}
OPENGL_API void WINAPI glColor3f( GLfloat red, GLfloat green, GLfloat blue )
{
	D3D_SetColor( red, green, blue, 1.0f );
}
OPENGL_API void WINAPI glColor3fv( const GLfloat *v )
{
	D3D_SetColor( v[0], v[1], v[2], 1.0f );
}
OPENGL_API void WINAPI glColor3i( GLint red, GLint green, GLint blue )
{
	D3D_SetColor( red, green, blue );
}
OPENGL_API void WINAPI glColor3iv( const GLint *v )
{
	D3D_SetColor( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glColor3s( GLshort red, GLshort green, GLshort blue )
{
	D3D_SetColor( red, green, blue );
}
OPENGL_API void WINAPI glColor3sv( const GLshort *v )
{
	D3D_SetColor( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glColor3ub( GLubyte red, GLubyte green, GLubyte blue )
{
	D3D_SetColor( red, green, blue, 0xFF );
}
OPENGL_API void WINAPI glColor3ubv( const GLubyte *v )
{
	D3D_SetColor( v[0], v[1], v[2], 0xFF );
}
OPENGL_API void WINAPI glColor3ui( GLuint red, GLuint green, GLuint blue )
{
	D3D_SetColor( red, green, blue );
}
OPENGL_API void WINAPI glColor3uiv( const GLuint *v )
{
	D3D_SetColor( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glColor3us( GLushort red, GLushort green, GLushort blue )
{
	D3D_SetColor( red, green, blue );
}
OPENGL_API void WINAPI glColor3usv( const GLushort *v )
{
	D3D_SetColor( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glColor4b( GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4bv( const GLbyte *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4d( GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4dv( const GLdouble *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4f( GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4fv( const GLfloat *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4i( GLint red, GLint green, GLint blue, GLint alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4iv( const GLint *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4s( GLshort red, GLshort green, GLshort blue, GLshort alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4sv( const GLshort *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4ub( GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4ubv( const GLubyte *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4ui( GLuint red, GLuint green, GLuint blue, GLuint alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4uiv( const GLuint *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4us( GLushort red, GLushort green, GLushort blue, GLushort alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4usv( const GLushort *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glSecondaryColor3b( GLbyte red, GLbyte green, GLbyte blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3bv( const GLbyte *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3d( GLdouble red, GLdouble green, GLdouble blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3dv( const GLdouble *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3f( GLfloat red, GLfloat green, GLfloat blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3fv( const GLfloat *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3i( GLint red, GLint green, GLint blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3iv( const GLint *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3s( GLshort red, GLshort green, GLshort blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3sv( const GLshort *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3ub( GLubyte red, GLubyte green, GLubyte blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3ubv( const GLubyte *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3ui( GLuint red, GLuint green, GLuint blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3uiv( const GLuint *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3us( GLushort red, GLushort green, GLushort blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3usv( const GLushort *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glNormal3b( GLbyte nx, GLbyte ny, GLbyte nz )
{
	D3D_SetNormal( nx, ny, nz );
}
OPENGL_API void WINAPI glNormal3bv( const GLbyte *v )
{
	D3D_SetNormal( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glNormal3d( GLdouble nx, GLdouble ny, GLdouble nz )
{
	D3D_SetNormal( nx, ny, nz );
}
OPENGL_API void WINAPI glNormal3dv( const GLdouble *v )
{
	D3D_SetNormal( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glNormal3f( GLfloat nx, GLfloat ny, GLfloat nz )
{
	D3D_SetNormal( nx, ny, nz );
}
OPENGL_API void WINAPI glNormal3fv( const GLfloat *v )
{
	D3D_SetNormal( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glNormal3i( GLint nx, GLint ny, GLint nz )
{
	D3D_SetNormal( nx, ny, nz );
}
OPENGL_API void WINAPI glNormal3iv( const GLint *v )
{
	D3D_SetNormal( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glNormal3s( GLshort nx, GLshort ny, GLshort nz )
{
	D3D_SetNormal( nx, ny, nz );
}
OPENGL_API void WINAPI glNormal3sv( const GLshort *v )
{
	D3D_SetNormal( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glTexCoord1d( GLdouble s )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, 0.0, 0.0, 1.0, 1 );
}
OPENGL_API void WINAPI glTexCoord1dv( const GLdouble *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], 0.0, 0.0, 1.0, 1 );
}
OPENGL_API void WINAPI glTexCoord1f( GLfloat s )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, 0.0f, 0.0f, 1.0f, 1 );
}
OPENGL_API void WINAPI glTexCoord1fv( const GLfloat *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], 0.0f, 0.0f, 1.0f, 1 );
}
OPENGL_API void WINAPI glTexCoord1i( GLint s )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, 0, 0, 1, 1 );
}
OPENGL_API void WINAPI glTexCoord1iv( const GLint *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], 0, 0, 1, 1 );
}
OPENGL_API void WINAPI glTexCoord1s( GLshort s )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, (GLshort)0, (GLshort)0, (GLshort)1, 1 );
}
OPENGL_API void WINAPI glTexCoord1sv( const GLshort *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], (GLshort)0, (GLshort)0, (GLshort)1, 1 );
}
OPENGL_API void WINAPI glTexCoord2d( GLdouble s, GLdouble t )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, 0.0, 1.0, 2 );
}
OPENGL_API void WINAPI glTexCoord2dv( const GLdouble *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], 0.0, 1.0, 2 );
}
OPENGL_API void WINAPI glTexCoord2f( GLfloat s, GLfloat t )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, 0.0f, 1.0f, 2 );
}
OPENGL_API void WINAPI glTexCoord2fv( const GLfloat *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], 0.0f, 1.0f, 2 );
}
OPENGL_API void WINAPI glTexCoord2i( GLint s, GLint t )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, 0, 1, 2 );
}
OPENGL_API void WINAPI glTexCoord2iv( const GLint *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], 0, 1, 2 );
}
OPENGL_API void WINAPI glTexCoord2s( GLshort s, GLshort t )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, (GLshort)0, (GLshort)1, 2 );
}
OPENGL_API void WINAPI glTexCoord2sv( const GLshort *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], (GLshort)0, (GLshort)1, 2 );
}
OPENGL_API void WINAPI glTexCoord3d( GLdouble s, GLdouble t, GLdouble r )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, 1.0, 3 );
}
OPENGL_API void WINAPI glTexCoord3dv( const GLdouble *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], 1.0, 3 );
}
OPENGL_API void WINAPI glTexCoord3f( GLfloat s, GLfloat t, GLfloat r )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, 1.0f, 3 );
}
OPENGL_API void WINAPI glTexCoord3fv( const GLfloat *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], 1.0f, 3 );
}
OPENGL_API void WINAPI glTexCoord3i( GLint s, GLint t, GLint r )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, 1, 3 );
}
OPENGL_API void WINAPI glTexCoord3iv( const GLint *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], 1, 3 );
}
OPENGL_API void WINAPI glTexCoord3s( GLshort s, GLshort t, GLshort r )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, (GLshort)1, 3 );
}
OPENGL_API void WINAPI glTexCoord3sv( const GLshort *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], (GLshort)1, 3 );
}
OPENGL_API void WINAPI glTexCoord4d( GLdouble s, GLdouble t, GLdouble r, GLdouble q )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glTexCoord4dv( const GLdouble *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], v[3], 4 );
}
OPENGL_API void WINAPI glTexCoord4f( GLfloat s, GLfloat t, GLfloat r, GLfloat q )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glTexCoord4fv( const GLfloat *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], v[3], 4 );
}
OPENGL_API void WINAPI glTexCoord4i( GLint s, GLint t, GLint r, GLint q )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glTexCoord4iv( const GLint *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], v[3], 4 );
}
OPENGL_API void WINAPI glTexCoord4s( GLshort s, GLshort t, GLshort r, GLshort q )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glTexCoord4sv( const GLshort *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], v[3], 4 );
}
OPENGL_API void WINAPI glEdgeFlag( GLboolean )
{
	logPrintf( "WARNING: glEdgeFlag is not implemented\n" );
}
OPENGL_API void WINAPI glEdgeFlagv( const GLboolean* )
{
	logPrintf( "WARNING: glEdgeFlagv is not implemented\n" );
}
OPENGL_API void WINAPI glIndexd( GLdouble )
{
	logPrintf( "WARNING: glIndexd is not implemented\n" );
}
OPENGL_API void WINAPI glIndexdv( const GLdouble* )
{
	logPrintf( "WARNING: glIndexdv is not implemented\n" );
}
OPENGL_API void WINAPI glIndexf( GLfloat )
{
	logPrintf( "WARNING: glIndexf is not implemented\n" );
}
OPENGL_API void WINAPI glIndexfv( const GLfloat* )
{
	logPrintf( "WARNING: glIndexfv is not implemented\n" );
}
OPENGL_API void WINAPI glIndexi( GLint )
{
	logPrintf( "WARNING: glIndexi is not implemented\n" );
}
OPENGL_API void WINAPI glIndexiv( const GLint* )
{
	logPrintf( "WARNING: glIndexiv is not implemented\n" );
}
OPENGL_API void WINAPI glIndexs( GLshort )
{
	logPrintf( "WARNING: glIndexs is not implemented\n" );
}
OPENGL_API void WINAPI glIndexsv( const GLshort* )
{
	logPrintf( "WARNING: glIndexsv is not implemented\n" );
}
OPENGL_API void WINAPI glIndexub( GLubyte )
{
	logPrintf( "WARNING: glIndexub is not implemented\n" );
}
OPENGL_API void WINAPI glIndexubv( const GLubyte* )
{
	logPrintf( "WARNING: glIndexubv is not implemented\n" );
}
OPENGL_API void WINAPI glMultiTexCoord1s( GLenum target, GLshort s )
{
	D3D_SetTexCoord( target, s, (GLshort)0, (GLshort)0, (GLshort)1, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1i( GLenum target, GLint s )
{
	D3D_SetTexCoord( target, s, 0, 0, 1, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1f( GLenum target, GLfloat s )
{
	D3D_SetTexCoord( target, s, 0.0f, 0.0f, 1.0f, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1d( GLenum target, GLdouble s )
{
	D3D_SetTexCoord( target, s, 0.0, 0.0, 1.0, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1dEXT( GLenum target, GLdouble s )
{
	D3D_SetTexCoord( target, s, 0.0, 0.0, 1.0, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord2s( GLenum target, GLshort s, GLshort t )
{
	D3D_SetTexCoord( target, s, t, (GLshort)0, (GLshort)1, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord2i( GLenum target, GLint s, GLint t )
{
	D3D_SetTexCoord( target, s, t, 0, 1, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord2f( GLenum target, GLfloat s, GLfloat t )
{
	D3D_SetTexCoord( target, s, t, 0.0f, 1.0f, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord2d( GLenum target, GLdouble s, GLdouble t )
{
	D3D_SetTexCoord( target, s, t, 0.0, 1.0, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord3s( GLenum target, GLshort s, GLshort t, GLshort r )
{
	D3D_SetTexCoord( target, s, t, r, (GLshort)1, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord3i( GLenum target, GLint s, GLint t, GLint r )
{
	D3D_SetTexCoord( target, s, t, r, 1, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord3f( GLenum target, GLfloat s, GLfloat t, GLfloat r )
{
	D3D_SetTexCoord( target, s, t, r, 1.0f, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord3d( GLenum target, GLdouble s, GLdouble t, GLdouble r )
{
	D3D_SetTexCoord( target, s, t, r, 1.0, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord4s( GLenum target, GLshort s, GLshort t, GLshort r, GLshort q )
{
	D3D_SetTexCoord( target, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4i( GLenum target, GLint s, GLint t, GLint r, GLint q )
{
	D3D_SetTexCoord( target, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4f( GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q )
{
	D3D_SetTexCoord( target, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4d( GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q )
{
	D3D_SetTexCoord( target, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4sdARB( GLenum target, GLshort s, GLshort t, GLshort r, GLshort q )
{
	D3D_SetTexCoord( target, static_cast<GLfloat>( s ), static_cast<GLfloat>( t ), static_cast<GLfloat>( r ), static_cast<GLfloat>( q ), 4 );
}
OPENGL_API void WINAPI glMultiTexCoord1sv( GLenum target, const GLshort *v )
{
	D3D_SetTexCoord( target, v[0], (GLshort)0, (GLshort)0, (GLshort)1, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1iv( GLenum target, const GLint *v )
{
	D3D_SetTexCoord( target, v[0], 0, 0, 1, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1fv( GLenum target, const GLfloat *v )
{
	D3D_SetTexCoord( target, v[0], 0.0f, 0.0f, 1.0f, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1dv( GLenum target, const GLdouble *v )
{
	D3D_SetTexCoord( target, v[0], 0.0, 0.0, 1.0, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord2sv( GLenum target, const GLshort *v )
{
	D3D_SetTexCoord( target, v[0], v[1], (GLshort)0, (GLshort)1, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord2iv( GLenum target, const GLint *v )
{
	D3D_SetTexCoord( target, v[0], v[1], 0, 1, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord2fv( GLenum target, const GLfloat *v )
{
	D3D_SetTexCoord( target, v[0], v[1], 0.0f, 1.0f, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord2dv( GLenum target, const GLdouble *v )
{
	D3D_SetTexCoord( target, v[0], v[1], 0.0, 1.0, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord3sv( GLenum target, const GLshort *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[2], (GLshort)1, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord3iv( GLenum target, const GLint *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[2], 1, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord3fv( GLenum target, const GLfloat *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[2], 1.0f, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord3dv( GLenum target, const GLdouble *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[2], 1.0, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord4sv( GLenum target, const GLshort *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[2], v[3], 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4iv( GLenum target, const GLint *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[2], v[3], 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4fv( GLenum target, const GLfloat *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[2], v[3], 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4dv( GLenum target, const GLdouble *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[2], v[3], 4 );
}
OPENGL_API void WINAPI glMTexCoord2f( GLenum target, GLfloat s, GLfloat t )
{
	D3D_SetTexCoord( target + GL_TEXTURE0_ARB - GL_TEXTURE0_SGIS, s, t, 0.0f, 1.0f, 4 );
}
OPENGL_API void WINAPI glMTexCoord2fv( GLenum target, const GLfloat *v )
{
	D3D_SetTexCoord( target + GL_TEXTURE0_ARB - GL_TEXTURE0_SGIS, v[0], v[1], 0.0f, 1.0f, 4 );
}
OPENGL_API void WINAPI glFogCoordd( GLdouble coord )
{
	D3D_SetFogCoord( (FLOAT)coord );
}
OPENGL_API void WINAPI glFogCoordf( GLfloat coord )
{
	D3D_SetFogCoord( coord );
}
OPENGL_API void WINAPI glFogCoorddv( GLdouble *coord )
{
	D3D_SetFogCoord( (FLOAT)( *coord ) );
}
OPENGL_API void WINAPI glFogCoordfv( GLfloat *coord )
{
	D3D_SetFogCoord( *coord );
}

//=========================================
// Vertex* functions fill the immediate 
// buffer collecting all other current state
// values
//=========================================
template<typename T> inline void D3D_AddVertex( T x, T y, T z, T w )
{
	assert( D3DGlobal.pIMBuffer != NULL );
	if ( gDLRecording ) {
		FLOAT fx = (FLOAT)x, fy = (FLOAT)y, fz = (FLOAT)z, fw = (FLOAT)w;
		DL_RecordCommand( [fx, fy, fz, fw]() {
			D3DGlobal.pIMBuffer->AddVertex( fx, fy, fz, fw );
		} );
		if ( !gDLExecute ) return;
	}
	D3DGlobal.pIMBuffer->AddVertex( (FLOAT)x, (FLOAT)y, (FLOAT)z, (FLOAT)w );
}
template<typename T> inline void D3D_AddVertex( T x, T y, T z )
{
	assert( D3DGlobal.pIMBuffer != NULL );
	if ( gDLRecording ) {
		FLOAT fx = (FLOAT)x, fy = (FLOAT)y, fz = (FLOAT)z;
		DL_RecordCommand( [fx, fy, fz]() {
			D3DGlobal.pIMBuffer->AddVertex( fx, fy, fz );
		} );
		if ( !gDLExecute ) return;
	}
	D3DGlobal.pIMBuffer->AddVertex( (FLOAT)x, (FLOAT)y, (FLOAT)z );
}

OPENGL_API void WINAPI glVertex2d( GLdouble x, GLdouble y )
{
	D3D_AddVertex( x, y, 0.0 );
}
OPENGL_API void WINAPI glVertex2dv( const GLdouble *v )
{
	D3D_AddVertex( v[0], v[1], 0.0 );
}
OPENGL_API void WINAPI glVertex2f( GLfloat x, GLfloat y )
{
	D3D_AddVertex( x, y, 0.0f );
}
OPENGL_API void WINAPI glVertex2fv( const GLfloat *v )
{
	D3D_AddVertex( v[0], v[1], 0.0f );
}
OPENGL_API void WINAPI glVertex2i( GLint x, GLint y )
{
	D3D_AddVertex( x, y, 0 );
}
OPENGL_API void WINAPI glVertex2iv( const GLint *v )
{
	D3D_AddVertex( v[0], v[1], 0 );
}
OPENGL_API void WINAPI glVertex2s( GLshort x, GLshort y )
{
	D3D_AddVertex( x, y, (GLshort)0 );
}
OPENGL_API void WINAPI glVertex2sv( const GLshort *v )
{
	D3D_AddVertex( v[0], v[1], (GLshort)0 );
}
OPENGL_API void WINAPI glVertex3d( GLdouble x, GLdouble y, GLdouble z )
{
	D3D_AddVertex( x, y, z );
}
OPENGL_API void WINAPI glVertex3dv( const GLdouble *v )
{
	D3D_AddVertex( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glVertex3f( GLfloat x, GLfloat y, GLfloat z )
{
	D3D_AddVertex( x, y, z );
}
OPENGL_API void WINAPI glVertex3fv( const GLfloat *v )
{
	D3D_AddVertex( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glVertex3i( GLint x, GLint y, GLint z )
{
	D3D_AddVertex( x, y, z );
}
OPENGL_API void WINAPI glVertex3iv( const GLint *v )
{
	D3D_AddVertex( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glVertex3s( GLshort x, GLshort y, GLshort z )
{
	D3D_AddVertex( x, y, z );
}
OPENGL_API void WINAPI glVertex3sv( const GLshort *v )
{
	D3D_AddVertex( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glVertex4d( GLdouble x, GLdouble y, GLdouble z, GLdouble w )
{
	D3D_AddVertex( x, y, z, w );
}
OPENGL_API void WINAPI glVertex4dv( const GLdouble *v )
{
	D3D_AddVertex( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glVertex4f( GLfloat x, GLfloat y, GLfloat z, GLfloat w )
{
	D3D_AddVertex( x, y, z, w );
}
OPENGL_API void WINAPI glVertex4fv( const GLfloat *v )
{
	D3D_AddVertex( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glVertex4i( GLint x, GLint y, GLint z, GLint w )
{
	D3D_AddVertex( x, y, z, w );
}
OPENGL_API void WINAPI glVertex4iv( const GLint *v )
{
	D3D_AddVertex( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glVertex4s( GLshort x, GLshort y, GLshort z, GLshort w )
{
	D3D_AddVertex( x, y, z, w );
}
OPENGL_API void WINAPI glVertex4sv( const GLshort *v )
{
	D3D_AddVertex( v[0], v[1], v[2], v[3] );
}

//=========================================
// Begin/End pair
//=========================================
OPENGL_API void WINAPI glBegin( GLenum mode )
{
	DL_RECORD_1( glBegin, mode );
	if ( !D3DGlobal.deviceLost ) {
		D3DState_Check( );
		D3DState_AssureBeginScene( );
	}
	assert( D3DGlobal.pIMBuffer != NULL );
	D3DGlobal.pIMBuffer->Begin( mode );
}

OPENGL_API void WINAPI glEnd( )
{
	DL_RECORD_0( glEnd );
	QGLDrawTimer drawTimer;
	assert( D3DGlobal.pIMBuffer != NULL );
	D3DGlobal.pIMBuffer->End( );
	D3DState.CurrentState.isSet.all = 0;
}

//=========================================
// Rect specification
//=========================================
OPENGL_API void WINAPI glRectf( GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2 );

template<typename T> inline void D3D_Rect( T x1, T y1, T x2, T y2 )
{
	if ( gDLRecording ) {
		FLOAT fx1 = (FLOAT)x1, fy1 = (FLOAT)y1, fx2 = (FLOAT)x2, fy2 = (FLOAT)y2;
		DL_RecordCommand( [fx1, fy1, fx2, fy2]() {
			glRectf( fx1, fy1, fx2, fy2 );
		} );
		if ( !gDLExecute ) return;
	}
	if ( D3DGlobal.deviceLost ) return;
	D3DState_Check( );
	D3DState_AssureBeginScene( );
	assert( D3DGlobal.pIMBuffer != NULL );
	D3DGlobal.pIMBuffer->Begin( GL_POLYGON );
		D3D_AddVertex( x1, y1, ( T )0 );
		D3D_AddVertex( x2, y1, ( T )0 );
		D3D_AddVertex( x2, y2, ( T )0 );
		D3D_AddVertex( x1, y2, ( T )0 );
	D3DGlobal.pIMBuffer->End( );
}

OPENGL_API void WINAPI glRectd( GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2 )
{
	D3D_Rect( x1, y1, x2, y2 );
}
OPENGL_API void WINAPI glRectdv( const GLdouble *v1, const GLdouble *v2 )
{
	D3D_Rect( v1[0], v1[1], v2[0], v2[1] );
}
OPENGL_API void WINAPI glRectf( GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2 )
{
	D3D_Rect( x1, y1, x2, y2 );
}
OPENGL_API void WINAPI glRectfv( const GLfloat *v1, const GLfloat *v2 )
{
	D3D_Rect( v1[0], v1[1], v2[0], v2[1] );
}
OPENGL_API void WINAPI glRecti( GLint x1, GLint y1, GLint x2, GLint y2 )
{
	D3D_Rect( x1, y1, x2, y2 );
}
OPENGL_API void WINAPI glRectiv( const GLint *v1, const GLint *v2 )
{
	D3D_Rect( v1[0], v1[1], v2[0], v2[1] );
}
OPENGL_API void WINAPI glRects( GLshort x1, GLshort y1, GLshort x2, GLshort y2 )
{
	D3D_Rect( x1, y1, x2, y2 );
}
OPENGL_API void WINAPI glRectsv( const GLshort *v1, const GLshort *v2 )
{
	D3D_Rect( v1[0], v1[1], v2[0], v2[1] );
}
