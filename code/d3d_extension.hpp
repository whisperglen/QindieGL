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
#ifndef QINDIEGL_D3D_EXTENSION_H
#define QINDIEGL_D3D_EXTENSION_H

extern void D3DExtension_BuildExtensionsString();
extern void D3DExtension_DumpMissingProcs();
extern void D3DExtension_DumpProcSummary();
extern void D3DExtension_RecordStubInvocation( const char *name );

extern GLuint ARB_GetBoundVertexProgram();
extern GLuint ARB_GetBoundFragmentProgram();

extern OPENGL_API void WINAPI glBindBuffer(GLenum target, GLuint buffer);
extern OPENGL_API void WINAPI glBindBufferARB(GLenum target, GLuint buffer);
extern OPENGL_API void WINAPI glDeleteBuffers(GLsizei n, const GLuint *buffers);
extern OPENGL_API void WINAPI glDeleteBuffersARB(GLsizei n, const GLuint *buffers);
extern OPENGL_API void WINAPI glGenBuffers(GLsizei n, GLuint *buffers);
extern OPENGL_API void WINAPI glGenBuffersARB(GLsizei n, GLuint *buffers);
extern OPENGL_API GLboolean WINAPI glIsBuffer(GLuint buffer);
extern OPENGL_API GLboolean WINAPI glIsBufferARB(GLuint buffer);
extern OPENGL_API void WINAPI glBufferData(GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage);
extern OPENGL_API void WINAPI glBufferDataARB(GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage);
extern OPENGL_API void WINAPI glBufferSubData(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid *data);
extern OPENGL_API void WINAPI glBufferSubDataARB(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid *data);
extern OPENGL_API void WINAPI glGetBufferSubData(GLenum target, GLintptrARB offset, GLsizeiptrARB size, GLvoid *data);
extern OPENGL_API void WINAPI glGetBufferSubDataARB(GLenum target, GLintptrARB offset, GLsizeiptrARB size, GLvoid *data);
extern OPENGL_API void WINAPI glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params);
extern OPENGL_API void WINAPI glGetBufferParameterivARB(GLenum target, GLenum pname, GLint *params);
extern OPENGL_API void WINAPI glGetBufferPointerv(GLenum target, GLenum pname, GLvoid **params);
extern OPENGL_API void WINAPI glGetBufferPointervARB(GLenum target, GLenum pname, GLvoid **params);
extern OPENGL_API GLvoid* WINAPI glMapBuffer(GLenum target, GLenum access);
extern OPENGL_API GLvoid* WINAPI glMapBufferARB(GLenum target, GLenum access);
extern OPENGL_API GLboolean WINAPI glUnmapBuffer(GLenum target);
extern OPENGL_API GLboolean WINAPI glUnmapBufferARB(GLenum target);
extern OPENGL_API void WINAPI glActiveTexture(GLenum texture);
extern OPENGL_API void WINAPI glClientActiveTexture(GLenum texture);
extern OPENGL_API void WINAPI glMultiTexCoord1s( GLenum target, GLshort s );
extern OPENGL_API void WINAPI glMultiTexCoord1i( GLenum target, GLint s );
extern OPENGL_API void WINAPI glMultiTexCoord1f( GLenum target, GLfloat s );
extern OPENGL_API void WINAPI glMultiTexCoord1d( GLenum target, GLdouble s );
extern OPENGL_API void WINAPI glMultiTexCoord1dEXT( GLenum target, GLdouble s );
extern OPENGL_API void WINAPI glMultiTexCoord2s( GLenum target, GLshort s, GLshort t );
extern OPENGL_API void WINAPI glMultiTexCoord2i( GLenum target, GLint s, GLint t );
extern OPENGL_API void WINAPI glMultiTexCoord2f( GLenum target, GLfloat s, GLfloat t );
extern OPENGL_API void WINAPI glMultiTexCoord2d( GLenum target, GLdouble s, GLdouble t );
extern OPENGL_API void WINAPI glMultiTexCoord3s( GLenum target, GLshort s, GLshort t, GLshort r );
extern OPENGL_API void WINAPI glMultiTexCoord3i( GLenum target, GLint s, GLint t, GLint r );
extern OPENGL_API void WINAPI glMultiTexCoord3f( GLenum target, GLfloat s, GLfloat t, GLfloat r );
extern OPENGL_API void WINAPI glMultiTexCoord3d( GLenum target, GLdouble s, GLdouble t, GLdouble r );
extern OPENGL_API void WINAPI glMultiTexCoord4s( GLenum target, GLshort s, GLshort t, GLshort r, GLshort q );
extern OPENGL_API void WINAPI glMultiTexCoord4i( GLenum target, GLint s, GLint t, GLint r, GLint q );
extern OPENGL_API void WINAPI glMultiTexCoord4f( GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q );
extern OPENGL_API void WINAPI glMultiTexCoord4d( GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q );
extern OPENGL_API void WINAPI glMultiTexCoord4sdARB( GLenum target, GLshort s, GLshort t, GLshort r, GLshort q );
extern OPENGL_API void WINAPI glMultiTexCoord1sv( GLenum target, const GLshort *v );
extern OPENGL_API void WINAPI glMultiTexCoord1iv( GLenum target, const GLint *v );
extern OPENGL_API void WINAPI glMultiTexCoord1fv( GLenum target, const GLfloat *v );
extern OPENGL_API void WINAPI glMultiTexCoord1dv( GLenum target, const GLdouble *v );
extern OPENGL_API void WINAPI glMultiTexCoord2sv( GLenum target, const GLshort *v );
extern OPENGL_API void WINAPI glMultiTexCoord2iv( GLenum target, const GLint *v );
extern OPENGL_API void WINAPI glMultiTexCoord2fv( GLenum target, const GLfloat *v );
extern OPENGL_API void WINAPI glMultiTexCoord2dv( GLenum target, const GLdouble *v );
extern OPENGL_API void WINAPI glMultiTexCoord3sv( GLenum target, const GLshort *v );
extern OPENGL_API void WINAPI glMultiTexCoord3iv( GLenum target, const GLint *v );
extern OPENGL_API void WINAPI glMultiTexCoord3fv( GLenum target, const GLfloat *v );
extern OPENGL_API void WINAPI glMultiTexCoord3dv( GLenum target, const GLdouble *v );
extern OPENGL_API void WINAPI glMultiTexCoord4sv( GLenum target, const GLshort *v );
extern OPENGL_API void WINAPI glMultiTexCoord4iv( GLenum target, const GLint *v );
extern OPENGL_API void WINAPI glMultiTexCoord4fv( GLenum target, const GLfloat *v );
extern OPENGL_API void WINAPI glMultiTexCoord4dv( GLenum target, const GLdouble *v );
extern OPENGL_API void WINAPI WINAPI glActiveStencilFace(GLenum face);
extern OPENGL_API void WINAPI WINAPI glDeleteTextures(GLsizei n, const GLuint *textures);
extern OPENGL_API void WINAPI WINAPI glGenTextures(GLsizei n, GLuint *textures);
extern OPENGL_API GLboolean WINAPI glIsTexture(GLuint texture);
extern OPENGL_API void WINAPI glBindTexture(GLenum target, GLuint texture);
extern OPENGL_API GLboolean WINAPI glAreTexturesResident(GLsizei n,  const GLuint *textures,  GLboolean *residences);
extern OPENGL_API void WINAPI glPrioritizeTextures(GLsizei n,  const GLuint *textures,  const GLclampf *priorities);
extern OPENGL_API void WINAPI glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
extern OPENGL_API void WINAPI glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels);
extern OPENGL_API void WINAPI glCopyTexImage3D(GLenum target,  GLint level,  GLenum internalFormat,  GLint x,  GLint y,  GLint z,  GLsizei width,  GLsizei height,  GLsizei depth,  GLint border);
extern OPENGL_API void WINAPI glCopyTexSubImage3D(GLenum target,  GLint level,  GLint xoffset,  GLint yoffset,  GLint zoffset,  GLint x,  GLint y,  GLint z,  GLsizei width,  GLsizei height,  GLsizei depth);
extern OPENGL_API BOOL WINAPI wglSwapInterval(int interval);
extern OPENGL_API int WINAPI wglGetSwapInterval();

#ifndef HPBUFFERARB
typedef void* HPBUFFERARB;
#endif

extern OPENGL_API HPBUFFERARB WINAPI wglCreatePbufferARB(HDC hDC, int iPixelFormat, int iWidth, int iHeight, const int *piAttribList);
extern OPENGL_API HDC WINAPI wglGetPbufferDCARB(HPBUFFERARB hPbuffer);
extern OPENGL_API int WINAPI wglReleasePbufferDCARB(HPBUFFERARB hPbuffer, HDC hDC);
extern OPENGL_API BOOL WINAPI wglDestroyPbufferARB(HPBUFFERARB hPbuffer);
extern OPENGL_API BOOL WINAPI wglQueryPbufferARB(HPBUFFERARB hPbuffer, int iAttribute, int *piValue);
extern OPENGL_API BOOL WINAPI wglBindTexImageARB(HPBUFFERARB hPbuffer, int iBuffer);
extern OPENGL_API BOOL WINAPI wglReleaseTexImageARB(HPBUFFERARB hPbuffer, int iBuffer);
extern OPENGL_API BOOL WINAPI wglSetPbufferAttribARB(HPBUFFERARB hPbuffer, const int *piAttribList);
extern OPENGL_API BOOL WINAPI wglChoosePixelFormatARB(HDC hdc, const int *piAttribIList, const FLOAT *pfAttribFList, UINT nMaxFormats, int *piFormats, UINT *nNumFormats);
extern OPENGL_API BOOL WINAPI wglGetPixelFormatAttribivARB(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes, const int *piAttributes, int *piValues);
extern OPENGL_API BOOL WINAPI wglGetPixelFormatAttribfvARB(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes, const int *piAttributes, FLOAT *pfValues);
extern OPENGL_API void WINAPI glSelectTexture(GLenum texture);
extern OPENGL_API void WINAPI glMTexCoord2f( GLenum target, GLfloat s, GLfloat t );
extern OPENGL_API void WINAPI glMTexCoord2fv( GLenum target, const GLfloat *v );
extern OPENGL_API void WINAPI glLoadTransposeMatrixf(const GLfloat *m);
extern OPENGL_API void WINAPI glLoadTransposeMatrixd(const GLdouble *m);
extern OPENGL_API void WINAPI glMultTransposeMatrixf(const GLfloat *m);
extern OPENGL_API void WINAPI glMultTransposeMatrixd(const GLdouble *m);
extern OPENGL_API void WINAPI glFogCoordd( GLdouble coord );
extern OPENGL_API void WINAPI glFogCoordf( GLfloat coord );
extern OPENGL_API void WINAPI glFogCoorddv( GLdouble *coord );
extern OPENGL_API void WINAPI glFogCoordfv( GLfloat *coord );
extern OPENGL_API void WINAPI glFogCoordPointer(GLenum type,  GLsizei stride,  const GLvoid *pointer);
extern OPENGL_API void WINAPI glSecondaryColor3b(GLbyte red,  GLbyte green,  GLbyte blue);
extern OPENGL_API void WINAPI glSecondaryColor3bv(const GLbyte *v);
extern OPENGL_API void WINAPI glSecondaryColor3d(GLdouble red,  GLdouble green,  GLdouble blue);
extern OPENGL_API void WINAPI glSecondaryColor3dv(const GLdouble *v);
extern OPENGL_API void WINAPI glSecondaryColor3f(GLfloat red,  GLfloat green,  GLfloat blue);
extern OPENGL_API void WINAPI glSecondaryColor3fv(const GLfloat *v);
extern OPENGL_API void WINAPI glSecondaryColor3i(GLint red,  GLint green,  GLint blue);
extern OPENGL_API void WINAPI glSecondaryColor3iv(const GLint *v);
extern OPENGL_API void WINAPI glSecondaryColor3s(GLshort red,  GLshort green,  GLshort blue);
extern OPENGL_API void WINAPI glSecondaryColor3sv(const GLshort *v);
extern OPENGL_API void WINAPI glSecondaryColor3ub(GLubyte red,  GLubyte green,  GLubyte blue);
extern OPENGL_API void WINAPI glSecondaryColor3ubv(const GLubyte *v);
extern OPENGL_API void WINAPI glSecondaryColor3ui(GLuint red,  GLuint green,  GLuint blue);
extern OPENGL_API void WINAPI glSecondaryColor3uiv(const GLuint *v);
extern OPENGL_API void WINAPI glSecondaryColor3us(GLushort red,  GLushort green,  GLushort blue);
extern OPENGL_API void WINAPI glSecondaryColor3usv(const GLushort *v);
extern OPENGL_API void WINAPI glSecondaryColorPointer(GLint size,  GLenum type,  GLsizei stride,  const GLvoid *pointer);
extern OPENGL_API void WINAPI glDrawRangeElements( GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices );
extern OPENGL_API void WINAPI glMultiDrawArrays( GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount );
extern OPENGL_API void WINAPI glMultiDrawElements( GLenum mode, GLsizei *count, GLenum type, const GLvoid **indices, GLsizei primcount );
extern OPENGL_API void WINAPI glLockArrays( GLint first, GLsizei count );
extern OPENGL_API void WINAPI glUnlockArrays();
extern OPENGL_API void WINAPI glBlendColor( GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha );
extern OPENGL_API void WINAPI glBlendEquation( GLenum mode );
extern OPENGL_API void WINAPI glCompressedTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLsizei imageSize, const GLvoid *pixels);
extern OPENGL_API void WINAPI glCompressedTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *pixels);
extern OPENGL_API void WINAPI glCompressedTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid *pixels);
extern OPENGL_API void WINAPI glCompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const GLvoid *pixels);
extern OPENGL_API void WINAPI glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *pixels);
extern OPENGL_API void WINAPI glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid *pixels);
extern OPENGL_API void WINAPI glGetCompressedTexImage(GLenum target, GLint level, GLvoid *img);
extern OPENGL_API void WINAPI glPNTrianglesiATI( GLenum pname, GLint param );
extern OPENGL_API void WINAPI glPNTrianglesfATI( GLenum pname, GLfloat param );

// GL_EXT_blend_func_separate / GL_EXT_blend_equation_separate
extern OPENGL_API void WINAPI glBlendFuncSeparateEXT( GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha );
extern OPENGL_API void WINAPI glBlendEquationSeparateEXT( GLenum modeRGB, GLenum modeAlpha );

// GL 2.0 stencil separate
extern OPENGL_API void WINAPI glStencilFuncSeparate( GLenum face, GLenum func, GLint ref, GLuint mask );
extern OPENGL_API void WINAPI glStencilOpSeparate( GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass );
extern OPENGL_API void WINAPI glStencilMaskSeparate( GLenum face, GLuint mask );

// ARB_vertex_program vertex attrib functions
extern OPENGL_API void WINAPI glVertexAttrib1dARB( GLuint index, GLdouble x );
extern OPENGL_API void WINAPI glVertexAttrib1dvARB( GLuint index, const GLdouble *v );
extern OPENGL_API void WINAPI glVertexAttrib1fARB( GLuint index, GLfloat x );
extern OPENGL_API void WINAPI glVertexAttrib1sARB( GLuint index, GLshort x );
extern OPENGL_API void WINAPI glVertexAttrib1svARB( GLuint index, const GLshort *v );
extern OPENGL_API void WINAPI glVertexAttrib2dARB( GLuint index, GLdouble x, GLdouble y );
extern OPENGL_API void WINAPI glVertexAttrib2dvARB( GLuint index, const GLdouble *v );
extern OPENGL_API void WINAPI glVertexAttrib2fARB( GLuint index, GLfloat x, GLfloat y );
extern OPENGL_API void WINAPI glVertexAttrib2sARB( GLuint index, GLshort x, GLshort y );
extern OPENGL_API void WINAPI glVertexAttrib2svARB( GLuint index, const GLshort *v );
extern OPENGL_API void WINAPI glVertexAttrib3dARB( GLuint index, GLdouble x, GLdouble y, GLdouble z );
extern OPENGL_API void WINAPI glVertexAttrib3dvARB( GLuint index, const GLdouble *v );
extern OPENGL_API void WINAPI glVertexAttrib3fARB( GLuint index, GLfloat x, GLfloat y, GLfloat z );
extern OPENGL_API void WINAPI glVertexAttrib3sARB( GLuint index, GLshort x, GLshort y, GLshort z );
extern OPENGL_API void WINAPI glVertexAttrib3svARB( GLuint index, const GLshort *v );
extern OPENGL_API void WINAPI glVertexAttrib4NbvARB( GLuint index, const GLbyte *v );
extern OPENGL_API void WINAPI glVertexAttrib4NivARB( GLuint index, const GLint *v );
extern OPENGL_API void WINAPI glVertexAttrib4NsvARB( GLuint index, const GLshort *v );
extern OPENGL_API void WINAPI glVertexAttrib4NubARB( GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w );
extern OPENGL_API void WINAPI glVertexAttrib4fARB( GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w );
extern OPENGL_API void WINAPI glVertexAttrib1fvARB( GLuint index, const GLfloat *v );
extern OPENGL_API void WINAPI glVertexAttrib2fvARB( GLuint index, const GLfloat *v );
extern OPENGL_API void WINAPI glVertexAttrib3fvARB( GLuint index, const GLfloat *v );
extern OPENGL_API void WINAPI glVertexAttrib4fvARB( GLuint index, const GLfloat *v );
extern OPENGL_API void WINAPI glVertexAttrib4NubvARB( GLuint index, const GLubyte *v );
extern OPENGL_API void WINAPI glVertexAttrib4NuivARB( GLuint index, const GLuint *v );
extern OPENGL_API void WINAPI glVertexAttrib4NusvARB( GLuint index, const GLushort *v );
extern OPENGL_API void WINAPI glVertexAttrib4bvARB( GLuint index, const GLbyte *v );
extern OPENGL_API void WINAPI glVertexAttrib4dARB( GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w );
extern OPENGL_API void WINAPI glVertexAttrib4dvARB( GLuint index, const GLdouble *v );
extern OPENGL_API void WINAPI glVertexAttrib4ivARB( GLuint index, const GLint *v );
extern OPENGL_API void WINAPI glVertexAttrib4sARB( GLuint index, GLshort x, GLshort y, GLshort z, GLshort w );
extern OPENGL_API void WINAPI glVertexAttrib4svARB( GLuint index, const GLshort *v );
extern OPENGL_API void WINAPI glVertexAttrib4ubvARB( GLuint index, const GLubyte *v );
extern OPENGL_API void WINAPI glVertexAttrib4uivARB( GLuint index, const GLuint *v );
extern OPENGL_API void WINAPI glVertexAttrib4usvARB( GLuint index, const GLushort *v );
extern OPENGL_API void WINAPI glVertexAttribPointerARB( GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer );
extern OPENGL_API void WINAPI glEnableVertexAttribArrayARB( GLuint index );
extern OPENGL_API void WINAPI glDisableVertexAttribArrayARB( GLuint index );
extern OPENGL_API void WINAPI glGetVertexAttribdvARB( GLuint index, GLenum pname, GLdouble *params );
extern OPENGL_API void WINAPI glGetVertexAttribfvARB( GLuint index, GLenum pname, GLfloat *params );
extern OPENGL_API void WINAPI glGetVertexAttribivARB( GLuint index, GLenum pname, GLint *params );
extern OPENGL_API void WINAPI glGetVertexAttribPointervARB( GLuint index, GLenum pname, GLvoid **pointer );

// ARB_occlusion_query
extern OPENGL_API void WINAPI glGenQueriesARB( GLsizei n, GLuint *ids );
extern OPENGL_API void WINAPI glDeleteQueriesARB( GLsizei n, const GLuint *ids );
extern OPENGL_API GLboolean WINAPI glIsQueryARB( GLuint id );
extern OPENGL_API void WINAPI glBeginQueryARB( GLenum target, GLuint id );
extern OPENGL_API void WINAPI glEndQueryARB( GLenum target );
extern OPENGL_API void WINAPI glGetQueryivARB( GLenum target, GLenum pname, GLint *params );
extern OPENGL_API void WINAPI glGetQueryObjectivARB( GLuint id, GLenum pname, GLint *params );
extern OPENGL_API void WINAPI glGetQueryObjectuivARB( GLuint id, GLenum pname, GLuint *params );
void D3DExtension_ReleaseQueryResources();
void D3DExtension_CleanupQueries();

// ARB_point_parameters
extern OPENGL_API void WINAPI glPointParameterfARB( GLenum pname, GLfloat param );
extern OPENGL_API void WINAPI glPointParameterfvARB( GLenum pname, const GLfloat *params );

#endif //QINDIEGL_D3D_EXTENSION_H
