#pragma once

// Black-box OpenGL harness: loads a built QindieGL opengl32.dll, creates a
// hidden window with a real D3D9-backed context and resolves the entry points
// the tests use. Core functions come from the DLL exports, extension functions
// from wglGetProcAddress exactly as an application would obtain them.

#include <windows.h>
#include <string>

#include "../code/gl_headers/gl.h"
#include "../code/gl_headers/glext.h"

// DS2's historical misspelling of glMultiTexCoord4svARB, exported by QindieGL.
typedef void (APIENTRY *PFNGLMULTITEXCOORD4SDARBPROC)(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);

struct GLApi
{
	decltype(&::glClear) Clear;
	decltype(&::glClearColor) ClearColor;
	decltype(&::glViewport) Viewport;
	decltype(&::glMatrixMode) MatrixMode;
	decltype(&::glLoadIdentity) LoadIdentity;
	decltype(&::glEnable) Enable;
	decltype(&::glDisable) Disable;
	decltype(&::glEnableClientState) EnableClientState;
	decltype(&::glDisableClientState) DisableClientState;
	decltype(&::glVertexPointer) VertexPointer;
	decltype(&::glColorPointer) ColorPointer;
	decltype(&::glNormalPointer) NormalPointer;
	decltype(&::glTexCoordPointer) TexCoordPointer;
	decltype(&::glDrawArrays) DrawArrays;
	decltype(&::glDrawElements) DrawElements;
	decltype(&::glReadPixels) ReadPixels;
	decltype(&::glGetError) GetError;
	decltype(&::glGetFloatv) GetFloatv;
	decltype(&::glGenTextures) GenTextures;
	decltype(&::glDeleteTextures) DeleteTextures;
	decltype(&::glBindTexture) BindTexture;
	decltype(&::glTexImage2D) TexImage2D;
	decltype(&::glTexParameteri) TexParameteri;
	decltype(&::glColor4ub) Color4ub;
	decltype(&::glFinish) Finish;
	decltype(&::glGetString) GetString;
	decltype(&::glLightfv) Lightfv;
	decltype(&::glGetLightfv) GetLightfv;
	decltype(&::glLoadMatrixf) LoadMatrixf;
	decltype(&::glOrtho) Ortho;
	decltype(&::glDepthMask) DepthMask;

	PFNGLBINDBUFFERARBPROC BindBufferARB;
	PFNGLGENBUFFERSARBPROC GenBuffersARB;
	PFNGLDELETEBUFFERSARBPROC DeleteBuffersARB;
	PFNGLISBUFFERARBPROC IsBufferARB;
	PFNGLBUFFERDATAARBPROC BufferDataARB;
	PFNGLBUFFERSUBDATAARBPROC BufferSubDataARB;
	PFNGLGETBUFFERSUBDATAARBPROC GetBufferSubDataARB;
	PFNGLMAPBUFFERARBPROC MapBufferARB;
	PFNGLUNMAPBUFFERARBPROC UnmapBufferARB;
	PFNGLGETBUFFERPARAMETERIVARBPROC GetBufferParameterivARB;
	PFNGLGETBUFFERPOINTERVARBPROC GetBufferPointervARB;
	PFNGLDRAWRANGEELEMENTSEXTPROC DrawRangeElementsEXT;
	PFNGLACTIVETEXTUREARBPROC ActiveTextureARB;
	PFNGLSECONDARYCOLORPOINTEREXTPROC SecondaryColorPointerEXT;
	PFNGLMULTITEXCOORD4SDARBPROC MultiTexCoord4sdARB;
};

extern GLApi gl;

// Loads dllPath, creates a width x height hidden window and makes a context
// current. The process working directory must already contain QindieGL.ini.
bool Harness_Init( const char *dllPath, int width, int height, std::string &error );
void Harness_Shutdown();
// Presents the frame (wglSwapBuffers). Read pixels before calling it.
void Harness_Swap();
// wglGetProcAddress of the loaded DLL.
PROC Harness_GetProcAddress( const char *name );
int Harness_Width();
int Harness_Height();

struct RGBA8 { unsigned char r, g, b, a; };
RGBA8 Harness_ReadPixel( int x, int y );
RGBA8 Harness_ReadCenter();
