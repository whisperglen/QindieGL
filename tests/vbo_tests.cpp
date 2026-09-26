// ARB_vertex_buffer_object regression tests (task brief section 22).
//
// Every draw test renders a full-viewport quad and reads the centre pixel
// back through glReadPixels, so array and element offsets are verified where
// they matter: when QindieGL resolves them at draw time. Decoy data is placed
// wherever a wrong offset would be read, drawing either nothing (off-screen
// vertices) or a different colour.

#include <stdio.h>
#include <string.h>

#include "tests.h"
#include "gl_harness.h"

#define CHECK(condition, ...) do { \
		char checkMessage_[320]; \
		sprintf_s(checkMessage_, __VA_ARGS__); \
		xassert_str(!!(condition), checkMessage_, __func__, (unsigned)__LINE__, __FILE__); \
	} while (0)

namespace {

struct Vec3 { float x, y, z; };
struct Color4 { unsigned char r, g, b, a; };

// Triangle strip covering the viewport, and a degenerate strip outside it.
const Vec3 kQuad[4] = { { -1, -1, 0 }, { 1, -1, 0 }, { -1, 1, 0 }, { 1, 1, 0 } };
const Vec3 kOffscreen[4] = { { 3, 3, 0 }, { 3, 3, 0 }, { 3, 3, 0 }, { 3, 3, 0 } };
const GLushort kQuadTriangles[6] = { 0, 1, 2, 2, 1, 3 };

const Color4 kRed = { 220, 20, 20, 255 };
const Color4 kBlue = { 20, 40, 230, 255 };
const Color4 kMagenta = { 255, 0, 255, 255 };

void FillColors( Color4 *colors, Color4 color )
{
	for (int i = 0; i < 4; ++i) colors[i] = color;
}

void DrainErrors()
{
	for (int i = 0; i < 8 && gl.GetError() != GL_NO_ERROR; ++i) {}
}

void ResetState()
{
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	gl.BindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
	gl.DisableClientState(GL_VERTEX_ARRAY);
	gl.DisableClientState(GL_COLOR_ARRAY);
	gl.DisableClientState(GL_NORMAL_ARRAY);
	gl.DisableClientState(GL_TEXTURE_COORD_ARRAY);
	gl.DisableClientState(GL_SECONDARY_COLOR_ARRAY_EXT);
	gl.Disable(GL_TEXTURE_2D);
	gl.Disable(GL_LIGHTING);
	gl.Disable(GL_LIGHT0);
	gl.Disable(GL_COLOR_SUM_EXT);
	gl.Disable(GL_DEPTH_TEST);
	gl.Disable(GL_CULL_FACE);
	gl.Disable(GL_BLEND);
	gl.Disable(GL_ALPHA_TEST);
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
	gl.Color4ub(255, 255, 255, 255);
	gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	gl.Clear(GL_COLOR_BUFFER_BIT);
	DrainErrors();
}

bool Near( int actual, int expected, int tolerance )
{
	return actual >= expected - tolerance && actual <= expected + tolerance;
}

void ExpectCenter( const char *label, int r, int g, int b, int tolerance = 4 )
{
	const RGBA8 pixel = Harness_ReadCenter();
	CHECK(Near(pixel.r, r, tolerance) && Near(pixel.g, g, tolerance) && Near(pixel.b, b, tolerance),
		"%s: centre pixel (%d,%d,%d), expected (%d,%d,%d)", label, pixel.r, pixel.g, pixel.b, r, g, b);
}

void ExpectCenter( const char *label, Color4 color )
{
	ExpectCenter(label, color.r, color.g, color.b);
}

void ExpectNothingDrawn( const char *label )
{
	ExpectCenter(label, 0, 0, 0, 2);
}

void ExpectError( const char *label, GLenum expected )
{
	const GLenum error = gl.GetError();
	CHECK(error == expected, "%s: glGetError 0x%04X, expected 0x%04X", label, error, expected);
}

GLuint MakeBuffer( GLenum target, const void *data, size_t size, GLenum usage = GL_STATIC_DRAW_ARB )
{
	GLuint buffer = 0;
	gl.GenBuffersARB(1, &buffer);
	gl.BindBufferARB(target, buffer);
	gl.BufferDataARB(target, static_cast<GLsizeiptrARB>(size), data, usage);
	return buffer;
}

void DeleteBuffer( GLuint buffer )
{
	gl.DeleteBuffersARB(1, &buffer);
}

// Client colour array used by tests that exercise another array.
void UseClientColors( const Color4 *colors )
{
	gl.EnableClientState(GL_COLOR_ARRAY);
	gl.ColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
}

// 2x1 texture: left texel red, right texel green, point sampled.
GLuint MakeTwoTexelTexture()
{
	const unsigned char texels[8] = { 255, 0, 0, 255, 0, 255, 0, 255 };
	GLuint texture = 0;
	gl.GenTextures(1, &texture);
	gl.BindTexture(GL_TEXTURE_2D, texture);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
	return texture;
}

//---------------------------------------------------------------------------
// Client-side arrays (baseline without buffer objects)
//---------------------------------------------------------------------------

void TestClientDrawArrays()
{
	ResetState();
	Color4 colors[4];
	FillColors(colors, kRed);
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, kQuad);
	UseClientColors(colors);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	ExpectError("client glDrawArrays", GL_NO_ERROR);
	ExpectCenter("client glDrawArrays", kRed);
	Harness_Swap();
}

void TestClientDrawElements()
{
	ResetState();
	Color4 colors[4];
	FillColors(colors, kBlue);
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, kQuad);
	UseClientColors(colors);
	gl.DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, kQuadTriangles);
	ExpectError("client glDrawElements", GL_NO_ERROR);
	ExpectCenter("client glDrawElements", kBlue);
	Harness_Swap();
}

//---------------------------------------------------------------------------
// GL_ARRAY_BUFFER offsets
//---------------------------------------------------------------------------

void TestVertexBufferOffsetZero()
{
	ResetState();
	Color4 colors[4];
	FillColors(colors, kRed);
	const GLuint buffer = MakeBuffer(GL_ARRAY_BUFFER_ARB, kQuad, sizeof(kQuad));
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	// Colours are specified with no buffer bound: a client pointer.
	UseClientColors(colors);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	ExpectCenter("VBO vertex pointer offset 0 + client colours", kRed);
	Harness_Swap();
	DeleteBuffer(buffer);
}

void TestVertexBufferNonZeroOffset()
{
	ResetState();
	Color4 colors[4];
	FillColors(colors, kBlue);
	Vec3 data[8];
	memcpy(data, kOffscreen, sizeof(kOffscreen));
	memcpy(data + 4, kQuad, sizeof(kQuad));
	const GLuint buffer2 = MakeBuffer(GL_ARRAY_BUFFER_ARB, data, sizeof(data));
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, reinterpret_cast<const GLvoid *>(sizeof(kOffscreen)));
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	UseClientColors(colors);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	ExpectCenter("VBO vertex pointer non-zero offset", kBlue);
	// glDrawArrays(first=4) with offset 0 reaches the same vertices.
	ResetState();
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, buffer2);
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	Color4 colors8[8];
	FillColors(colors8, kMagenta);
	FillColors(colors8 + 4, kBlue);
	UseClientColors(colors8);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 4, 4);
	ExpectCenter("VBO offset 0 with glDrawArrays first=4", kBlue);
	Harness_Swap();
	DeleteBuffer(buffer2);
}

// Position, colour and texture coordinate interleaved in one buffer, starting
// after a block of decoy vertices.
void TestInterleavedArraysInOneBuffer()
{
	struct Vertex { float position[3]; Color4 color; float uv[2]; };
	static_assert(sizeof(Vertex) == 24, "interleaved vertex layout");
	ResetState();
	Vertex data[8];
	for (int i = 0; i < 4; ++i) {
		memcpy(data[i].position, &kOffscreen[i], sizeof(Vec3));
		data[i].color = kMagenta;
		data[i].uv[0] = 0.25f; data[i].uv[1] = 0.5f;
		memcpy(data[i + 4].position, &kQuad[i], sizeof(Vec3));
		data[i + 4].color = { 255, 128, 255, 255 };
		data[i + 4].uv[0] = 0.75f; data[i + 4].uv[1] = 0.5f;
	}
	const GLuint texture = MakeTwoTexelTexture();
	gl.Enable(GL_TEXTURE_2D);
	const GLuint buffer = MakeBuffer(GL_ARRAY_BUFFER_ARB, data, sizeof(data));
	const size_t base = 4 * sizeof(Vertex);
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.EnableClientState(GL_COLOR_ARRAY);
	gl.EnableClientState(GL_TEXTURE_COORD_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, sizeof(Vertex), reinterpret_cast<const GLvoid *>(base));
	gl.ColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), reinterpret_cast<const GLvoid *>(base + 12));
	gl.TexCoordPointer(2, GL_FLOAT, sizeof(Vertex), reinterpret_cast<const GLvoid *>(base + 16));
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	// Right (green) texel modulated by (255,128,255).
	ExpectCenter("interleaved position+colour+uv in one VBO", 0, 128, 0);
	Harness_Swap();
	DeleteBuffer(buffer);
	gl.DeleteTextures(1, &texture);
}

// Positions, two normal sets and texture coordinates as separate blocks of
// one buffer. Lighting makes a wrong normal offset visible.
void TestSeparateBlocksWithNormals()
{
	const Vec3 towardLight[4] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
	const Vec3 awayFromLight[4] = { { 0, 0, -1 }, { 0, 0, -1 }, { 0, 0, -1 }, { 0, 0, -1 } };
	const float uvs[8] = { 0.75f, 0.5f, 0.75f, 0.5f, 0.75f, 0.5f, 0.75f, 0.5f };
	unsigned char data[48 * 3 + sizeof(uvs)];
	memcpy(data, kQuad, 48);
	memcpy(data + 48, towardLight, 48);
	memcpy(data + 96, awayFromLight, 48);
	memcpy(data + 144, uvs, sizeof(uvs));

	RGBA8 lit = {}, unlit = {};
	for (int pass = 0; pass < 2; ++pass) {
		ResetState();
		const GLuint texture = MakeTwoTexelTexture();
		gl.Enable(GL_TEXTURE_2D);
		gl.Enable(GL_LIGHTING);
		gl.Enable(GL_LIGHT0);
		const GLuint buffer = MakeBuffer(GL_ARRAY_BUFFER_ARB, data, sizeof(data));
		gl.EnableClientState(GL_VERTEX_ARRAY);
		gl.EnableClientState(GL_NORMAL_ARRAY);
		gl.EnableClientState(GL_TEXTURE_COORD_ARRAY);
		gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
		gl.NormalPointer(GL_FLOAT, 0, reinterpret_cast<const GLvoid *>(pass == 0 ? 48 : 96));
		gl.TexCoordPointer(2, GL_FLOAT, 0, reinterpret_cast<const GLvoid *>(144));
		gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		(pass == 0 ? lit : unlit) = Harness_ReadCenter();
		Harness_Swap();
		DeleteBuffer(buffer);
		gl.DeleteTextures(1, &texture);
	}
	// GL defaults: 0.8 diffuse + 0.04 ambient toward the light, 0.04 away.
	CHECK(lit.g > 150 && lit.r < 40 && lit.b < 40,
		"VBO normals facing the light (offset 48) + uv (offset 144): got (%d,%d,%d), expected lit green",
		lit.r, lit.g, lit.b);
	CHECK(unlit.g < 80 && lit.g > unlit.g + 100,
		"VBO normals facing away (offset 96): got (%d,%d,%d), expected dark", unlit.r, unlit.g, unlit.b);
}

void TestSecondaryColorFromBuffer()
{
	if (!gl.SecondaryColorPointerEXT) {
		CHECK(false, "glSecondaryColorPointerEXT unavailable");
		return;
	}
	ResetState();
	const unsigned char colors[24] = {
		255, 0, 255, 255, 0, 255, 255, 0, 255, 255, 0, 255,	// decoy
		0, 200, 0, 0, 200, 0, 0, 200, 0, 0, 200, 0 };
	const GLuint positions = MakeBuffer(GL_ARRAY_BUFFER_ARB, kQuad, sizeof(kQuad));
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
	const GLuint secondary = MakeBuffer(GL_ARRAY_BUFFER_ARB, colors, sizeof(colors));
	gl.EnableClientState(GL_SECONDARY_COLOR_ARRAY_EXT);
	gl.SecondaryColorPointerEXT(3, GL_UNSIGNED_BYTE, 0, reinterpret_cast<const GLvoid *>(12));
	gl.Enable(GL_COLOR_SUM_EXT);
	gl.Color4ub(0, 0, 0, 255);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	ExpectCenter("secondary colour VBO offset 12 with COLOR_SUM", 0, 200, 0);
	Harness_Swap();
	DeleteBuffer(secondary);
	DeleteBuffer(positions);
}

//---------------------------------------------------------------------------
// GL_ELEMENT_ARRAY_BUFFER offsets
//---------------------------------------------------------------------------

// Vertices 0-3 are off-screen, 4-7 the visible quad. Element buffers start
// with decoy indices addressing the off-screen vertices.
GLuint MakeEightVertexBuffer()
{
	Vec3 data[8];
	memcpy(data, kOffscreen, sizeof(kOffscreen));
	memcpy(data + 4, kQuad, sizeof(kQuad));
	return MakeBuffer(GL_ARRAY_BUFFER_ARB, data, sizeof(data));
}

template<typename Index>
void DrawWithElementBufferOffset( GLenum type, const char *label )
{
	ResetState();
	Color4 colors[8];
	FillColors(colors, kMagenta);
	FillColors(colors + 4, kRed);
	const GLuint vertices = MakeEightVertexBuffer();
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	UseClientColors(colors);

	Index indices[12];
	for (int i = 0; i < 6; ++i) {
		indices[i] = static_cast<Index>(kQuadTriangles[i]);
		indices[i + 6] = static_cast<Index>(kQuadTriangles[i] + 4);
	}
	const GLuint elements = MakeBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB, indices, sizeof(indices));
	gl.DrawElements(GL_TRIANGLES, 6, type, reinterpret_cast<const GLvoid *>(6 * sizeof(Index)));
	ExpectError(label, GL_NO_ERROR);
	ExpectCenter(label, kRed);
	Harness_Swap();
	DeleteBuffer(elements);
	DeleteBuffer(vertices);
}

void TestElementBufferOffsets()
{
	DrawWithElementBufferOffset<GLubyte>(GL_UNSIGNED_BYTE, "element VBO GL_UNSIGNED_BYTE offset 6");
	DrawWithElementBufferOffset<GLushort>(GL_UNSIGNED_SHORT, "element VBO GL_UNSIGNED_SHORT offset 12");
	DrawWithElementBufferOffset<GLuint>(GL_UNSIGNED_INT, "element VBO GL_UNSIGNED_INT offset 24");
}

void TestDrawRangeElementsWithBuffers()
{
	if (!gl.DrawRangeElementsEXT) {
		CHECK(false, "glDrawRangeElementsEXT unavailable");
		return;
	}
	ResetState();
	Color4 colors[8];
	FillColors(colors, kMagenta);
	FillColors(colors + 4, kBlue);
	const GLuint vertices = MakeEightVertexBuffer();
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	UseClientColors(colors);
	GLushort indices[12];
	for (int i = 0; i < 6; ++i) {
		indices[i] = kQuadTriangles[i];
		indices[i + 6] = static_cast<GLushort>(kQuadTriangles[i] + 4);
	}
	const GLuint elements = MakeBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB, indices, sizeof(indices));
	gl.DrawRangeElementsEXT(GL_TRIANGLES, 4, 7, 6, GL_UNSIGNED_SHORT, reinterpret_cast<const GLvoid *>(12));
	ExpectCenter("glDrawRangeElementsEXT start=4 end=7 element offset 12", kBlue);
	Harness_Swap();
	DeleteBuffer(elements);
	DeleteBuffer(vertices);
}

//---------------------------------------------------------------------------
// Binding changes
//---------------------------------------------------------------------------

void TestUnbindArrayBufferKeepsCapturedBinding()
{
	ResetState();
	Color4 colors[4];
	FillColors(colors, kRed);
	const GLuint buffer = MakeBuffer(GL_ARRAY_BUFFER_ARB, kQuad, sizeof(kQuad));
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	UseClientColors(colors);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	ExpectCenter("array binding captured at glVertexPointer survives unbinding", kRed);
	Harness_Swap();

	// Respecified with no buffer bound: a client pointer, even after the
	// buffer is bound again.
	ResetState();
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, kOffscreen);
	UseClientColors(colors);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, buffer);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	ExpectNothingDrawn("pointer specified with buffer 0 stays a client pointer after rebinding");
	Harness_Swap();
	DeleteBuffer(buffer);
}

void TestUnbindElementBuffer()
{
	ResetState();
	Color4 colors[8];
	FillColors(colors, kMagenta);
	FillColors(colors + 4, kBlue);
	const GLuint vertices = MakeEightVertexBuffer();
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	UseClientColors(colors);

	const GLushort visible[6] = { 4, 5, 6, 6, 5, 7 };
	const GLuint elements = MakeBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB, visible, sizeof(visible));
	gl.DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
	ExpectCenter("element VBO offset 0", kBlue);
	Harness_Swap();

	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.BindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
	gl.DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, kQuadTriangles);
	ExpectNothingDrawn("unbound element buffer: indices are client memory (off-screen vertices 0-3)");
	Harness_Swap();

	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, visible);
	ExpectCenter("unbound element buffer: client indices 4-7", kBlue);
	Harness_Swap();
	DeleteBuffer(elements);
	DeleteBuffer(vertices);
}

void TestDeleteBoundBuffer()
{
	ResetState();
	const GLuint buffer = MakeBuffer(GL_ARRAY_BUFFER_ARB, kQuad, sizeof(kQuad));
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
	gl.DeleteBuffersARB(1, &buffer);
	ExpectError("glDeleteBuffersARB of the bound buffer", GL_NO_ERROR);
	CHECK(!gl.IsBufferARB(buffer), "deleted buffer is no longer a buffer object");
	// The ARRAY_BUFFER binding reverted to zero.
	gl.BufferDataARB(GL_ARRAY_BUFFER_ARB, 16, nullptr, GL_STATIC_DRAW_ARB);
	ExpectError("glBufferDataARB after deleting the bound buffer", GL_INVALID_OPERATION);
	// The vertex array still names the deleted store. The specification
	// allows program termination here; QindieGL must skip the draw safely.
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	DrainErrors();
	ExpectNothingDrawn("draw from a deleted vertex buffer is skipped");
	Harness_Swap();
}

//---------------------------------------------------------------------------
// Data store operations
//---------------------------------------------------------------------------

void TestMapUnmap()
{
	ResetState();
	Color4 colors[4];
	FillColors(colors, kBlue);
	const GLuint buffer = MakeBuffer(GL_ARRAY_BUFFER_ARB, nullptr, sizeof(kQuad), GL_DYNAMIC_DRAW_ARB);

	gl.MapBufferARB(GL_ARRAY_BUFFER_ARB, 0x1234);
	ExpectError("glMapBufferARB invalid access", GL_INVALID_ENUM);

	void *mapped = gl.MapBufferARB(GL_ARRAY_BUFFER_ARB, GL_WRITE_ONLY_ARB);
	ExpectError("glMapBufferARB WRITE_ONLY", GL_NO_ERROR);
	CHECK(mapped != nullptr, "glMapBufferARB returns the data store");
	GLint isMapped = GL_FALSE;
	gl.GetBufferParameterivARB(GL_ARRAY_BUFFER_ARB, GL_BUFFER_MAPPED_ARB, &isMapped);
	CHECK(isMapped == GL_TRUE, "BUFFER_MAPPED is TRUE while mapped");
	void *mapPointer = nullptr;
	gl.GetBufferPointervARB(GL_ARRAY_BUFFER_ARB, GL_BUFFER_MAP_POINTER_ARB, &mapPointer);
	CHECK(mapPointer == mapped, "BUFFER_MAP_POINTER matches the mapped pointer");

	CHECK(gl.MapBufferARB(GL_ARRAY_BUFFER_ARB, GL_WRITE_ONLY_ARB) == nullptr, "second map returns NULL");
	ExpectError("glMapBufferARB on a mapped buffer", GL_INVALID_OPERATION);
	gl.BufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, 4, kQuad);
	ExpectError("glBufferSubDataARB on a mapped buffer", GL_INVALID_OPERATION);
	unsigned char readback[4];
	gl.GetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, 4, readback);
	ExpectError("glGetBufferSubDataARB on a mapped buffer", GL_INVALID_OPERATION);

	if (mapped) memcpy(mapped, kQuad, sizeof(kQuad));
	CHECK(gl.UnmapBufferARB(GL_ARRAY_BUFFER_ARB) == GL_TRUE, "glUnmapBufferARB returns TRUE");
	gl.GetBufferParameterivARB(GL_ARRAY_BUFFER_ARB, GL_BUFFER_MAPPED_ARB, &isMapped);
	CHECK(isMapped == GL_FALSE, "BUFFER_MAPPED is FALSE after unmapping");
	CHECK(gl.UnmapBufferARB(GL_ARRAY_BUFFER_ARB) == GL_FALSE, "unmapping twice returns FALSE");
	ExpectError("glUnmapBufferARB on an unmapped buffer", GL_INVALID_OPERATION);

	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	UseClientColors(colors);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	ExpectCenter("draw from data written through glMapBufferARB", kBlue);
	Harness_Swap();

	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, buffer);
	const void *readOnly = gl.MapBufferARB(GL_ARRAY_BUFFER_ARB, GL_READ_ONLY_ARB);
	CHECK(readOnly && !memcmp(readOnly, kQuad, sizeof(kQuad)), "READ_ONLY map exposes the stored data");
	gl.UnmapBufferARB(GL_ARRAY_BUFFER_ARB);
	DeleteBuffer(buffer);
}

void TestBufferData()
{
	ResetState();
	gl.BufferDataARB(GL_ARRAY_BUFFER_ARB, 16, nullptr, GL_STATIC_DRAW_ARB);
	ExpectError("glBufferDataARB with buffer 0 bound", GL_INVALID_OPERATION);

	GLuint buffer = 0;
	gl.GenBuffersARB(1, &buffer);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, buffer);
	gl.BufferDataARB(GL_ARRAY_BUFFER_ARB, -1, nullptr, GL_STATIC_DRAW_ARB);
	ExpectError("glBufferDataARB negative size", GL_INVALID_VALUE);
	gl.BufferDataARB(GL_ARRAY_BUFFER_ARB, 16, nullptr, 0x1234);
	ExpectError("glBufferDataARB invalid usage", GL_INVALID_ENUM);

	// Positions followed by colours; the colour block is replaced below.
	struct Layout { Vec3 positions[4]; Color4 colors[4]; } data;
	memcpy(data.positions, kQuad, sizeof(kQuad));
	FillColors(data.colors, kRed);
	gl.BufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof(data), &data, GL_STREAM_DRAW_ARB);
	ExpectError("glBufferDataARB", GL_NO_ERROR);
	GLint value = 0;
	gl.GetBufferParameterivARB(GL_ARRAY_BUFFER_ARB, GL_BUFFER_SIZE_ARB, &value);
	CHECK(value == (GLint)sizeof(data), "BUFFER_SIZE %d, expected %d", value, (int)sizeof(data));
	gl.GetBufferParameterivARB(GL_ARRAY_BUFFER_ARB, GL_BUFFER_USAGE_ARB, &value);
	CHECK(value == GL_STREAM_DRAW_ARB, "BUFFER_USAGE 0x%X, expected STREAM_DRAW", value);
	gl.GetBufferParameterivARB(GL_ARRAY_BUFFER_ARB, 0x1234, &value);
	ExpectError("glGetBufferParameterivARB invalid pname", GL_INVALID_ENUM);

	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.EnableClientState(GL_COLOR_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
	gl.ColorPointer(4, GL_UNSIGNED_BYTE, 0, reinterpret_cast<const GLvoid *>(sizeof(data.positions)));
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	ExpectCenter("VBO positions + VBO colours", kRed);
	Harness_Swap();

	// Replacing the store takes effect for arrays specified earlier.
	FillColors(data.colors, kBlue);
	gl.BufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof(data), &data, GL_STATIC_DRAW_ARB);
	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	ExpectCenter("draw after glBufferDataARB replaced the store", kBlue);
	Harness_Swap();
	DeleteBuffer(buffer);
}

// ARB_vertex_buffer_object: BufferData deletes the old store and resets
// BUFFER_MAPPED to FALSE, so it implicitly unmaps instead of failing.
void TestBufferDataWhileMapped()
{
	ResetState();
	const GLuint buffer = MakeBuffer(GL_ARRAY_BUFFER_ARB, kQuad, sizeof(kQuad));
	gl.MapBufferARB(GL_ARRAY_BUFFER_ARB, GL_WRITE_ONLY_ARB);
	gl.BufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof(kOffscreen), kOffscreen, GL_STATIC_DRAW_ARB);
	ExpectError("glBufferDataARB on a mapped buffer", GL_NO_ERROR);
	GLint isMapped = GL_TRUE;
	gl.GetBufferParameterivARB(GL_ARRAY_BUFFER_ARB, GL_BUFFER_MAPPED_ARB, &isMapped);
	CHECK(isMapped == GL_FALSE, "glBufferDataARB implicitly unmaps the buffer");
	Vec3 readback[4] = {};
	gl.GetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, sizeof(readback), readback);
	CHECK(!memcmp(readback, kOffscreen, sizeof(readback)), "new store holds the new data");
	gl.UnmapBufferARB(GL_ARRAY_BUFFER_ARB);
	DrainErrors();
	DeleteBuffer(buffer);
}

void TestBufferSubData()
{
	ResetState();
	struct Layout { Vec3 positions[4]; Color4 colors[4]; } data;
	memcpy(data.positions, kQuad, sizeof(kQuad));
	FillColors(data.colors, kRed);
	const GLuint buffer = MakeBuffer(GL_ARRAY_BUFFER_ARB, &data, sizeof(data));
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.EnableClientState(GL_COLOR_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
	gl.ColorPointer(4, GL_UNSIGNED_BYTE, 0, reinterpret_cast<const GLvoid *>(sizeof(data.positions)));

	Color4 blue[4];
	FillColors(blue, kBlue);
	gl.BufferSubDataARB(GL_ARRAY_BUFFER_ARB, sizeof(data.positions), sizeof(blue), blue);
	ExpectError("glBufferSubDataARB", GL_NO_ERROR);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	ExpectCenter("draw after glBufferSubDataARB replaced the colour block", kBlue);
	Harness_Swap();

	Color4 readback[4] = {};
	gl.GetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, sizeof(data.positions), sizeof(readback), readback);
	CHECK(!memcmp(readback, blue, sizeof(blue)), "glGetBufferSubDataARB reads the updated block");
	Vec3 positions[4] = {};
	gl.GetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, sizeof(positions), positions);
	CHECK(!memcmp(positions, kQuad, sizeof(positions)), "glBufferSubDataARB left other bytes intact");

	gl.BufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, 0, nullptr);
	ExpectError("glBufferSubDataARB size 0", GL_NO_ERROR);
	DeleteBuffer(buffer);
}

void TestGetBufferSubData()
{
	ResetState();
	unsigned char temp[4] = {};
	gl.GetBufferSubDataARB(GL_TEXTURE_2D, 0, 4, temp);
	ExpectError("glGetBufferSubDataARB invalid target", GL_INVALID_ENUM);
	gl.GetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, 4, temp);
	ExpectError("glGetBufferSubDataARB with buffer 0 bound", GL_INVALID_OPERATION);

	const unsigned char source[8] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };
	const GLuint buffer = MakeBuffer(GL_ARRAY_BUFFER_ARB, source, sizeof(source));
	gl.GetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 6, 4, temp);
	ExpectError("glGetBufferSubDataARB past the end", GL_INVALID_VALUE);

	unsigned char sentinel = 0xAA;
	gl.GetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, 0, &sentinel);
	ExpectError("glGetBufferSubDataARB size 0", GL_NO_ERROR);
	CHECK(sentinel == 0xAA, "size 0 readback leaves the destination untouched");

	unsigned char output[4] = {};
	gl.GetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 2, 4, output);
	ExpectError("glGetBufferSubDataARB offset 2", GL_NO_ERROR);
	CHECK(!memcmp(output, source + 2, 4), "glGetBufferSubDataARB offset 2 returns bytes 2..5");
	DeleteBuffer(buffer);
}

//---------------------------------------------------------------------------
// Invalid use
//---------------------------------------------------------------------------

void TestOutOfRangeOffsets()
{
	ResetState();
	const unsigned char source[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	const unsigned char patch[4] = { 9, 9, 9, 9 };
	const GLuint buffer = MakeBuffer(GL_ARRAY_BUFFER_ARB, source, sizeof(source));
	gl.BufferSubDataARB(GL_ARRAY_BUFFER_ARB, 6, 4, patch);
	ExpectError("glBufferSubDataARB offset+size > BUFFER_SIZE", GL_INVALID_VALUE);
	gl.BufferSubDataARB(GL_ARRAY_BUFFER_ARB, -1, 4, patch);
	ExpectError("glBufferSubDataARB negative offset", GL_INVALID_VALUE);
	gl.BufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, -4, patch);
	ExpectError("glBufferSubDataARB negative size", GL_INVALID_VALUE);
	unsigned char readback[8] = {};
	gl.GetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, sizeof(readback), readback);
	CHECK(!memcmp(readback, source, sizeof(source)), "rejected updates left the store unchanged");
	DeleteBuffer(buffer);
}

// Out-of-range sourcing is undefined (the specification permits program
// termination). QindieGL validates ranges and must skip such draws.
void TestOutOfRangeDraws()
{
	Color4 colors[4];
	FillColors(colors, kRed);

	ResetState();
	const GLuint buffer = MakeBuffer(GL_ARRAY_BUFFER_ARB, kQuad, sizeof(kQuad));
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, reinterpret_cast<const GLvoid *>(24));
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	UseClientColors(colors);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	DrainErrors();
	ExpectNothingDrawn("vertices extend past the end of the VBO");
	Harness_Swap();

	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, buffer);
	gl.VertexPointer(3, GL_FLOAT, 0, reinterpret_cast<const GLvoid *>(4096));
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	DrainErrors();
	ExpectNothingDrawn("vertex offset beyond the VBO");
	Harness_Swap();

	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, buffer);
	gl.VertexPointer(3, GL_FLOAT, 0, nullptr);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	Color4 colors16[16];
	for (int i = 0; i < 16; ++i) colors16[i] = kRed;
	UseClientColors(colors16);
	const GLushort beyond[6] = { 0, 1, 2, 2, 1, 10 };
	const GLuint elements = MakeBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB, beyond, sizeof(beyond));
	gl.DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
	DrainErrors();
	ExpectNothingDrawn("index 10 addresses a vertex past the 4-vertex VBO");
	Harness_Swap();

	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, reinterpret_cast<const GLvoid *>(8));
	DrainErrors();
	ExpectNothingDrawn("indices extend past the end of the element VBO");
	Harness_Swap();

	DeleteBuffer(elements);
	DeleteBuffer(buffer);
}

void TestInvalidTargets()
{
	ResetState();
	GLuint buffer = 0;
	gl.GenBuffersARB(1, &buffer);
	gl.BindBufferARB(GL_TEXTURE_2D, buffer);
	ExpectError("glBindBufferARB invalid target", GL_INVALID_ENUM);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, buffer);
	gl.BufferDataARB(GL_TEXTURE_2D, 16, nullptr, GL_STATIC_DRAW_ARB);
	ExpectError("glBufferDataARB invalid target", GL_INVALID_ENUM);
	gl.BufferSubDataARB(GL_TEXTURE_2D, 0, 0, nullptr);
	ExpectError("glBufferSubDataARB invalid target", GL_INVALID_ENUM);
	CHECK(gl.MapBufferARB(GL_TEXTURE_2D, GL_WRITE_ONLY_ARB) == nullptr, "glMapBufferARB invalid target returns NULL");
	ExpectError("glMapBufferARB invalid target", GL_INVALID_ENUM);
	gl.UnmapBufferARB(GL_TEXTURE_2D);
	ExpectError("glUnmapBufferARB invalid target", GL_INVALID_ENUM);
	DeleteBuffer(buffer);
}

} // namespace

// Entry points must be exposed exactly when the extension is advertised.
void do_extension_availability_tests( bool expectBufferObjects )
{
	const char *extensions = reinterpret_cast<const char *>(gl.GetString(GL_EXTENSIONS));
	const bool advertised = extensions && strstr(extensions, "GL_ARB_vertex_buffer_object ") != nullptr;
	CHECK(advertised == expectBufferObjects, "GL_ARB_vertex_buffer_object advertised=%d, expected %d",
		advertised, expectBufferObjects);
	const char *names[] = { "glBindBufferARB", "glGenBuffersARB", "glDeleteBuffersARB", "glIsBufferARB",
		"glBufferDataARB", "glBufferSubDataARB", "glGetBufferSubDataARB", "glMapBufferARB",
		"glUnmapBufferARB", "glGetBufferParameterivARB", "glGetBufferPointervARB",
		"glBindBuffer", "glBufferData", "glMapBuffer", "glUnmapBuffer" };
	for (const char *name : names) {
		const bool available = Harness_GetProcAddress(name) != nullptr;
		CHECK(available == expectBufferObjects, "wglGetProcAddress(%s) %s, expected %s", name,
			available ? "non-NULL" : "NULL", expectBufferObjects ? "non-NULL" : "NULL");
	}
	CHECK(Harness_GetProcAddress("glQindieGLNonexistentProc") == nullptr,
		"wglGetProcAddress of an unknown procedure is NULL");
}

void do_vbo_tests()
{
	if (!gl.BindBufferARB || !gl.GenBuffersARB || !gl.DeleteBuffersARB || !gl.IsBufferARB ||
		!gl.BufferDataARB || !gl.BufferSubDataARB || !gl.GetBufferSubDataARB || !gl.MapBufferARB ||
		!gl.UnmapBufferARB || !gl.GetBufferParameterivARB || !gl.GetBufferPointervARB) {
		CHECK(false, "ARB_vertex_buffer_object entry points unavailable");
		return;
	}
	TestClientDrawArrays();
	TestClientDrawElements();
	TestVertexBufferOffsetZero();
	TestVertexBufferNonZeroOffset();
	TestInterleavedArraysInOneBuffer();
	TestSeparateBlocksWithNormals();
	TestSecondaryColorFromBuffer();
	TestElementBufferOffsets();
	TestDrawRangeElementsWithBuffers();
	TestUnbindArrayBufferKeepsCapturedBinding();
	TestUnbindElementBuffer();
	TestDeleteBoundBuffer();
	TestMapUnmap();
	TestBufferData();
	TestBufferDataWhileMapped();
	TestBufferSubData();
	TestGetBufferSubData();
	TestOutOfRangeOffsets();
	TestOutOfRangeDraws();
	TestInvalidTargets();
}

// Ported from the former buffer_multitex.cpp, which inspected internal state
// and could not link: DS2's glMultiTexCoord4sdARB spelling through the API.
void do_multitexture_tests()
{
	if (!gl.MultiTexCoord4sdARB || !gl.ActiveTextureARB) {
		CHECK(false, "glMultiTexCoord4sdARB/glActiveTextureARB unavailable");
		return;
	}
	gl.MultiTexCoord4sdARB(GL_TEXTURE_2D, 1, 2, 3, 4);
	ExpectError("glMultiTexCoord4sdARB invalid target", GL_INVALID_ENUM);
	gl.MultiTexCoord4sdARB(GL_TEXTURE1_ARB, 1, -2, 3, -4);
	ExpectError("glMultiTexCoord4sdARB", GL_NO_ERROR);
	gl.ActiveTextureARB(GL_TEXTURE1_ARB);
	float coords[4] = {};
	gl.GetFloatv(GL_CURRENT_TEXTURE_COORDS, coords);
	gl.ActiveTextureARB(GL_TEXTURE0_ARB);
	CHECK(coords[0] == 1.0f && coords[1] == -2.0f && coords[2] == 3.0f && coords[3] == -4.0f,
		"glMultiTexCoord4sdARB unit 1: (%g,%g,%g,%g), expected (1,-2,3,-4)",
		coords[0], coords[1], coords[2], coords[3]);
}
