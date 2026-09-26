#include "gl_harness.h"

GLApi gl = {};

namespace {
	HMODULE g_dll = nullptr;
	HWND g_window = nullptr;
	HDC g_dc = nullptr;
	HGLRC g_context = nullptr;
	int g_width = 0;
	int g_height = 0;

	typedef HGLRC (WINAPI *PFNWGLCREATECONTEXT)(HDC);
	typedef BOOL (WINAPI *PFNWGLMAKECURRENT)(HDC, HGLRC);
	typedef BOOL (WINAPI *PFNWGLDELETECONTEXT)(HGLRC);
	typedef PROC (WINAPI *PFNWGLGETPROCADDRESS)(LPCSTR);
	typedef int (WINAPI *PFNWGLCHOOSEPIXELFORMAT)(HDC, CONST PIXELFORMATDESCRIPTOR *);
	typedef BOOL (WINAPI *PFNWGLSETPIXELFORMAT)(HDC, int, CONST PIXELFORMATDESCRIPTOR *);

	PFNWGLCREATECONTEXT g_wglCreateContext = nullptr;
	PFNWGLMAKECURRENT g_wglMakeCurrent = nullptr;
	PFNWGLDELETECONTEXT g_wglDeleteContext = nullptr;
	PFNWGLGETPROCADDRESS g_wglGetProcAddress = nullptr;
	typedef BOOL (WINAPI *PFNWGLSWAPBUFFERS)(HDC);
	PFNWGLSWAPBUFFERS g_wglSwapBuffers = nullptr;

	template<typename T>
	bool LoadExport( T &function, const char *name, std::string &error )
	{
		function = reinterpret_cast<T>(GetProcAddress(g_dll, name));
		if (!function) error = std::string("missing export ") + name;
		return function != nullptr;
	}

	// Extension entry points are optional: a NULL result is part of what the
	// tests verify, so the individual test reports it.
	template<typename T>
	void LoadExtension( T &function, const char *name )
	{
		function = reinterpret_cast<T>(g_wglGetProcAddress(name));
	}

	LRESULT CALLBACK WindowProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam )
	{
		return DefWindowProcA(window, message, wParam, lParam);
	}
}

bool Harness_Init( const char *dllPath, int width, int height, std::string &error )
{
	g_dll = LoadLibraryA(dllPath);
	if (!g_dll) {
		error = std::string("LoadLibrary failed for ") + dllPath;
		return false;
	}

	PFNWGLCHOOSEPIXELFORMAT choosePixelFormat = nullptr;
	PFNWGLSETPIXELFORMAT setPixelFormat = nullptr;
	if (!LoadExport(g_wglCreateContext, "wglCreateContext", error) ||
		!LoadExport(g_wglMakeCurrent, "wglMakeCurrent", error) ||
		!LoadExport(g_wglDeleteContext, "wglDeleteContext", error) ||
		!LoadExport(g_wglGetProcAddress, "wglGetProcAddress", error) ||
		!LoadExport(g_wglSwapBuffers, "wglSwapBuffers", error) ||
		!LoadExport(choosePixelFormat, "wglChoosePixelFormat", error) ||
		!LoadExport(setPixelFormat, "wglSetPixelFormat", error) ||
		!LoadExport(gl.Clear, "glClear", error) ||
		!LoadExport(gl.ClearColor, "glClearColor", error) ||
		!LoadExport(gl.Viewport, "glViewport", error) ||
		!LoadExport(gl.MatrixMode, "glMatrixMode", error) ||
		!LoadExport(gl.LoadIdentity, "glLoadIdentity", error) ||
		!LoadExport(gl.Enable, "glEnable", error) ||
		!LoadExport(gl.Disable, "glDisable", error) ||
		!LoadExport(gl.EnableClientState, "glEnableClientState", error) ||
		!LoadExport(gl.DisableClientState, "glDisableClientState", error) ||
		!LoadExport(gl.VertexPointer, "glVertexPointer", error) ||
		!LoadExport(gl.ColorPointer, "glColorPointer", error) ||
		!LoadExport(gl.NormalPointer, "glNormalPointer", error) ||
		!LoadExport(gl.TexCoordPointer, "glTexCoordPointer", error) ||
		!LoadExport(gl.DrawArrays, "glDrawArrays", error) ||
		!LoadExport(gl.DrawElements, "glDrawElements", error) ||
		!LoadExport(gl.ReadPixels, "glReadPixels", error) ||
		!LoadExport(gl.GetError, "glGetError", error) ||
		!LoadExport(gl.GetFloatv, "glGetFloatv", error) ||
		!LoadExport(gl.GenTextures, "glGenTextures", error) ||
		!LoadExport(gl.DeleteTextures, "glDeleteTextures", error) ||
		!LoadExport(gl.BindTexture, "glBindTexture", error) ||
		!LoadExport(gl.TexImage2D, "glTexImage2D", error) ||
		!LoadExport(gl.TexParameteri, "glTexParameteri", error) ||
		!LoadExport(gl.Color4ub, "glColor4ub", error) ||
		!LoadExport(gl.Finish, "glFinish", error) ||
		!LoadExport(gl.GetString, "glGetString", error) ||
		!LoadExport(gl.Lightfv, "glLightfv", error) ||
		!LoadExport(gl.GetLightfv, "glGetLightfv", error) ||
		!LoadExport(gl.LoadMatrixf, "glLoadMatrixf", error) ||
		!LoadExport(gl.Ortho, "glOrtho", error) ||
		!LoadExport(gl.DepthMask, "glDepthMask", error))
		return false;

	WNDCLASSA windowClass = {};
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = GetModuleHandleA(nullptr);
	windowClass.lpszClassName = "QindieGLTestWindow";
	RegisterClassA(&windowClass);

	RECT rect = { 0, 0, width, height };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	g_window = CreateWindowA(windowClass.lpszClassName, "QindieGL tests", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
		nullptr, nullptr, windowClass.hInstance, nullptr);
	if (!g_window) {
		error = "CreateWindow failed";
		return false;
	}
	g_dc = GetDC(g_window);
	g_width = width;
	g_height = height;

	PIXELFORMATDESCRIPTOR pixelFormat = {};
	pixelFormat.nSize = sizeof(pixelFormat);
	pixelFormat.nVersion = 1;
	pixelFormat.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pixelFormat.iPixelType = PFD_TYPE_RGBA;
	pixelFormat.cColorBits = 32;
	pixelFormat.cDepthBits = 24;
	const int format = choosePixelFormat(g_dc, &pixelFormat);
	setPixelFormat(g_dc, format, &pixelFormat);

	g_context = g_wglCreateContext(g_dc);
	if (!g_context || !g_wglMakeCurrent(g_dc, g_context)) {
		error = "wglCreateContext/wglMakeCurrent failed (no D3D9 device?)";
		return false;
	}

	LoadExtension(gl.BindBufferARB, "glBindBufferARB");
	LoadExtension(gl.GenBuffersARB, "glGenBuffersARB");
	LoadExtension(gl.DeleteBuffersARB, "glDeleteBuffersARB");
	LoadExtension(gl.IsBufferARB, "glIsBufferARB");
	LoadExtension(gl.BufferDataARB, "glBufferDataARB");
	LoadExtension(gl.BufferSubDataARB, "glBufferSubDataARB");
	LoadExtension(gl.GetBufferSubDataARB, "glGetBufferSubDataARB");
	LoadExtension(gl.MapBufferARB, "glMapBufferARB");
	LoadExtension(gl.UnmapBufferARB, "glUnmapBufferARB");
	LoadExtension(gl.GetBufferParameterivARB, "glGetBufferParameterivARB");
	LoadExtension(gl.GetBufferPointervARB, "glGetBufferPointervARB");
	LoadExtension(gl.DrawRangeElementsEXT, "glDrawRangeElementsEXT");
	LoadExtension(gl.ActiveTextureARB, "glActiveTextureARB");
	LoadExtension(gl.SecondaryColorPointerEXT, "glSecondaryColorPointerEXT");
	LoadExtension(gl.MultiTexCoord4sdARB, "glMultiTexCoord4sdARB");

	gl.Viewport(0, 0, width, height);
	return true;
}

void Harness_Shutdown()
{
	if (g_context) {
		g_wglMakeCurrent(nullptr, nullptr);
		g_wglDeleteContext(g_context);
		g_context = nullptr;
	}
	if (g_dc) {
		ReleaseDC(g_window, g_dc);
		g_dc = nullptr;
	}
	if (g_window) {
		DestroyWindow(g_window);
		g_window = nullptr;
	}
	// The DLL stays loaded: QindieGL keeps process-wide D3D state until exit.
}

void Harness_Swap()
{
	if (g_dc) g_wglSwapBuffers(g_dc);
}

PROC Harness_GetProcAddress( const char *name )
{
	return g_wglGetProcAddress ? g_wglGetProcAddress(name) : nullptr;
}

int Harness_Width() { return g_width; }
int Harness_Height() { return g_height; }

RGBA8 Harness_ReadPixel( int x, int y )
{
	RGBA8 pixel = { 0, 0, 0, 0 };
	gl.Finish();
	gl.ReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
	return pixel;
}

RGBA8 Harness_ReadCenter()
{
	return Harness_ReadPixel(g_width / 2, g_height / 2);
}
