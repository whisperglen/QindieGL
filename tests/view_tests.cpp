// View diagnostics (task brief sections 42 and 43). The child renders frames
// with a perspective, depth-tested world phase followed by an orthographic,
// depth-disabled HUD phase; the parent then checks the projection classes and
// HUD boundary counts in the QindieGL session summary.

#include <stdio.h>
#include <string.h>
#include <string>

#include "tests.h"
#include "gl_harness.h"

#define CHECK(condition, ...) do { \
		char checkMessage_[320]; \
		sprintf_s(checkMessage_, __VA_ARGS__); \
		xassert_str(!!(condition), checkMessage_, __func__, (unsigned)__LINE__, __FILE__); \
	} while (0)

const int kViewTestFrames = 3;

void do_view_tests()
{
	// GL column-major gluPerspective(60, 1, 1, 100).
	const float f = 1.7320508f;
	const float perspective[16] = {
		f, 0, 0, 0,
		0, f, 0, 0,
		0, 0, -101.0f / 99.0f, -1,
		0, 0, -200.0f / 99.0f, 0 };
	static const float world[12] = { -4, -4, -5, 4, -4, -5, -4, 4, -5, 4, 4, -5 };
	static const float hud[12] = { 0, 0, 0, 10, 0, 0, 0, 10, 0, 10, 10, 0 };

	gl.EnableClientState(GL_VERTEX_ARRAY);
	for (int frame = 0; frame < kViewTestFrames; ++frame) {
		gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		gl.MatrixMode(GL_PROJECTION);
		gl.LoadMatrixf(perspective);
		gl.MatrixMode(GL_MODELVIEW);
		gl.LoadIdentity();
		gl.Enable(GL_DEPTH_TEST);
		gl.DepthMask(GL_TRUE);
		gl.Color4ub(0, 255, 0, 255);
		gl.VertexPointer(3, GL_FLOAT, 0, world);
		gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		gl.MatrixMode(GL_PROJECTION);
		gl.LoadIdentity();
		gl.Ortho(0, 64, 0, 64, -1, 1);
		gl.Disable(GL_DEPTH_TEST);
		gl.DepthMask(GL_FALSE);
		gl.Color4ub(255, 255, 255, 255);
		gl.VertexPointer(3, GL_FLOAT, 0, hud);
		gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		if (frame == 0) {
			const RGBA8 centre = Harness_ReadCenter();
			CHECK(centre.g > 200 && centre.r < 40, "perspective world quad visible: (%d,%d,%d)",
				centre.r, centre.g, centre.b);
			const RGBA8 corner = Harness_ReadPixel(2, 2);
			CHECK(corner.r > 200 && corner.g > 200, "orthographic HUD quad visible: (%d,%d,%d)",
				corner.r, corner.g, corner.b);
		}
		Harness_Swap();
	}
	gl.DisableClientState(GL_VERTEX_ARRAY);
	gl.DepthMask(GL_TRUE);
}

// Runs in the parent after the child exited and QindieGL wrote its summary.
void check_view_diagnostics_log( const std::string &logPath )
{
	std::string log;
	FILE *file = nullptr;
	if (!fopen_s(&file, logPath.c_str(), "rb") && file) {
		char buffer[4096];
		size_t read;
		while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) log.append(buffer, read);
		fclose(file);
	}
	CHECK(!log.empty(), "view diagnostics log %s readable", logPath.c_str());

	char expected[160];
	const char *patterns[] = {
		"PERSPECTIVE fovY=60.0 aspect=1.000 near=1.000 far=100.0",
		"ORTHO left=0.0 right=64.0 bottom=0.0 top=64.0",
		"Frames with orthographic draws before the last world draw: 0",
	};
	for (const char *pattern : patterns)
		CHECK(log.find(pattern) != std::string::npos, "session summary contains \"%s\"", pattern);
	sprintf_s(expected, "Frames with world draws (perspective, depth tested): %d of %d",
		kViewTestFrames, kViewTestFrames);
	CHECK(log.find(expected) != std::string::npos, "session summary contains \"%s\"", expected);
	sprintf_s(expected, "boundary perspective->ortho: %d, depthTest on->off: %d, depthWrite on->off: %d",
		kViewTestFrames, kViewTestFrames, kViewTestFrames);
	CHECK(log.find(expected) != std::string::npos, "session summary contains \"%s\"", expected);
	CHECK(log.find("[VIEW_FRAME] frame=0 draws=2 projections=[#1 #2] lastWorldDraw=1 hudDraws=1 hudOrtho=1") !=
		std::string::npos, "DEBUG log reports the first frame's world-to-HUD boundary");
}
