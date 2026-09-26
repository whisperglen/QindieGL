// Fixed-function light state. QindieGL stores directional lights as the
// negated eye-space direction D3D expects; GL-visible state must not leak it.

#include <stdio.h>

#include "tests.h"
#include "gl_harness.h"

#define CHECK(condition, ...) do { \
		char checkMessage_[320]; \
		sprintf_s(checkMessage_, __VA_ARGS__); \
		xassert_str(!!(condition), checkMessage_, __func__, (unsigned)__LINE__, __FILE__); \
	} while (0)

namespace {

void ExpectLightPosition( const char *label, float x, float y, float z, float w )
{
	float position[4] = {};
	gl.GetLightfv(GL_LIGHT0, GL_POSITION, position);
	CHECK(position[0] == x && position[1] == y && position[2] == z && position[3] == w,
		"%s: GL_POSITION (%g,%g,%g,%g), expected (%g,%g,%g,%g)", label,
		position[0], position[1], position[2], position[3], x, y, z, w);
}

// A white full-viewport quad facing +Z, lit by LIGHT0 only.
unsigned char DrawLitQuad()
{
	static const float quad[12] = { -1, -1, 0, 1, -1, 0, -1, 1, 0, 1, 1, 0 };
	static const float normals[12] = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
	gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.Enable(GL_LIGHTING);
	gl.Enable(GL_LIGHT0);
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.EnableClientState(GL_NORMAL_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, quad);
	gl.NormalPointer(GL_FLOAT, 0, normals);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	const RGBA8 pixel = Harness_ReadCenter();
	Harness_Swap();
	gl.DisableClientState(GL_NORMAL_ARRAY);
	gl.DisableClientState(GL_VERTEX_ARRAY);
	gl.Disable(GL_LIGHT0);
	gl.Disable(GL_LIGHTING);
	return pixel.g;
}

} // namespace

void do_lighting_tests()
{
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();

	// GL default: directional light at (0,0,1,0), shining along -Z onto
	// surfaces facing +Z (0.8 diffuse + 0.04 ambient with default material).
	ExpectLightPosition("default LIGHT0", 0.0f, 0.0f, 1.0f, 0.0f);
	const unsigned char defaultLit = DrawLitQuad();
	CHECK(defaultLit > 150, "default LIGHT0 lights a +Z-facing quad: green %d, expected ~214", defaultLit);

	const float behind[4] = { 0.0f, 0.0f, -1.0f, 0.0f };
	gl.Lightfv(GL_LIGHT0, GL_POSITION, behind);
	ExpectLightPosition("LIGHT0 set to (0,0,-1,0)", 0.0f, 0.0f, -1.0f, 0.0f);
	const unsigned char backLit = DrawLitQuad();
	CHECK(backLit < 80, "light behind a +Z-facing quad: green %d, expected ~10", backLit);

	const float front[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
	gl.Lightfv(GL_LIGHT0, GL_POSITION, front);
	ExpectLightPosition("LIGHT0 set to (0,0,1,0)", 0.0f, 0.0f, 1.0f, 0.0f);
}
