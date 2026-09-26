/***************************************************************************
* Copyright( C ) 2011-2016, Crystice Softworks.
* 
* This file is part of QindieGL source code.
* Please note that QindieGL is not driver, it's emulator.
* 
* QindieGL source code is free software; you can redistribute it and/or 
* modify it under the terms of the GNU General Public License as 
* published by the Free Software Foundation; either version 2 of 
* the License, or( at your option ) any later version.
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
#include "d3d_matrix_stack.hpp"
#include "d3d_matrix_detection.hpp"
#include "d3d_utils.hpp"
#include "d3d_lists.hpp"
//==================================================================================
// Some words about projection matrices
//----------------------------------------------------------------------------------
// They are different in OpenGL and Direct3D because of different clip space.
// glFrustum will create a D3D-compatible projection matrix.
// But some games( like Quake3 and Doom3 ) create their own projection matrices 
// and glLoadMatrix them. If ProjectionFix setting is enabled, we will 
// try to catch such operation and fix the matrix.
//
// The fix is applied to values C( m[2][2] ) and D( m[2][3] ).
//
// In OpenGL, perspective projection matrix is defined in this way:
// C =( zn + zf ) /( zn - zf )
// D = 2 * zn * zf /( zn - zf )
// In Direct3D they are a bit different:
// C = zf /( zn - zf )
// D = zn * zf /( zn - zf )
// So, we restore zn and zf from OpenGL matrix, and then subtract zn/( zn - zf ) from C 
// and scale D by one half.
//
// In OpenGL, ortho projection matrix is defined in this way:
// C = 2 /( zn - zf )
// D =( zn + zf ) /( zn - zf )
// In Direct3D they are a bit different:
// C = 1 /( zn - zf )
// D = zn /( zn - zf )
// So, we restore zn and zf from OpenGL matrix, and then subtract zf/( zn - zf ) from D 
// and scale C by one half.
//
// Without this fix there will be noticeable clipping problems on viewmodels in games that
// don't use glFrustum and load projection matrices themselves. Also, Doom3 won't draw any
// 2D graphics.
//
//==================================================================================
// Matrix operation functions
//----------------------------------------------------------------------------------
// We use some D3DX functionality to implement it
//==================================================================================

static inline void CheckTexCoordOffset_Hack( bool ortho )
{
	if( !D3DGlobal.settings.texcoordFix )
		return;

	//HACK: OpenGL somehow offsets texcoords in 2D mode??!
	//HACK: so we will create a workaround for it
	//HACK: without this fix there will be no crosshair in Quake2
	//HACK: also there will be a problem with sprites and fonts in Xash
	if( D3DState.TransformState.matrixMode == GL_PROJECTION ) {
		if( ortho ) {
			D3DState.TransformState.texcoordFixEnabled = TRUE;
		} else {
			D3DState.TransformState.texcoordFixEnabled = FALSE;
		}
	}
}

// Diagnostics: modelview history for the YAE program-fog probe. Loads and
// multiplies record the GL matrix translation and the length of its first
// column (its uniform scale); other operations record their arguments.
enum { MVOP_IDENTITY, MVOP_LOAD, MVOP_MULT, MVOP_PUSH, MVOP_POP, MVOP_TRANSLATE, MVOP_ROTATE, MVOP_SCALE };
static void RecordModelviewOp( int op, const GLfloat *m16, GLfloat x = 0, GLfloat y = 0, GLfloat z = 0, GLfloat w = 0 )
{
	if (D3DState.TransformState.matrixMode != GL_MODELVIEW) return;
	GLfloat v[4] = { x, y, z, w };
	if (m16) {
		v[0] = m16[12]; v[1] = m16[13]; v[2] = m16[14];
		v[3] = sqrtf(m16[0] * m16[0] + m16[1] * m16[1] + m16[2] * m16[2]);
	}
	QGL_DiagnosticsRecordProgramOp('M', GL_MODELVIEW, 0, op, v);
}

OPENGL_API void WINAPI glMatrixMode( GLenum mode )
{
	DL_RECORD_1( glMatrixMode, mode );
	if( D3DState.TransformState.matrixMode != mode )
	{
		D3DState.TransformState.matrixMode = mode;
		if( !D3DState_SetMatrixMode() ) 
			logPrintf( "WARNING: glMatrixMode: unimplemented matrix mode 0x%x\n", mode );
	}
}

OPENGL_API void WINAPI glLoadIdentity()
{
	DL_RECORD_0( glLoadIdentity );
	if( !D3DState.currentMatrixStack ) return;
	RecordModelviewOp( MVOP_IDENTITY, nullptr );
	D3DState.currentMatrixStack->load_identity( );
	*D3DState.currentMatrixModified = true;
	CheckTexCoordOffset_Hack( false );

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->load_identity( );
		D3DGlobal.viewMatrixStack->load_identity( );
	}
}

static void ProjectionMatrix_GLtoD3D( FLOAT *m )
{
	if( m[2*4+3] >= 0 ) {
		//2D projection
		//Restore znear and zfar from projection matrix
		GLfloat fC = m[2*4+2];
		GLfloat fD = m[3*4+2];
		GLfloat fQ =( fD + 1.0f ) /( fD - 1.0f );
		GLfloat zF = 2.0f /( fC *( fQ - 1.0f ) );
		GLfloat zN =( 2.0f / fC ) + zF;
		//Convert GL ortho projection to D3D
		m[2*4+2] *= 0.5f;
		m[3*4+2] -= zF /( zN - zF );
	} else {
		//3D projection
		//first check for infinite zfar plane
		//without this check, fQ below will be 0.0 and then
		//  we will runto into float inf and nans with those formulas
		if ( m[2*4+2] == -1.0f )
		{
			PRINT_ONCE( "Infinite ZFar form of Projection Matrix detected.\n" );

			if ( D3DGlobal.settings.projectionMaxZFar )
			{
				GLfloat zF = (GLfloat)D3DGlobal.settings.projectionMaxZFar;
				GLfloat zN = -m[3*4+2] * 0.5f;
				m[2*4+2] = zF/(zN - zF);
				m[3*4+2] = zN*zF/(zN - zF);
			}
			else
			{
				//the only fix needed is to divide D by 2
				m[3*4+2] *= 0.5f;
			}
		}
		else
		{
			//Restore znear and zfar from projection matrix
			GLfloat fC = m[2*4+2];
			GLfloat fD = m[3*4+2];
			GLfloat fQ =( 1.0f + fC ) /( 1.0f - fC );
			//WG: do we need to check for fQ != 0 ?
			GLfloat zF =( fD *( 1.0f + fQ ) ) /( 2.0f * fQ );
			GLfloat zN =( fD * zF ) /( fD - 2.0f*zF );
			//Convert GL perspective projection to D3D
			m[2*4+2] -= zN /( zN - zF );
			m[3*4+2] *= 0.5f;
		}
	}
}

OPENGL_API void WINAPI glLoadMatrixf( const GLfloat *m )
{
	DL_RECORD_MAT16F( glLoadMatrixf, m );
	if( !D3DState.currentMatrixStack ) return;
	RecordModelviewOp( MVOP_LOAD, m );
	bool b2Dproj = false;
	if( D3DGlobal.settings.projectionFix ) {
		D3DXMATRIX m2( m );
		if( D3DState.TransformState.matrixMode == GL_PROJECTION ) {
			b2Dproj =( m2[2*4+3] >= 0 );
			ProjectionMatrix_GLtoD3D( m2 );
		}
		D3DState.currentMatrixStack->load( m2 );
	} else {
		D3DState.currentMatrixStack->load( m );
	}
	*D3DState.currentMatrixModified = true;
	CheckTexCoordOffset_Hack( b2Dproj );

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DXMATRIX model, view;
		matrix_detect_process_upload(m, &model, &view);
		D3DGlobal.modelMatrixStack->load(model);
		D3DGlobal.viewMatrixStack->load(view);
	}
}
OPENGL_API void WINAPI glLoadMatrixd( const GLdouble *m )
{
	DL_RECORD_MAT16D( glLoadMatrixd, m );
	if( !D3DState.currentMatrixStack ) return;
	bool b2Dproj = false;
	FLOAT mf[16];
	for( int i = 0; i < 16; ++i ) 
		mf[i] =(FLOAT)m[i];
	RecordModelviewOp( MVOP_LOAD, mf );
	if( D3DGlobal.settings.projectionFix ) {
		if( D3DState.TransformState.matrixMode == GL_PROJECTION ) {
			b2Dproj =( mf[2*4+3] >= 0 );
			ProjectionMatrix_GLtoD3D( mf );
		}
	}
	D3DState.currentMatrixStack->load( mf );
	*D3DState.currentMatrixModified = true;
	CheckTexCoordOffset_Hack( b2Dproj );

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DXMATRIX model, view;
		matrix_detect_process_upload(mf, &model, &view);
		D3DGlobal.modelMatrixStack->load(model);
		D3DGlobal.viewMatrixStack->load(view);
	}
}
OPENGL_API void WINAPI glMultMatrixf( const GLfloat *m )
{
	DL_RECORD_MAT16F( glMultMatrixf, m );
	if( !D3DState.currentMatrixStack ) return;
	RecordModelviewOp( MVOP_MULT, m );
	D3DState.currentMatrixStack->multiply( m );
	*D3DState.currentMatrixModified = true;
	CheckTexCoordOffset_Hack( false );

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->multiply( m );
	}
}
OPENGL_API void WINAPI glMultMatrixd( const GLdouble *m )
{
	DL_RECORD_MAT16D( glMultMatrixd, m );
	if( !D3DState.currentMatrixStack ) return;
	FLOAT mf[16];
	for( int i = 0; i < 16; ++i ) 
		mf[i] =(FLOAT)m[i];
	RecordModelviewOp( MVOP_MULT, mf );
	D3DState.currentMatrixStack->multiply( mf );
	*D3DState.currentMatrixModified = true;
	CheckTexCoordOffset_Hack( false );

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->multiply( mf );
	}
}
OPENGL_API void WINAPI glLoadTransposeMatrixf( const GLfloat *m )
{
	DL_RECORD_MAT16F( glLoadTransposeMatrixf, m );
	if( !D3DState.currentMatrixStack ) return;
	bool b2Dproj = false;
	D3DXMATRIX mt;
	D3DXMatrixTranspose( &mt,(D3DXMATRIX*)m );
	RecordModelviewOp( MVOP_LOAD, &mt.m[0][0] );
	if( D3DGlobal.settings.projectionFix ) {
		if( D3DState.TransformState.matrixMode == GL_PROJECTION ) {
			b2Dproj =( mt[2*4+3] >= 0 );
			ProjectionMatrix_GLtoD3D( mt );
		}
	}
	D3DState.currentMatrixStack->load( mt );
	*D3DState.currentMatrixModified = true;
	CheckTexCoordOffset_Hack( b2Dproj );

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DXMATRIX model, view;
		matrix_detect_process_upload(&mt.m[0][0], &model, &view);
		D3DGlobal.modelMatrixStack->load(model);
		D3DGlobal.viewMatrixStack->load(view);
	}
}
OPENGL_API void WINAPI glLoadTransposeMatrixd( const GLdouble *m )
{
	DL_RECORD_MAT16D( glLoadTransposeMatrixd, m );
	if( !D3DState.currentMatrixStack ) return;
	bool b2Dproj = false;
	D3DXMATRIX mt;
	for( int i = 0; i < 4; ++i ) 
		for( int j = 0; j < 4; ++i ) 
			mt.m[i][j] =(FLOAT)m[i*4+j];
	if( D3DGlobal.settings.projectionFix ) {
		if( D3DState.TransformState.matrixMode == GL_PROJECTION ) {
			b2Dproj =( mt[2*4+3] >= 0 );
			ProjectionMatrix_GLtoD3D( mt );
		}
	}
	D3DState.currentMatrixStack->load( mt );
	*D3DState.currentMatrixModified = true;
	CheckTexCoordOffset_Hack( b2Dproj );

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DXMATRIX model, view;
		matrix_detect_process_upload(&mt.m[0][0], &model, &view);
		D3DGlobal.modelMatrixStack->load(model);
		D3DGlobal.viewMatrixStack->load(view);
	}
}
OPENGL_API void WINAPI glMultTransposeMatrixf( const GLfloat *m )
{
	DL_RECORD_MAT16F( glMultTransposeMatrixf, m );
	if( !D3DState.currentMatrixStack ) return;
	D3DXMATRIX mt;
	D3DXMatrixTranspose( &mt,(D3DXMATRIX*)m );
	RecordModelviewOp( MVOP_MULT, &mt.m[0][0] );
	D3DState.currentMatrixStack->multiply( mt );
	*D3DState.currentMatrixModified = true;
	CheckTexCoordOffset_Hack( false );

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->multiply( mt );
	}
}
OPENGL_API void WINAPI glMultTransposeMatrixd( const GLdouble *m )
{
	DL_RECORD_MAT16D( glMultTransposeMatrixd, m );
	if( !D3DState.currentMatrixStack ) return;
	D3DXMATRIX mt;
	for( int i = 0; i < 4; ++i ) 
		for( int j = 0; j < 4; ++i ) 
			mt.m[i][j] =(FLOAT)m[i*4+j];
	D3DState.currentMatrixStack->multiply( mt );
	*D3DState.currentMatrixModified = true;
	CheckTexCoordOffset_Hack( false );

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->multiply( mt );
	}
}
OPENGL_API void WINAPI glFrustum( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar )
{
	DL_RECORD_6( glFrustum, left, right, bottom, top, zNear, zFar );
	if( !D3DState.currentMatrixStack ) return;
	D3DXMATRIX m;
	D3DXMatrixPerspectiveOffCenterRH( &m,(FLOAT)left,(FLOAT)right,(FLOAT)bottom,(FLOAT)top,(FLOAT)zNear,(FLOAT)zFar );
	D3DState.currentMatrixStack->multiply( m );
	*D3DState.currentMatrixModified = true;
	CheckTexCoordOffset_Hack( false );
}
OPENGL_API void WINAPI glOrtho( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar )
{
	DL_RECORD_6( glOrtho, left, right, bottom, top, zNear, zFar );
	if( !D3DState.currentMatrixStack ) return;
	D3DXMATRIX m;
	D3DXMatrixOrthoOffCenterRH( &m,(FLOAT)left + D3DState.viewport_offX,
		(FLOAT)right + D3DState.viewport_offX,
		(FLOAT)bottom - D3DState.viewport_offY,
		(FLOAT)top - D3DState.viewport_offY,
		(FLOAT)zNear,(FLOAT)zFar );
	D3DState.currentMatrixStack->multiply( m );
	*D3DState.currentMatrixModified = true;
	CheckTexCoordOffset_Hack( true );
}
OPENGL_API void WINAPI glPopMatrix( void )
{
	DL_RECORD_0( glPopMatrix );
	if( !D3DState.currentMatrixStack ) return;
	RecordModelviewOp( MVOP_POP, nullptr );
	HRESULT hr = D3DState.currentMatrixStack->pop( );
	if( FAILED( hr ) ) QGL_SET_ERROR(hr);
	*D3DState.currentMatrixModified = true;

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->pop( );
		D3DGlobal.viewMatrixStack->pop( );
	}
}
OPENGL_API void WINAPI glPushMatrix( void )
{
	DL_RECORD_0( glPushMatrix );
	if( !D3DState.currentMatrixStack ) return;
	RecordModelviewOp( MVOP_PUSH, nullptr );
	HRESULT hr = D3DState.currentMatrixStack->push( );
	if( FAILED( hr ) ) QGL_SET_ERROR(hr);

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->push( );
		D3DGlobal.viewMatrixStack->push( );
	}
}
OPENGL_API void WINAPI glRotatef( GLfloat angle, GLfloat x, GLfloat y, GLfloat z )
{
	DL_RECORD_4( glRotatef, angle, x, y, z );
	if( !D3DState.currentMatrixStack ) return;
	RecordModelviewOp( MVOP_ROTATE, nullptr, angle, x, y, z );
	D3DXMATRIX m;
	D3DXVECTOR3 v( x,y,z );
	D3DXMatrixRotationAxis( &m, &v, D3DXToRadian( angle ) );
	D3DState.currentMatrixStack->multiply( m );
	*D3DState.currentMatrixModified = true;

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->multiply( m );
	}
}
OPENGL_API void WINAPI glRotated( GLdouble angle, GLdouble x, GLdouble y, GLdouble z )
{
	DL_RECORD_4( glRotated, angle, x, y, z );
	if( !D3DState.currentMatrixStack ) return;
	RecordModelviewOp( MVOP_ROTATE, nullptr, (GLfloat)angle, (GLfloat)x, (GLfloat)y, (GLfloat)z );
	D3DXMATRIX m;
	D3DXVECTOR3 v( (FLOAT)x,(FLOAT)y,(FLOAT)z );
	D3DXMatrixRotationAxis( &m, &v, D3DXToRadian( (FLOAT)angle ) );
	D3DState.currentMatrixStack->multiply( m );
	*D3DState.currentMatrixModified = true;

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->multiply( m );
	}
}
OPENGL_API void WINAPI glScalef( GLfloat x, GLfloat y, GLfloat z )
{
	DL_RECORD_3( glScalef, x, y, z );
	if( !D3DState.currentMatrixStack ) return;
	RecordModelviewOp( MVOP_SCALE, nullptr, x, y, z );
	D3DXMATRIX m;
	D3DXMatrixScaling( &m, x, y, z );
	D3DState.currentMatrixStack->multiply( m );
	*D3DState.currentMatrixModified = true;

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->multiply( m );
	}
}
OPENGL_API void WINAPI glScaled( GLdouble x, GLdouble y, GLdouble z )
{
	DL_RECORD_3( glScaled, x, y, z );
	if( !D3DState.currentMatrixStack ) return;
	RecordModelviewOp( MVOP_SCALE, nullptr, (GLfloat)x, (GLfloat)y, (GLfloat)z );
	D3DXMATRIX m;
	D3DXMatrixScaling( &m,(FLOAT)x,(FLOAT)y,(FLOAT)z );
	D3DState.currentMatrixStack->multiply( m );
	*D3DState.currentMatrixModified = true;

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->multiply( m );
	}
}
OPENGL_API void WINAPI glTranslatef( GLfloat x, GLfloat y, GLfloat z )
{
	DL_RECORD_3( glTranslatef, x, y, z );
	if( !D3DState.currentMatrixStack ) return;
	RecordModelviewOp( MVOP_TRANSLATE, nullptr, x, y, z );
	D3DXMATRIX m;
	D3DXMatrixTranslation( &m, x, y, z );
	D3DState.currentMatrixStack->multiply( m );
	*D3DState.currentMatrixModified = true;

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->multiply( m );
	}
}
OPENGL_API void WINAPI glTranslated( GLdouble x, GLdouble y, GLdouble z )
{
	DL_RECORD_3( glTranslated, x, y, z );
	if( !D3DState.currentMatrixStack ) return;
	RecordModelviewOp( MVOP_TRANSLATE, nullptr, (GLfloat)x, (GLfloat)y, (GLfloat)z );
	D3DXMATRIX m;
	D3DXMatrixTranslation( &m,(FLOAT)x,(FLOAT)y,(FLOAT)z );
	D3DState.currentMatrixStack->multiply( m );
	*D3DState.currentMatrixModified = true;

	if (D3DState.TransformState.matrixMode == GL_MODELVIEW)
	{
		D3DGlobal.modelMatrixStack->multiply( m );
	}
}