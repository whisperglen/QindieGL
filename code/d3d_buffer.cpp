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
#include <cstring>
#include <new>
#include <unordered_map>

#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_utils.hpp"
#include "d3d_buffer.hpp"

static std::unordered_map<GLuint, D3DBufferObject> g_bufferObjects;
static GLuint g_arrayBufferBinding = 0;
static GLuint g_elementArrayBufferBinding = 0;
static GLuint g_nextBufferId = 1;

static bool D3DBuffer_IsValidTarget( GLenum target )
{
	return target == GL_ARRAY_BUFFER_ARB || target == GL_ELEMENT_ARRAY_BUFFER_ARB;
}

static bool D3DBuffer_IsValidUsage( GLenum usage )
{
	switch (usage) {
	case GL_STREAM_DRAW_ARB:
	case GL_STREAM_READ_ARB:
	case GL_STREAM_COPY_ARB:
	case GL_STATIC_DRAW_ARB:
	case GL_STATIC_READ_ARB:
	case GL_STATIC_COPY_ARB:
	case GL_DYNAMIC_DRAW_ARB:
	case GL_DYNAMIC_READ_ARB:
	case GL_DYNAMIC_COPY_ARB:
		return true;
	default:
		return false;
	}
}

static D3DBufferObject *D3DBuffer_GetBoundObject( GLenum target )
{
	if (!D3DBuffer_IsValidTarget(target)) {
		QGL_SET_ERROR(E_INVALID_ENUM);
		return nullptr;
	}

	const GLuint binding = D3DBuffer_GetBinding(target);
	if (!binding) {
		QGL_SET_ERROR(E_INVALID_OPERATION);
		return nullptr;
	}

	D3DBufferObject *bufferObject = D3DBuffer_GetObject(binding, false);
	if (!bufferObject) {
		QGL_SET_ERROR(E_INVALID_OPERATION);
		return nullptr;
	}

	return bufferObject;
}

static bool D3DBuffer_ValidateRange( const D3DBufferObject *bufferObject,
	GLintptrARB offset, GLsizeiptrARB size )
{
	if (offset < 0 || size < 0) {
		QGL_SET_ERROR(E_INVALIDARG);
		return false;
	}

	const size_t byteOffset = static_cast<size_t>(offset);
	const size_t byteCount = static_cast<size_t>(size);
	const size_t bufferSize = static_cast<size_t>(bufferObject->size);
	if (byteOffset > bufferSize || byteCount > bufferSize - byteOffset) {
		QGL_SET_ERROR(E_INVALIDARG);
		return false;
	}

	return true;
}

void D3DBuffer_Bind( GLenum target, GLuint buffer )
{
	if (!D3DBuffer_IsValidTarget(target)) {
		QGL_SET_ERROR(E_INVALID_ENUM);
		return;
	}

	// Do not change the binding if creating the object fails.
	if (buffer != 0 && !D3DBuffer_GetObject(buffer, true)) {
		QGL_SET_ERROR(E_OUTOFMEMORY);
		return;
	}

	if (target == GL_ARRAY_BUFFER_ARB)
		g_arrayBufferBinding = buffer;
	else
		g_elementArrayBufferBinding = buffer;

	QGL_SET_ERROR(S_OK);
}

GLuint D3DBuffer_GetBinding( GLenum target )
{
	switch (target) {
		case GL_ARRAY_BUFFER_ARB:
			return g_arrayBufferBinding;
		case GL_ELEMENT_ARRAY_BUFFER_ARB:
			return g_elementArrayBufferBinding;
		default:
			QGL_SET_ERROR(E_INVALID_ENUM);
			return 0;
	}
}

D3DBufferObject *D3DBuffer_GetObject( GLuint buffer, bool create )
{
	if (!buffer) return nullptr;

	auto it = g_bufferObjects.find(buffer);
	if (it != g_bufferObjects.end()) return &it->second;
	if (!create) return nullptr;

	D3DBufferObject object = {};
	object.name = buffer;
	object.size = 0;
	object.usage = GL_STATIC_DRAW_ARB;
	object.storage = nullptr;
	object.mapped = false;
	object.mapAccess = GL_WRITE_ONLY_ARB;

	std::pair<std::unordered_map<GLuint, D3DBufferObject>::iterator, bool> result;
	try {
		result = g_bufferObjects.emplace(buffer, object);
	}
	catch (const std::bad_alloc &) {
		return nullptr;
	}
	if (!result.second) return &result.first->second;
	QGL_DiagnosticsRecordVBOCreated();

	return &result.first->second;
}

const GLubyte *D3DBuffer_ResolvePointer( GLuint buffer, const GLvoid *pointer, size_t requiredBytes )
{
	if (!buffer)
		return static_cast<const GLubyte *>(pointer);

	D3DBufferObject *bufferObject = D3DBuffer_GetObject(buffer, false);
	const size_t offset = reinterpret_cast<size_t>(pointer);
	if (!bufferObject || !bufferObject->storage || bufferObject->size < 0) {
		QGL_SET_ERROR(E_INVALID_OPERATION);
		QGL_DiagnosticsRecordEvent(true, "VBO_RESOLVE",
			"buffer=%u offset=%u unavailable", buffer, static_cast<unsigned>(offset));
		return nullptr;
	}

	const size_t bufferSize = static_cast<size_t>(bufferObject->size);
	if (offset > bufferSize || requiredBytes > bufferSize - offset) {
		QGL_SET_ERROR(E_INVALID_OPERATION);
		QGL_DiagnosticsRecordEvent(true, "VBO_RESOLVE",
			"buffer=%u offset=%u required=%u size=%u out of range", buffer,
			static_cast<unsigned>(offset), static_cast<unsigned>(requiredBytes),
			static_cast<unsigned>(bufferSize));
		return nullptr;
	}

	return static_cast<const GLubyte *>(bufferObject->storage) + offset;
}

void D3DBuffer_Cleanup()
{
	for (auto &entry : g_bufferObjects) {
		D3DBufferObject &bufferObject = entry.second;
		if (bufferObject.storage) {
			QGL_DiagnosticsRecordVBOBytes(-static_cast<int64_t>(bufferObject.size));
			UTIL_Free(bufferObject.storage);
			bufferObject.storage = nullptr;
		}
	}
	g_bufferObjects.clear();
	g_arrayBufferBinding = 0;
	g_elementArrayBufferBinding = 0;
	g_nextBufferId = 1;
}

OPENGL_API void WINAPI glBindBuffer( GLenum target, GLuint buffer )
{
	D3DBuffer_Bind( target, buffer );
}

OPENGL_API void WINAPI glBindBufferARB( GLenum target, GLuint buffer )
{
	glBindBuffer( target, buffer );
}

OPENGL_API void WINAPI glGenBuffers( GLsizei n, GLuint *buffers )
{
	if (n < 0 || (n > 0 && !buffers)) {
		QGL_SET_ERROR(E_INVALIDARG);
		return;
	}
	if (n == 0) {
		QGL_SET_ERROR(S_OK);
		return;
	}

	for (GLsizei i = 0; i < n; ++i) {
		GLuint id = 0;
		do {
			id = g_nextBufferId++;
			if (!g_nextBufferId) g_nextBufferId = 1;
		} while (!id || D3DBuffer_GetObject(id, false));
		if (!D3DBuffer_GetObject(id, true)) {
			QGL_SET_ERROR(E_OUTOFMEMORY);
			return;
		}
		buffers[i] = id;
	}

	QGL_SET_ERROR(S_OK);
}

OPENGL_API void WINAPI glGenBuffersARB( GLsizei n, GLuint *buffers )
{
	glGenBuffers(n, buffers);
}

OPENGL_API GLvoid* WINAPI glMapBuffer( GLenum target, GLenum access )
{
	D3DBufferObject *bufferObject = D3DBuffer_GetBoundObject(target);
	if (!bufferObject) return nullptr;

	switch (access) {
		case GL_READ_ONLY_ARB:
		case GL_WRITE_ONLY_ARB:
		case GL_READ_WRITE_ARB:
			break;
		default:
			QGL_SET_ERROR(E_INVALID_ENUM);
			return nullptr;
	}

	if (bufferObject->mapped) {
		QGL_SET_ERROR(E_INVALID_OPERATION);
		return nullptr;
	}

	bufferObject->mapped = true;
	bufferObject->mapAccess = access;
	// TODO: map/unmap D3D buffer when hardware VBOs are implemented
	QGL_SET_ERROR(S_OK);
	return bufferObject->storage;
}

OPENGL_API GLvoid* WINAPI glMapBufferARB( GLenum target, GLenum access )
{
	return glMapBuffer(target, access);
}

OPENGL_API GLboolean WINAPI glUnmapBuffer( GLenum target )
{
	D3DBufferObject *bufferObject = D3DBuffer_GetBoundObject(target);
	if (!bufferObject) return GL_FALSE;
	if (!bufferObject->mapped) {
		QGL_SET_ERROR(E_INVALID_OPERATION);
		return GL_FALSE;
	}

	bufferObject->mapped = false;
	QGL_SET_ERROR(S_OK);
	return GL_TRUE;
}

OPENGL_API GLboolean WINAPI glUnmapBufferARB( GLenum target )
{
	return glUnmapBuffer(target);
}

OPENGL_API void WINAPI glDeleteBuffers( GLsizei n, const GLuint *buffers )
{
	if (n < 0 || (n > 0 && !buffers)) {
		QGL_SET_ERROR(E_INVALIDARG);
		return;
	}
	if (n == 0) {
		QGL_SET_ERROR(S_OK);
		return;
	}

	for (GLsizei i = 0; i < n; ++i) {
		GLuint id = buffers[i];
		if (!id) {
			continue;
		}

		auto it = g_bufferObjects.find(id);
		if (it == g_bufferObjects.end()) {
			continue;
		}

		if (g_arrayBufferBinding == id) {
			g_arrayBufferBinding = 0;
		}
		if (g_elementArrayBufferBinding == id) {
			g_elementArrayBufferBinding = 0;
		}

		if (it->second.storage) {
			QGL_DiagnosticsRecordVBOBytes(-static_cast<int64_t>(it->second.size));
			UTIL_Free( it->second.storage );
			it->second.storage = nullptr;
		}

		// TODO: release D3D buffer when D3D-backed VBOs are added

		g_bufferObjects.erase(it);
	}

	QGL_SET_ERROR(S_OK);
}

OPENGL_API void WINAPI glDeleteBuffersARB( GLsizei n, const GLuint *buffers )
{
	glDeleteBuffers(n, buffers);
}

OPENGL_API GLboolean WINAPI glIsBuffer( GLuint buffer )
{
	if (!buffer) {
		QGL_SET_ERROR(S_OK);
		return GL_FALSE;
	}

	const GLboolean result = D3DBuffer_GetObject(buffer, false) ? GL_TRUE : GL_FALSE;
	QGL_SET_ERROR(S_OK);
	return result;
}

OPENGL_API GLboolean WINAPI glIsBufferARB( GLuint buffer )
{
	return glIsBuffer(buffer);
}

OPENGL_API void WINAPI glBufferData( GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage )
{
	if (size < 0) {
		QGL_SET_ERROR(E_INVALIDARG);
		return;
	}
	if (!D3DBuffer_IsValidUsage(usage)) {
		QGL_SET_ERROR(E_INVALID_ENUM);
		return;
	}

	D3DBufferObject *bufferObject = D3DBuffer_GetBoundObject(target);
	if (!bufferObject) return;
	// ARB_vertex_buffer_object: BufferData deletes the existing store and
	// resets BUFFER_MAPPED to FALSE, implicitly unmapping a mapped buffer.

	void *newStorage = nullptr;
	if (size > 0) {
		if (size > std::numeric_limits<int>::max()) {
			QGL_SET_ERROR(E_OUTOFMEMORY);
			return;
		}
		newStorage = UTIL_Alloc(static_cast<int>(size));
		if (!newStorage) {
			QGL_SET_ERROR(E_OUTOFMEMORY);
			return;
		}
		if (data) {
			memcpy(newStorage, data, static_cast<size_t>(size));
		}
	}

	if (bufferObject->storage) {
		QGL_DiagnosticsRecordVBOBytes(-static_cast<int64_t>(bufferObject->size));
		UTIL_Free(bufferObject->storage);
	}
	bufferObject->storage = newStorage;
	bufferObject->size = size;
	bufferObject->usage = usage;
	bufferObject->mapped = false;
	if (newStorage)
		QGL_DiagnosticsRecordVBOBytes(static_cast<int64_t>(size));

	QGL_SET_ERROR(S_OK);
}

OPENGL_API void WINAPI glBufferDataARB( GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage )
{
	glBufferData(target, size, data, usage);
}

OPENGL_API void WINAPI glBufferSubData( GLenum target, GLintptrARB offset,
	GLsizeiptrARB size, const GLvoid *data )
{
	D3DBufferObject *bufferObject = D3DBuffer_GetBoundObject(target);
	if (!bufferObject) return;
	if (bufferObject->mapped) {
		QGL_SET_ERROR(E_INVALID_OPERATION);
		return;
	}
	if (!D3DBuffer_ValidateRange(bufferObject, offset, size)) return;
	if (size > 0 && (!data || !bufferObject->storage)) {
		QGL_SET_ERROR(data ? E_INVALID_OPERATION : E_INVALIDARG);
		return;
	}

	if (size > 0) {
		auto *destination = static_cast<unsigned char *>(bufferObject->storage);
		memcpy(destination + static_cast<size_t>(offset), data, static_cast<size_t>(size));
	}

	QGL_SET_ERROR(S_OK);
}

OPENGL_API void WINAPI glBufferSubDataARB( GLenum target, GLintptrARB offset,
	GLsizeiptrARB size, const GLvoid *data )
{
	glBufferSubData(target, offset, size, data);
}

OPENGL_API void WINAPI glGetBufferSubData( GLenum target, GLintptrARB offset,
	GLsizeiptrARB size, GLvoid *data )
{
	D3DBufferObject *bufferObject = D3DBuffer_GetBoundObject(target);
	if (!bufferObject) return;
	if (bufferObject->mapped) {
		QGL_SET_ERROR(E_INVALID_OPERATION);
		return;
	}
	if (!D3DBuffer_ValidateRange(bufferObject, offset, size)) return;
	if (size > 0 && (!data || !bufferObject->storage)) {
		QGL_SET_ERROR(data ? E_INVALID_OPERATION : E_INVALIDARG);
		return;
	}

	if (size > 0) {
		auto *source = static_cast<const unsigned char *>(bufferObject->storage);
		memcpy( data, source + static_cast<size_t>(offset), static_cast<size_t>(size) );
	}

	QGL_SET_ERROR(S_OK);
}

OPENGL_API void WINAPI glGetBufferSubDataARB( GLenum target, GLintptrARB offset,
	GLsizeiptrARB size, GLvoid *data )
{
	glGetBufferSubData(target, offset, size, data);
}

OPENGL_API void WINAPI glGetBufferParameteriv( GLenum target, GLenum pname, GLint *params )
{
	D3DBufferObject *bufferObject = D3DBuffer_GetBoundObject(target);
	if (!bufferObject) return;
	if (!params) {
		QGL_SET_ERROR(E_INVALIDARG);
		return;
	}

	switch (pname) {
	case GL_BUFFER_SIZE_ARB:
		*params = static_cast<GLint>(bufferObject->size);
		break;
	case GL_BUFFER_USAGE_ARB:
		*params = static_cast<GLint>(bufferObject->usage);
		break;
	case GL_BUFFER_ACCESS_ARB:
		*params = static_cast<GLint>(bufferObject->mapAccess);
		break;
	case GL_BUFFER_MAPPED_ARB:
		*params = bufferObject->mapped ? GL_TRUE : GL_FALSE;
		break;
	default:
		QGL_SET_ERROR(E_INVALID_ENUM);
		return;
	}

	QGL_SET_ERROR(S_OK);
}

OPENGL_API void WINAPI glGetBufferParameterivARB( GLenum target, GLenum pname, GLint *params )
{
	glGetBufferParameteriv(target, pname, params);
}

OPENGL_API void WINAPI glGetBufferPointerv( GLenum target, GLenum pname, GLvoid **params )
{
	D3DBufferObject *bufferObject = D3DBuffer_GetBoundObject(target);
	if (!bufferObject) return;
	if (pname != GL_BUFFER_MAP_POINTER_ARB) {
		QGL_SET_ERROR(E_INVALID_ENUM);
		return;
	}
	if (!params) {
		QGL_SET_ERROR(E_INVALIDARG);
		return;
	}

	*params = bufferObject->mapped ? bufferObject->storage : nullptr;
	QGL_SET_ERROR(S_OK);
}

OPENGL_API void WINAPI glGetBufferPointervARB( GLenum target, GLenum pname, GLvoid **params )
{
	glGetBufferPointerv(target, pname, params);
}
