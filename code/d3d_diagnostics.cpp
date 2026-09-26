/***************************************************************************
* QindieGL diagnostic instrumentation.
***************************************************************************/
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_state.hpp"
#include "d3d_texture.hpp"
#include "d3d_buffer.hpp"
#include "d3d_extension.hpp"
#include "d3d_arb_program.hpp"
#include "d3d_matrix_stack.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

// Parameters of the currently bound ARB program object (d3d_extension.cpp).
extern GLfloat (*ARB_LocalParams( GLenum target ))[4];
extern bool ARB_GetLocalWriteStamp( GLenum target, GLuint index, uint64_t *frame, uint64_t *draw );

namespace {
	static const LONG kEventCapacity = 256;
	static const size_t kEventTextSize = 384;

	struct DiagnosticEvent
	{
		LONG sequence;
		bool d3dEvent;
		uint64_t frameId;
		uint64_t drawId;
		char category[24];
		char text[kEventTextSize];
	};

	struct DiagnosticState
	{
		uint64_t frameId;
		uint64_t drawId;
		uint64_t framesPresented;
		uint64_t drawsSubmitted;
		uint64_t drawsSkipped;
		uint64_t failedD3DCalls;
		uint64_t deviceResets;
		uint64_t pBuffersCreated;
		uint64_t arbProgramsUploaded;
		uint64_t arbProgramsCompiled;
		uint64_t arbProgramFailures;
		uint64_t vbosCreated;
		int64_t currentVBOBytes;
		uint64_t peakVBOBytes;
		int debugMaxDrawCall;
		int debugDumpFrame;
		int debugDumpDraw;
		bool crashDiagnostics;
		bool initialized;
		bool summaryDumped;
	};

	static DiagnosticState gDiagnostics = {};

	// Performance counters. Frame statistics only include frames with world
	// draws; the histogram has 0.1 ms buckets up to 200 ms plus an overflow.
	static const int kFrameHistogramBuckets = 2001;
	struct PerformanceState
	{
		int64_t frequency;
		int64_t lastFrameEnd;
		int64_t presentStart;
		int drawTimerDepth;
		int64_t frameDrawTicks;
		uint64_t frameVertices;
		uint64_t frameVertexBytes;
		uint64_t frameIndexBytes;
		uint64_t frames;
		double frameMsSum, frameMsMax;
		double drawMsSum, drawMsMax;
		double presentMsSum;
		uint64_t drawsSum, drawsMax;
		uint64_t verticesSum, vertexBytesSum, indexBytesSum;
		uint32_t histogram[kFrameHistogramBuckets];
	};
	static PerformanceState gPerformance = {};

	int64_t PerformanceNow()
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		if (!gPerformance.frequency) {
			LARGE_INTEGER frequency;
			QueryPerformanceFrequency(&frequency);
			gPerformance.frequency = frequency.QuadPart;
		}
		return now.QuadPart;
	}

	double TicksToMs( int64_t ticks )
	{
		return gPerformance.frequency ? ticks * 1000.0 / static_cast<double>(gPerformance.frequency) : 0.0;
	}

	void RecordFramePerformance( bool worldFrame, uint64_t draws )
	{
		const int64_t now = PerformanceNow();
		const double presentMs = gPerformance.presentStart ? TicksToMs(now - gPerformance.presentStart) : 0.0;
		if (worldFrame && gPerformance.lastFrameEnd) {
			const double frameMs = TicksToMs(now - gPerformance.lastFrameEnd);
			const double drawMs = TicksToMs(gPerformance.frameDrawTicks);
			++gPerformance.frames;
			gPerformance.frameMsSum += frameMs;
			gPerformance.frameMsMax = std::max(gPerformance.frameMsMax, frameMs);
			gPerformance.drawMsSum += drawMs;
			gPerformance.drawMsMax = std::max(gPerformance.drawMsMax, drawMs);
			gPerformance.presentMsSum += presentMs;
			gPerformance.drawsSum += draws;
			gPerformance.drawsMax = std::max(gPerformance.drawsMax, draws);
			gPerformance.verticesSum += gPerformance.frameVertices;
			gPerformance.vertexBytesSum += gPerformance.frameVertexBytes;
			gPerformance.indexBytesSum += gPerformance.frameIndexBytes;
			++gPerformance.histogram[std::min(static_cast<int>(frameMs * 10.0), kFrameHistogramBuckets - 1)];
		}
		gPerformance.lastFrameEnd = now;
		gPerformance.presentStart = 0;
		gPerformance.frameDrawTicks = 0;
		gPerformance.frameVertices = 0;
		gPerformance.frameVertexBytes = 0;
		gPerformance.frameIndexBytes = 0;
	}

	double FramePercentileMs( double fraction )
	{
		const uint64_t target = static_cast<uint64_t>(gPerformance.frames * fraction);
		uint64_t seen = 0;
		for (int bucket = 0; bucket < kFrameHistogramBuckets; ++bucket) {
			seen += gPerformance.histogram[bucket];
			if (seen > target) return (bucket + 1) / 10.0;
		}
		return kFrameHistogramBuckets / 10.0;
	}

	void DumpPerformanceSummary()
	{
		const uint64_t frames = gPerformance.frames;
		logPrintf("Performance (%llu frames with world draws):\n", static_cast<unsigned long long>(frames));
		if (!frames) return;
		const double frameMs = gPerformance.frameMsSum / frames;
		const double drawMs = gPerformance.drawMsSum / frames;
		logPrintf("  Frame time: avg %.2f ms (%.1f fps), p50 %.1f, p95 %.1f, p99 %.1f, max %.1f ms\n",
			frameMs, frameMs > 0.0 ? 1000.0 / frameMs : 0.0, FramePercentileMs(0.50),
			FramePercentileMs(0.95), FramePercentileMs(0.99), gPerformance.frameMsMax);
		logPrintf("  Inside QindieGL draw calls: avg %.2f ms/frame (%.0f%% of frame time), max %.2f ms\n",
			drawMs, frameMs > 0.0 ? 100.0 * drawMs / frameMs : 0.0, gPerformance.drawMsMax);
		logPrintf("  Present: avg %.2f ms/frame\n", gPerformance.presentMsSum / frames);
		logPrintf("  Draw calls: avg %.0f/frame, max %llu\n", static_cast<double>(gPerformance.drawsSum) / frames,
			static_cast<unsigned long long>(gPerformance.drawsMax));
		logPrintf("  Streamed to D3D9 by vertex arrays: avg %.0f vertices, %.1f KB vertex data, %.1f KB index data per frame\n",
			static_cast<double>(gPerformance.verticesSum) / frames,
			static_cast<double>(gPerformance.vertexBytesSum) / frames / 1024.0,
			static_cast<double>(gPerformance.indexBytesSum) / frames / 1024.0);
	}
	static DiagnosticEvent gEvents[kEventCapacity] = {};
	static volatile LONG gNextEvent = 0;
	static LPTOP_LEVEL_EXCEPTION_FILTER gPreviousExceptionFilter = nullptr;
	static bool gExceptionFilterInstalled = false;
	static char gRenderTarget[64] = "MAIN";
	static char gLastErrorSource[96] = "<none>";
	static char gActiveBuffers[128] = "array=0 element=0";
	static char gActivePrograms[128] = "vp=0 fp=0";
	static char gActiveTextures[512] = "none";
	static char gProjectionState[160] = "unavailable";
	static std::map<std::string, uint64_t> gD3DFailures;
	static std::map<std::string, uint64_t> gUnsupportedEnums;
	static std::set<uint32_t> gYAEWorldDrawStates;
	static std::set<GLuint> gYAEDumpedTextures;
	static unsigned int gYAEPostEffectDraws = 0;
	static bool gYAEPostEffectAfterDumped = false;

	const char *GLModeName( unsigned int mode )
	{
		switch (mode) {
		case GL_POINTS: return "POINTS";
		case GL_LINES: return "LINES";
		case GL_LINE_LOOP: return "LINE_LOOP";
		case GL_LINE_STRIP: return "LINE_STRIP";
		case GL_TRIANGLES: return "TRIANGLES";
		case GL_TRIANGLE_STRIP: return "TRIANGLE_STRIP";
		case GL_TRIANGLE_FAN: return "TRIANGLE_FAN";
		case GL_QUADS: return "QUADS";
		case GL_QUAD_STRIP: return "QUAD_STRIP";
		case GL_POLYGON: return "POLYGON";
		default: return "UNKNOWN";
		}
	}

	const char *GLErrorName( long error )
	{
		switch (error) {
		case E_INVALID_ENUM: return "GL_INVALID_ENUM";
		case E_INVALIDARG: return "GL_INVALID_VALUE";
		case E_INVALID_OPERATION: return "GL_INVALID_OPERATION";
		case E_STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
		case E_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
		case E_OUTOFMEMORY:
		case D3DERR_OUTOFVIDEOMEMORY: return "GL_OUT_OF_MEMORY";
		default: return "GL_INVALID_OPERATION";
		}
	}

	bool HasExtension( const char *extensions, const char *name )
	{
		if (!extensions || !name || !*name)
			return false;
		const size_t nameLength = strlen(name);
		const char *current = extensions;
		while ((current = strstr(current, name)) != nullptr) {
			const bool startsToken = current == extensions || current[-1] == ' ';
			const char after = current[nameLength];
			if (startsToken && (after == '\0' || after == ' '))
				return true;
			current += nameLength;
		}
		return false;
	}

	uint32_t HashBytes( const void *data, size_t length )
	{
		const unsigned char *bytes = static_cast<const unsigned char *>(data);
		uint32_t hash = 2166136261u;
		for (size_t i = 0; i < length; ++i) {
			hash ^= bytes[i];
			hash *= 16777619u;
		}
		return hash;
	}

	void MixHash( uint32_t& hash, const void *data, size_t length )
	{
		const unsigned char *bytes = static_cast<const unsigned char *>(data);
		for (size_t i = 0; i < length; ++i) {
			hash ^= bytes[i];
			hash *= 16777619u;
		}
	}

	int ResolveSampleVertex( int first, unsigned int indexType, const void *indices )
	{
		if (!indexType) return first;
		size_t size = indexType == GL_UNSIGNED_BYTE ? 1 : indexType == GL_UNSIGNED_SHORT ? 2 :
			indexType == GL_UNSIGNED_INT ? 4 : 0;
		if (!size) return first;
		const GLubyte *resolved = D3DBuffer_ResolvePointer(
			D3DBuffer_GetBinding(GL_ELEMENT_ARRAY_BUFFER_ARB), indices, size);
		if (!resolved) return first;
		if (indexType == GL_UNSIGNED_BYTE) return *resolved;
		if (indexType == GL_UNSIGNED_SHORT) return *reinterpret_cast<const GLushort *>(resolved);
		return static_cast<int>(*reinterpret_cast<const GLuint *>(resolved));
	}

	void LogArraySample( const char *name, const D3DVAInfo& info, int vertex )
	{
		const size_t componentBytes = info.elementType == GL_FLOAT ? sizeof(GLfloat) : 0;
		const size_t packedBytes = componentBytes * static_cast<size_t>(info.elementCount);
		const size_t stride = info.stride > 0 ? static_cast<size_t>(info.stride) : packedBytes;
		const size_t required = componentBytes ? static_cast<size_t>(vertex) * stride + packedBytes : 0;
		const GLubyte *base = componentBytes ?
			D3DBuffer_ResolvePointer(info.bufferBinding, info.data, required) : nullptr;
		if (!base) {
			logPrintfLevel(QGL_LOG_INFO, "YAE_DRAW_CENSUS",
				"%s buffer=%u offset=%p size=%d type=0x%X stride=%d sample=unavailable",
				name, info.bufferBinding, info.data, info.elementCount, info.elementType, info.stride);
			return;
		}
		const GLfloat *value = reinterpret_cast<const GLfloat *>(base + static_cast<size_t>(vertex) * stride);
		logPrintfLevel(QGL_LOG_INFO, "YAE_DRAW_CENSUS",
			"%s buffer=%u offset=%p size=%d type=0x%X stride=%d v%d=(%.6f,%.6f,%.6f,%.6f)",
			name, info.bufferBinding, info.data, info.elementCount, info.elementType, info.stride, vertex,
			info.elementCount > 0 ? value[0] : 0.0f, info.elementCount > 1 ? value[1] : 0.0f,
			info.elementCount > 2 ? value[2] : 0.0f, info.elementCount > 3 ? value[3] : 1.0f);
	}

	void CensusYAEWorldDraw( const char *api, unsigned int mode, int count, int first,
		unsigned int indexType, const void *indices )
	{
		if (!D3DGlobal.settings.game.yaeFallbackCompatibility || gDiagnostics.frameId < 250 ||
			!D3DGlobal.projectionMatrixStack || D3DGlobal_IsOrthoProjection() ||
			!D3DState.EnableState.depthTestEnabled ||
			!(D3DState.ClientVertexArrayState.vertexArrayEnable & VA_ENABLE_VERTEX_BIT) ||
			gYAEWorldDrawStates.size() >= 64)
			return;

		uint32_t signature = 2166136261u;
		MixHash(signature, &D3DState.ClientVertexArrayState.vertexArrayEnable,
			sizeof(D3DState.ClientVertexArrayState.vertexArrayEnable));
		MixHash(signature, &D3DState.EnableState.textureEnabled,
			sizeof(D3DState.EnableState.textureEnabled));
		MixHash(signature, &D3DState.EnableState.textureTargetEnabled,
			sizeof(D3DState.EnableState.textureTargetEnabled));
		MixHash(signature, &D3DState.EnableState.vertexProgramEnabled,
			sizeof(D3DState.EnableState.vertexProgramEnabled));
		MixHash(signature, &D3DState.EnableState.fragmentProgramEnabled,
			sizeof(D3DState.EnableState.fragmentProgramEnabled));
		for (int unit = 0; unit < D3DGlobal.maxActiveTMU; ++unit) {
			MixHash(signature, &D3DState.TextureState.TextureCombineState[unit],
				sizeof(D3DState.TextureState.TextureCombineState[unit]));
			for (int target = 0; target < D3D_TEXTARGET_MAX; ++target) {
				D3DTextureObject *texture = D3DState.TextureState.currentTexture[unit][target];
				GLuint id = texture ? texture->GetGLIndex() : 0;
				MixHash(signature, &id, sizeof(id));
			}
		}
		if (!gYAEWorldDrawStates.insert(signature).second) return;

		const int sampleVertex = ResolveSampleVertex(first, indexType, indices);
		logPrintfLevel(QGL_LOG_INFO, "YAE_DRAW_CENSUS",
			"state=%u/64 signature=%08X frame=%llu draw=%llu api=%s mode=0x%X count=%d sampleVertex=%d arrayMask=0x%08X programs=%u/%u enabled=%u/%u",
			(unsigned int)gYAEWorldDrawStates.size(), signature,
			static_cast<unsigned long long>(gDiagnostics.frameId),
			static_cast<unsigned long long>(gDiagnostics.drawId), api, mode, count, sampleVertex,
			D3DState.ClientVertexArrayState.vertexArrayEnable,
			ARB_GetBoundVertexProgram(), ARB_GetBoundFragmentProgram(),
			D3DState.EnableState.vertexProgramEnabled, D3DState.EnableState.fragmentProgramEnabled);
		LogArraySample("vertex", D3DState.ClientVertexArrayState.vertexInfo, sampleVertex);
		for (int unit = 0; unit < D3DGlobal.maxActiveTMU; ++unit) {
			const bool coordEnabled = VA_TEXTURE_BIT_IS_SET(D3DState.ClientVertexArrayState.vertexArrayEnable, unit);
			if (!D3DState.EnableState.textureEnabled[unit] && !coordEnabled) continue;
			GLuint textureId = 0;
			int chosenTarget = -1;
			unsigned int targetMask = 0;
			for (int target = 0; target < D3D_TEXTARGET_MAX; ++target) {
				if (D3DState.EnableState.textureTargetEnabled[unit][target]) {
					targetMask |= 1u << target;
					D3DTextureObject *texture = D3DState.TextureState.currentTexture[unit][target];
					if (texture) { textureId = texture->GetGLIndex(); chosenTarget = target; }
				}
			}
			const auto& combiner = D3DState.TextureState.TextureCombineState[unit];
			DWORD textureTransformFlags = D3DTTFF_DISABLE;
			DWORD textureCoordinateIndex = 0;
			D3DGlobal.pDevice->GetTextureStageState(unit, D3DTSS_TEXTURETRANSFORMFLAGS,
				&textureTransformFlags);
			D3DGlobal.pDevice->GetTextureStageState(unit, D3DTSS_TEXCOORDINDEX,
				&textureCoordinateIndex);
			logPrintfLevel(QGL_LOG_INFO, "YAE_DRAW_CENSUS",
				"tmu=%d enabled=%u targetMask=0x%X texture=%u target=%d size=%ux%u coord=%s d3dCoord=%u transform=0x%X env=0x%X rgbOp=0x%X rgbArgs=0x%X/0x%X/0x%X scale=%u",
				unit, D3DState.EnableState.textureEnabled[unit], targetMask, textureId, chosenTarget,
				chosenTarget >= 0 && D3DState.TextureState.currentTexture[unit][chosenTarget] ?
					D3DState.TextureState.currentTexture[unit][chosenTarget]->GetWidth() : 0,
				chosenTarget >= 0 && D3DState.TextureState.currentTexture[unit][chosenTarget] ?
					D3DState.TextureState.currentTexture[unit][chosenTarget]->GetHeight() : 0,
				coordEnabled ? "YES" : "NO", textureCoordinateIndex, textureTransformFlags,
				combiner.envMode, combiner.colorOp,
				combiner.colorArg1, combiner.colorArg2, combiner.colorArg3, combiner.colorScale);

			// Capture the first static-world material inputs after the level has settled.
			// D3DX performs the DXT decompression, making the dump useful for checking
			// whether corruption happened during upload rather than during sampling.
			if (D3DState.EnableState.vertexProgramEnabled && ARB_GetBoundVertexProgram() == 6 &&
				chosenTarget >= 0) {
				D3DTextureObject *texture = D3DState.TextureState.currentTexture[unit][chosenTarget];
				if (texture && texture->GetTarget() != GL_TEXTURE_CUBE_MAP_ARB &&
					(texture->GetGLIndex() == 1 || texture->GetGLIndex() == 209) &&
					gYAEDumpedTextures.insert(texture->GetGLIndex()).second) {
					_mkdir("QindieGL-dump");
					_mkdir("QindieGL-dump\\textures");
					char filename[MAX_PATH];
					sprintf_s(filename, "QindieGL-dump\\textures\\yae_id_%u_%ux%u.png",
						texture->GetGLIndex(), texture->GetWidth(), texture->GetHeight());
					const HRESULT dumpResult = D3DXSaveTextureToFileA(filename, D3DXIFF_PNG,
						texture->GetD3DTexture(), nullptr);
					logPrintfLevel(QGL_LOG_INFO, "YAE_TEXTURE_DUMP",
						"texture=%u tmu=%d file=%s result=0x%08X",
						texture->GetGLIndex(), unit, filename, dumpResult);
				}
			}
			if (coordEnabled) {
				char name[24];
				sprintf_s(name, "texcoord%d", unit);
				LogArraySample(name, D3DState.ClientVertexArrayState.texCoordInfo[unit], sampleVertex);
			}
		}
	}

	void TraceYAEPostEffectDraw( const char *api, unsigned int mode, int count, int first,
		unsigned int indexType, const void *indices )
	{
		if (!D3DGlobal.settings.game.yaeFallbackCompatibility ||
			!D3DState.EnableState.fragmentProgramEnabled ||
			ARB_GetBoundFragmentProgram() != 8 || gYAEPostEffectDraws >= 96)
			return;

		++gYAEPostEffectDraws;
		const int sampleVertex = ResolveSampleVertex(first, indexType, indices);
		logPrintfLevel(QGL_LOG_INFO, "YAE_POST_EFFECT",
			"sample=%u frame=%llu draw=%llu api=%s mode=0x%X count=%d vertex=%d blend=%u glBlend=0x%X/0x%X d3dBlend=%u/%u op=%u color=0x%08X arrays=0x%08X programs=%u/%u enabled=%u/%u requiredTexcoords=%d",
			gYAEPostEffectDraws,
			static_cast<unsigned long long>(gDiagnostics.frameId),
			static_cast<unsigned long long>(gDiagnostics.drawId), api, mode, count, sampleVertex,
			D3DState.EnableState.alphaBlendEnabled,
			D3DState.ColorBufferState.glBlendSrc, D3DState.ColorBufferState.glBlendDst,
			D3DState.ColorBufferState.alphaBlendSrcFunc,
			D3DState.ColorBufferState.alphaBlendDstFunc,
			D3DState.ColorBufferState.alphaBlendOp, D3DState.CurrentState.currentColor,
			D3DState.ClientVertexArrayState.vertexArrayEnable,
			ARB_GetBoundVertexProgram(), ARB_GetBoundFragmentProgram(),
			D3DState.EnableState.vertexProgramEnabled,
			D3DState.EnableState.fragmentProgramEnabled,
			ARB_GetRequiredVertexTexCoordCount());

		if (gYAEPostEffectDraws == 1) {
			_mkdir("QindieGL-dump");
			_mkdir("QindieGL-dump\\textures");
			D3DTextureObject *source = D3DState.TextureState.currentTexture[0][D3D_TEXTARGET_2D];
			const HRESULT sourceResult = source ? D3DXSaveTextureToFileA(
				"QindieGL-dump\\textures\\yae_post_source.png", D3DXIFF_PNG,
				source->GetD3DTexture(), nullptr) : E_FAIL;
			logPrintfLevel(QGL_LOG_INFO, "YAE_POST_EFFECT",
				"source dump result=0x%08X file=QindieGL-dump\\textures\\yae_post_source.png",
				sourceResult);
		}

		for (int unit : { 0, 1, 2, 4 }) {
			D3DTextureObject *texture = D3DState.TextureState.currentTexture[unit][D3D_TEXTARGET_2D];
			DWORD transformFlags = D3DTTFF_DISABLE;
			D3DGlobal.pDevice->GetTextureStageState(unit, D3DTSS_TEXTURETRANSFORMFLAGS,
				&transformFlags);
			D3DStateMatrix& matrix = D3DGlobal.textureMatrixStack[unit]->top();
			const D3DXMATRIX& m = *static_cast<const D3DXMATRIX *>(matrix);
			logPrintfLevel(QGL_LOG_INFO, "YAE_POST_EFFECT",
				"tmu=%d enabled=%u texture=%u size=%ux%u format=%d coord=(%.3f,%.3f,%.3f,%.3f) matrixIdentity=%u transform=0x%X matrix=[%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f]",
				unit, D3DState.EnableState.textureEnabled[unit], texture ? texture->GetGLIndex() : 0,
				texture ? texture->GetWidth() : 0, texture ? texture->GetHeight() : 0,
				texture ? texture->GetInternalFormat() : -1,
				D3DState.CurrentState.currentTexCoord[unit][0],
				D3DState.CurrentState.currentTexCoord[unit][1],
				D3DState.CurrentState.currentTexCoord[unit][2],
				D3DState.CurrentState.currentTexCoord[unit][3],
				matrix.is_identity(), transformFlags,
				m._11, m._12, m._13, m._14, m._21, m._22, m._23, m._24,
				m._31, m._32, m._33, m._34, m._41, m._42, m._43, m._44);
			if (VA_TEXTURE_BIT_IS_SET(D3DState.ClientVertexArrayState.vertexArrayEnable, unit)) {
				char name[24];
				sprintf_s(name, "postTexcoord%d", unit);
				LogArraySample(name, D3DState.ClientVertexArrayState.texCoordInfo[unit], sampleVertex);
			}
		}
	}

	// Reads one array element without integer normalization, matching how the
	// draw path feeds texcoord arrays (YAE bone indices are GL_SHORT texcoords).
	// Unlike D3DBuffer_ResolvePointer this never records a GL error.
	bool ReadArrayElement( const D3DVAInfo& info, int vertex, float out[4] )
	{
		out[0] = out[1] = out[2] = 0.0f;
		out[3] = 1.0f;
		size_t componentBytes = 0;
		switch (info.elementType) {
		case GL_BYTE: case GL_UNSIGNED_BYTE: componentBytes = 1; break;
		case GL_SHORT: case GL_UNSIGNED_SHORT: componentBytes = 2; break;
		case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: componentBytes = 4; break;
		case GL_DOUBLE: componentBytes = 8; break;
		default: return false;
		}
		if (vertex < 0 || info.elementCount <= 0) return false;
		const size_t packedBytes = componentBytes * static_cast<size_t>(info.elementCount);
		const size_t stride = info.stride > 0 ? static_cast<size_t>(info.stride) : packedBytes;
		const size_t offset = static_cast<size_t>(vertex) * stride;
		const GLubyte *base = info.data;
		if (info.bufferBinding) {
			D3DBufferObject *buffer = D3DBuffer_GetObject(info.bufferBinding, false);
			const size_t bufferOffset = reinterpret_cast<size_t>(info.data);
			if (!buffer || !buffer->storage || buffer->size < 0 ||
				bufferOffset > static_cast<size_t>(buffer->size) ||
				offset + packedBytes > static_cast<size_t>(buffer->size) - bufferOffset)
				return false;
			base = static_cast<const GLubyte *>(buffer->storage) + bufferOffset;
		}
		if (!base) return false;
		const GLubyte *element = base + offset;
		for (int i = 0; i < info.elementCount && i < 4; ++i) {
			switch (info.elementType) {
			case GL_BYTE: out[i] = reinterpret_cast<const GLbyte *>(element)[i]; break;
			case GL_UNSIGNED_BYTE: out[i] = element[i]; break;
			case GL_SHORT: out[i] = reinterpret_cast<const GLshort *>(element)[i]; break;
			case GL_UNSIGNED_SHORT: out[i] = reinterpret_cast<const GLushort *>(element)[i]; break;
			case GL_INT: out[i] = static_cast<float>(reinterpret_cast<const GLint *>(element)[i]); break;
			case GL_UNSIGNED_INT: out[i] = static_cast<float>(reinterpret_cast<const GLuint *>(element)[i]); break;
			case GL_FLOAT: out[i] = reinterpret_cast<const GLfloat *>(element)[i]; break;
			case GL_DOUBLE: out[i] = static_cast<float>(reinterpret_cast<const GLdouble *>(element)[i]); break;
			}
		}
		return true;
	}

	float Dot4( const GLfloat row[4], const float v[4] )
	{
		return row[0] * v[0] + row[1] * v[1] + row[2] * v[2] + row[3] * v[3];
	}

	float Length3( const float v[3] )
	{
		return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	}

	// Phase F probe for DS2's lit dynamic-material fragment programs, whose
	// linear fog reads the vertex program's TEXCOORD5 (camera_pos_ws minus the
	// inst_matrix-transformed position). The sample vertex is evaluated on the
	// CPU exactly as the rigid/skinned DS2 vertex programs do, and the camera
	// implied by the GL modelview is compared with the camera constants DS2
	// supplied. A mismatch identifies which input disagrees with the transform
	// that actually positions the geometry. Active only with LogLevel >= 3 (DEBUG);
	// the first saturated draws also dump the program/modelview call history.
	std::set<uint32_t> gYAEFogProbeStates;
	unsigned int gYAEFogProbeRecords = 0;

	struct ProgramHistoryEntry
	{
		uint32_t frame;
		uint32_t draw;
		char op;
		unsigned int target;
		unsigned int program;
		int index;
		float values[4];
	};
	static const size_t kProgramHistoryCapacity = 16384;
	static const unsigned int kProgramHistoryMaxDumps = 3;
	ProgramHistoryEntry gProgramHistory[kProgramHistoryCapacity];
	size_t gProgramHistoryNext = 0;
	unsigned int gProgramHistoryDumps = 0;

	bool ProgramHistoryActive()
	{
		return D3DGlobal.settings.game.yaeFallbackCompatibility && gDiagnostics.frameId >= 250 &&
			gProgramHistoryDumps < kProgramHistoryMaxDumps && logIsEnabled(QGL_LOG_DEBUG);
	}

	const char *ProgramTargetName( unsigned int target )
	{
		return target == GL_VERTEX_PROGRAM_ARB ? "VP" : target == GL_FRAGMENT_PROGRAM_ARB ? "FP" : "?";
	}

	// Writes the program binds, local writes and modelview operations issued
	// since four draws before the current one. Bone palette rows (VP local
	// 10..209) are counted rather than listed.
	void DumpProgramHistory( unsigned int record )
	{
		static const char *const modelviewOps[] = {
			"LoadIdentity", "LoadMatrix", "MultMatrix", "PushMatrix", "PopMatrix",
			"Translate", "Rotate", "Scale" };
		++gProgramHistoryDumps;
		const uint32_t frame = static_cast<uint32_t>(gDiagnostics.frameId);
		const uint32_t firstDraw = gDiagnostics.drawId > 4 ? static_cast<uint32_t>(gDiagnostics.drawId) - 4 : 0;
		const size_t available = gProgramHistoryNext < kProgramHistoryCapacity ?
			gProgramHistoryNext : kProgramHistoryCapacity;
		unsigned int boneWrites = 0, redundantBinds = 0, lines = 0;
		unsigned int boundVP = ~0u, boundFP = ~0u;
		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY", "record=%u begin frame=%u fromDraw=%u",
			record, frame, firstDraw);
		for (size_t i = gProgramHistoryNext - available; i < gProgramHistoryNext; ++i) {
			const ProgramHistoryEntry& e = gProgramHistory[i % kProgramHistoryCapacity];
			if (e.frame != frame || e.draw < firstDraw) continue;
			if (e.op == 'L' && e.target == GL_VERTEX_PROGRAM_ARB && e.index >= 10 && e.index <= 209) {
				++boneWrites;
				continue;
			}
			// DS2 rebinds the same program around every parameter write.
			if (e.op == 'B') {
				unsigned int& bound = e.target == GL_VERTEX_PROGRAM_ARB ? boundVP : boundFP;
				if (bound == e.program) { ++redundantBinds; continue; }
				bound = e.program;
			}
			if (++lines > 400) break;
			switch (e.op) {
			case 'D':
				logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY",
					"record=%u D:%u DRAW count=%.0f vp=%u(on=%.0f) fp=%d(on=%.0f)",
					record, e.draw, e.values[0], e.program, e.values[1], e.index, e.values[2]);
				break;
			case 'B':
				logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY", "record=%u D:%u bind %s %u",
					record, e.draw, ProgramTargetName(e.target), e.program);
				break;
			case 'E': case 'e':
				logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY", "record=%u D:%u %s %s",
					record, e.draw, e.op == 'E' ? "enable" : "disable", ProgramTargetName(e.target));
				break;
			case 'L':
				logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY",
					"record=%u D:%u local %s program=%u [%d] = (%.4f, %.4f, %.4f, %.4f)",
					record, e.draw, ProgramTargetName(e.target), e.program, e.index,
					e.values[0], e.values[1], e.values[2], e.values[3]);
				break;
			case 'M':
				logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY",
					"record=%u D:%u modelview %s (%.4f, %.4f, %.4f, %.4f)",
					record, e.draw, e.index >= 0 && e.index < 8 ? modelviewOps[e.index] : "?",
					e.values[0], e.values[1], e.values[2], e.values[3]);
				break;
			}
		}
		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY",
			"record=%u end lines=%u boneWritesOmitted=%u redundantBindsOmitted=%u",
			record, lines, boneWrites, redundantBinds);
	}

	void LogLocalWriteStamps( unsigned int record, const char *name, GLenum target,
		const int *indices, int count )
	{
		char text[256] = "";
		size_t used = 0;
		for (int i = 0; i < count && used < sizeof(text) - 32; ++i) {
			uint64_t frame = 0, draw = 0;
			int written;
			if (indices[i] < 0)
				continue;
			if (ARB_GetLocalWriteStamp(target, static_cast<GLuint>(indices[i]), &frame, &draw))
				written = sprintf_s(text + used, sizeof(text) - used, " [%d]@F%llu:D%llu", indices[i],
					static_cast<unsigned long long>(frame), static_cast<unsigned long long>(draw));
			else
				written = sprintf_s(text + used, sizeof(text) - used, " [%d]@never", indices[i]);
			if (written > 0) used += static_cast<size_t>(written);
		}
		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_PROBE", "record=%u %s lastWrites%s (now F%llu:D%llu)",
			record, name, text, static_cast<unsigned long long>(gDiagnostics.frameId),
			static_cast<unsigned long long>(gDiagnostics.drawId));
	}

	void ProbeYAEProgramFog( const char *api, int count, int first, unsigned int indexType,
		const void *indices )
	{
		if (!D3DGlobal.settings.game.yaeFallbackCompatibility || gDiagnostics.frameId < 250 ||
			!D3DState.EnableState.fragmentProgramEnabled || gYAEFogProbeRecords >= 160 ||
			!D3DGlobal.modelviewMatrixStack || !logIsEnabled(QGL_LOG_DEBUG))
			return;

		const GLuint fpId = ARB_GetBoundFragmentProgram();
		ARBCompiledProgram *fp = ARB_GetCompiledProgram(fpId);
		if (!fp || !fp->ps) return;
		const ARBParsedProgram& fpParsed = fp->parsed;
		if (!fpParsed.usedTexCoords.count(5) || !fpParsed.usedLocalParams.count(0) ||
			!fpParsed.usedLocalParams.count(1))
			return;

		GLfloat (*fpLocal)[4] = ARB_LocalParams(GL_FRAGMENT_PROGRAM_ARB);
		const auto& arrays = D3DState.ClientVertexArrayState;
		const int sampleVertex = ResolveSampleVertex(first, indexType, indices);

		float position[4];
		if (!(arrays.vertexArrayEnable & VA_ENABLE_VERTEX_BIT) ||
			!ReadArrayElement(arrays.vertexInfo, sampleVertex, position))
			return;

		const GLuint vpId = D3DState.EnableState.vertexProgramEnabled ? ARB_GetBoundVertexProgram() : 0;
		ARBCompiledProgram *vp = vpId ? ARB_GetCompiledProgram(vpId) : nullptr;
		const bool vpActive = vp && vp->vs;
		const bool skinned = vpActive && !vp->parsed.addressReg.empty();
		GLfloat (*vpLocal)[4] = ARB_LocalParams(GL_VERTEX_PROGRAM_ARB);

		// Model-space position consumed by MVP (DS2 VP register R2 / vertex.position).
		float model[4] = { position[0], position[1], position[2], 1.0f };
		float boneIds[4] = { 0, 0, 0, 1 }, boneWeights[4] = { 0, 0, 0, 1 };
		bool skinInputs = false;
		if (skinned &&
			VA_TEXTURE_BIT_IS_SET(arrays.vertexArrayEnable, 1) &&
			VA_TEXTURE_BIT_IS_SET(arrays.vertexArrayEnable, 2) &&
			ReadArrayElement(arrays.texCoordInfo[1], sampleVertex, boneIds) &&
			ReadArrayElement(arrays.texCoordInfo[2], sampleVertex, boneWeights)) {
			skinInputs = true;
			float skinnedPos[3] = { 0, 0, 0 };
			for (int influence = 0; influence < 2; ++influence) {
				const int a0 = static_cast<int>(floorf(boneIds[influence] * 3.0f));
				if (a0 + 11 < 0 || a0 + 11 >= 256) { skinInputs = false; break; }
				for (int axis = 0; axis < 3; ++axis)
					skinnedPos[axis] += boneWeights[influence] * Dot4(vpLocal[a0 + 9 + axis], position);
			}
			if (skinInputs) {
				model[0] = skinnedPos[0]; model[1] = skinnedPos[1]; model[2] = skinnedPos[2];
			}
		}

		// The matrix stack stores the transpose of the GL matrix, so D3DX row-vector
		// transforms reproduce GL's column-vector transforms.
		const D3DXMATRIX& modelview = *static_cast<const D3DXMATRIX *>(D3DGlobal.modelviewMatrixStack->top());
		D3DXVECTOR4 eye;
		D3DXVec4Transform(&eye, reinterpret_cast<const D3DXVECTOR4 *>(model), &modelview);
		const float eyeDistance = Length3(&eye.x);
		D3DXMATRIX inverseModelview;
		float modelCamera[4] = { 0, 0, 0, 1 };
		const bool invertible = D3DXMatrixInverse(&inverseModelview, nullptr, &modelview) != nullptr;
		if (invertible && inverseModelview._44 != 0.0f) {
			modelCamera[0] = inverseModelview._41 / inverseModelview._44;
			modelCamera[1] = inverseModelview._42 / inverseModelview._44;
			modelCamera[2] = inverseModelview._43 / inverseModelview._44;
		}
		float modelviewScale[3];
		for (int column = 0; column < 3; ++column) {
			const float axis[3] = { modelview.m[column][0], modelview.m[column][1], modelview.m[column][2] };
			modelviewScale[column] = Length3(axis);
		}

		float world[3] = { 0, 0, 0 }, camera[3] = { 0, 0, 0 }, impliedCamera[3] = { 0, 0, 0 };
		float toEye[3] = { 0, 0, 0 };
		int cameraIndex = -1;
		if (vpActive) {
			cameraIndex = vp->parsed.usedLocalParams.empty() ? -1 : *vp->parsed.usedLocalParams.rbegin();
			for (int axis = 0; axis < 3; ++axis) {
				world[axis] = Dot4(vpLocal[5 + axis], model);
				impliedCamera[axis] = Dot4(vpLocal[5 + axis], modelCamera);
				camera[axis] = cameraIndex >= 0 && cameraIndex < 256 ? vpLocal[cameraIndex][axis] : 0.0f;
				toEye[axis] = camera[axis] - world[axis];
			}
		} else {
			// Fixed-function vertex processing forwards texture coordinate set 5.
			float texcoord5[4];
			if (!VA_TEXTURE_BIT_IS_SET(arrays.vertexArrayEnable, 5) ||
				!ReadArrayElement(arrays.texCoordInfo[5], sampleVertex, texcoord5))
				memcpy(texcoord5, D3DState.CurrentState.currentTexCoord[5], sizeof(texcoord5));
			memcpy(toEye, texcoord5, sizeof(toEye));
		}
		const float fogDistance = Length3(toEye);
		const float fogStart = fpLocal[0][0], fogEnd = fpLocal[1][0];
		float fog = fogEnd != fogStart ? (fogDistance - fogStart) / (fogEnd - fogStart) : 1.0f;
		fog = fog < 0.0f ? 0.0f : (fog > 1.0f ? 1.0f : fog);
		const float fpCameraDelta[3] = {
			fpLocal[11][0] - modelCamera[0], fpLocal[11][1] - modelCamera[1], fpLocal[11][2] - modelCamera[2] };
		const float vpCameraDelta[3] = {
			camera[0] - impliedCamera[0], camera[1] - impliedCamera[1], camera[2] - impliedCamera[2] };
		// fog is DS2's own value; renderedFog includes yae_eye_distance_fog.
		const bool saturated = fog > 0.95f;
		float renderedFog = fog;
		if (vpActive && vp->parsed.eyeDistanceTexCoord5 && fogEnd != fogStart) {
			renderedFog = (eyeDistance - fogStart) / (fogEnd - fogStart);
			renderedFog = renderedFog < 0.0f ? 0.0f : (renderedFog > 1.0f ? 1.0f : renderedFog);
		}

		D3DTextureObject *diffuse = D3DState.TextureState.currentTexture[0][D3D_TEXTARGET_2D];
		const GLuint diffuseId = diffuse ? diffuse->GetGLIndex() : 0;
		uint32_t signature = 2166136261u;
		MixHash(signature, &vpId, sizeof(vpId));
		MixHash(signature, &fpId, sizeof(fpId));
		MixHash(signature, &diffuseId, sizeof(diffuseId));
		MixHash(signature, &saturated, sizeof(saturated));
		if (!gYAEFogProbeStates.insert(signature).second) return;
		++gYAEFogProbeRecords;

		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_PROBE",
			"record=%u frame=%llu draw=%llu api=%s count=%d vertex=%d vp=%u(%s) fp=%u diffuse=%u %ux%u fog=%.3f%s renderedFog=%.3f fogDist=%.3f eyeDist=%.3f start=%.3f end=%.3f color=(%.3f,%.3f,%.3f) vpCamErr=%.3f fpCamErr=%.3f",
			gYAEFogProbeRecords, static_cast<unsigned long long>(gDiagnostics.frameId),
			static_cast<unsigned long long>(gDiagnostics.drawId), api, count, sampleVertex,
			vpId, vpActive ? (skinned ? (skinInputs ? "skinned" : "skinned-noinputs") : "rigid") :
				(D3DState.EnableState.vertexProgramEnabled ? "enabled-uncompiled" : "fixed-function"),
			fpId, diffuseId, diffuse ? diffuse->GetWidth() : 0, diffuse ? diffuse->GetHeight() : 0,
			fog, saturated ? " SATURATED" : "", renderedFog, fogDistance, eyeDistance, fogStart, fogEnd,
			fpLocal[2][0], fpLocal[2][1], fpLocal[2][2],
			vpActive ? Length3(vpCameraDelta) : -1.0f,
			fpParsed.usedLocalParams.count(11) ? Length3(fpCameraDelta) : -1.0f);
		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_PROBE",
			"record=%u pos=(%.3f,%.3f,%.3f) bones=(%.1f,%.1f) weights=(%.3f,%.3f) model=(%.3f,%.3f,%.3f) eye=(%.3f,%.3f,%.3f) world=(%.3f,%.3f,%.3f) toEye=(%.3f,%.3f,%.3f)",
			gYAEFogProbeRecords, position[0], position[1], position[2],
			boneIds[0], boneIds[1], boneWeights[0], boneWeights[1],
			model[0], model[1], model[2], eye.x, eye.y, eye.z,
			world[0], world[1], world[2], toEye[0], toEye[1], toEye[2]);
		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_PROBE",
			"record=%u vpCam[local%d]=(%.3f,%.3f,%.3f) impliedWorldCam=(%.3f,%.3f,%.3f) fpCam[local11]=(%.3f,%.3f,%.3f) impliedModelCam=(%.3f,%.3f,%.3f) mvScale=(%.4f,%.4f,%.4f)",
			gYAEFogProbeRecords, cameraIndex, camera[0], camera[1], camera[2],
			impliedCamera[0], impliedCamera[1], impliedCamera[2],
			fpLocal[11][0], fpLocal[11][1], fpLocal[11][2],
			modelCamera[0], modelCamera[1], modelCamera[2],
			modelviewScale[0], modelviewScale[1], modelviewScale[2]);
		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_PROBE",
			"record=%u inst=[%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f] modelviewGL=[%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f]",
			gYAEFogProbeRecords,
			vpLocal[5][0], vpLocal[5][1], vpLocal[5][2], vpLocal[5][3],
			vpLocal[6][0], vpLocal[6][1], vpLocal[6][2], vpLocal[6][3],
			vpLocal[7][0], vpLocal[7][1], vpLocal[7][2], vpLocal[7][3],
			modelview._11, modelview._21, modelview._31, modelview._41,
			modelview._12, modelview._22, modelview._32, modelview._42,
			modelview._13, modelview._23, modelview._33, modelview._43);

		const int vpStamps[] = { 5, 6, 7, cameraIndex };
		const int fpStamps[] = { 0, 1, 2, 11 };
		if (vpActive)
			LogLocalWriteStamps(gYAEFogProbeRecords, "vp", GL_VERTEX_PROGRAM_ARB, vpStamps, 4);
		LogLocalWriteStamps(gYAEFogProbeRecords, "fp", GL_FRAGMENT_PROGRAM_ARB, fpStamps, 4);
		if (saturated && gProgramHistoryDumps < kProgramHistoryMaxDumps)
			DumpProgramHistory(gYAEFogProbeRecords);
	}

	void SnapshotState()
	{
		if (!D3DGlobal.initialized)
			return;

		const GLuint arrayBuffer = D3DBuffer_GetBinding(GL_ARRAY_BUFFER_ARB);
		const GLuint elementBuffer = D3DBuffer_GetBinding(GL_ELEMENT_ARRAY_BUFFER_ARB);
		const GLuint vertexProgram = ARB_GetBoundVertexProgram();
		const GLuint fragmentProgram = ARB_GetBoundFragmentProgram();
		const char *projection = "UNAVAILABLE";
		uint32_t projectionHash = 0;
		uint32_t modelviewHash = 0;
		if (D3DGlobal.projectionMatrixStack && D3DGlobal.modelviewMatrixStack) {
			projection = D3DGlobal_IsOrthoProjection() ? "ORTHOGRAPHIC" : "PERSPECTIVE";
			projectionHash = HashBytes(D3DGlobal.projectionMatrixStack->top(), sizeof(D3DXMATRIX));
			modelviewHash = HashBytes(D3DGlobal.modelviewMatrixStack->top(), sizeof(D3DXMATRIX));
		}
		sprintf_s(gActiveBuffers, "array=%u element=%u", arrayBuffer, elementBuffer);
		sprintf_s(gActivePrograms, "vp=%u fp=%u", vertexProgram, fragmentProgram);
		sprintf_s(gProjectionState, "%s projHash=%08X modelviewHash=%08X",
			projection, projectionHash, modelviewHash);
		gActiveTextures[0] = '\0';
		for (int unit = 0; unit < D3DGlobal.maxActiveTMU; ++unit) {
			for (int target = 0; target < D3D_TEXTARGET_MAX; ++target) {
				D3DTextureObject *texture = D3DState.TextureState.currentTexture[unit][target];
				if (!texture) continue;
				const size_t used = strlen(gActiveTextures);
				if (used + 32 >= sizeof(gActiveTextures)) continue;
				_snprintf_s(gActiveTextures + used, sizeof(gActiveTextures) - used, _TRUNCATE,
					"tmu%d:id%u ", unit, texture->GetGLIndex());
			}
		}
		if (!*gActiveTextures)
			strcpy_s(gActiveTextures, "none");

		QGL_DiagnosticsRecordEvent(false, "STATE",
			"buffers(%s) programs(%s) textures(%s) rt=%s projection=%s",
			gActiveBuffers, gActivePrograms, gActiveTextures, gRenderTarget, gProjectionState);
	}

	void DumpArray( const char *name, bool enabled, const D3DVAInfo& info )
	{
		logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE",
			"array=%s enabled=%s size=%d type=0x%X stride=%d pointer=%p arrayBuffer=%u",
			name, enabled ? "YES" : "NO", info.elementCount, info.elementType,
			info.stride, info.data, D3DBuffer_GetBinding(GL_ARRAY_BUFFER_ARB));
	}

	void DumpSelectedDrawState( const char *api, unsigned int mode, int count,
		int first, unsigned int indexType, const void *indices )
	{
		logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE", "===== Selected draw state =====");
		logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE",
			"api=%s primitive=%s(0x%X) count=%d first=%d indexType=0x%X indices=%p",
			api, GLModeName(mode), mode, count, first, indexType, indices);

		const DWORD mask = D3DState.ClientVertexArrayState.vertexArrayEnable;
		DumpArray("vertex", (mask & VA_ENABLE_VERTEX_BIT) != 0, D3DState.ClientVertexArrayState.vertexInfo);
		DumpArray("normal", (mask & VA_ENABLE_NORMAL_BIT) != 0, D3DState.ClientVertexArrayState.normalInfo);
		DumpArray("color", (mask & VA_ENABLE_COLOR_BIT) != 0, D3DState.ClientVertexArrayState.colorInfo);
		DumpArray("secondaryColor", (mask & VA_ENABLE_COLOR2_BIT) != 0, D3DState.ClientVertexArrayState.color2Info);
		DumpArray("fog", (mask & VA_ENABLE_FOG_BIT) != 0, D3DState.ClientVertexArrayState.fogInfo);
		for (int i = 0; i < D3DGlobal.maxActiveTMU; ++i) {
			char name[24];
			sprintf_s(name, "texcoord%d", i);
			DumpArray(name, VA_TEXTURE_BIT_IS_SET(mask, i), D3DState.ClientVertexArrayState.texCoordInfo[i]);
			for (int target = 0; target < D3D_TEXTARGET_MAX; ++target) {
				D3DTextureObject *texture = D3DState.TextureState.currentTexture[i][target];
				if (!texture)
					continue;
				logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE",
					"texture tmu=%d id=%u target=0x%X size=%ux%ux%u internal=0x%X",
					i, texture->GetGLIndex(), texture->GetTarget(), texture->GetWidth(),
					texture->GetHeight(), texture->GetDepth(), texture->GetInternalFormat());
			}
		}

		logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE",
			"blend=%u alphaTest=%u depthTest=%u depthWrite=%u cull=%u fog=%u stencil=%u",
			D3DState.EnableState.alphaBlendEnabled, D3DState.EnableState.alphaTestEnabled,
			D3DState.EnableState.depthTestEnabled, D3DState.DepthBufferState.depthWriteMask,
			D3DState.EnableState.cullEnabled, D3DState.EnableState.fogEnabled,
			D3DState.EnableState.stencilTestEnabled);
		logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE",
			"buffers array=%u element=%u programs vp=%u fp=%u renderTarget=%s projection=%s",
			D3DBuffer_GetBinding(GL_ARRAY_BUFFER_ARB), D3DBuffer_GetBinding(GL_ELEMENT_ARRAY_BUFFER_ARB),
			ARB_GetBoundVertexProgram(), ARB_GetBoundFragmentProgram(), gRenderTarget,
			(D3DGlobal.projectionMatrixStack && D3DGlobal_IsOrthoProjection()) ? "ORTHOGRAPHIC" : "PERSPECTIVE");
		if (D3DGlobal.projectionMatrixStack && D3DGlobal.modelviewMatrixStack) {
			logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE", "matrix projectionHash=%08X modelviewHash=%08X",
				HashBytes(D3DGlobal.projectionMatrixStack->top(), sizeof(D3DXMATRIX)),
				HashBytes(D3DGlobal.modelviewMatrixStack->top(), sizeof(D3DXMATRIX)));
		}
		logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE", "===== End selected draw state =====");
	}

	void WriteCrashText( HANDLE file, const char *text )
	{
		if (file == INVALID_HANDLE_VALUE || !text)
			return;
		DWORD written = 0;
		WriteFile(file, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
	}

	void WriteCrashFormat( HANDLE file, const char *fmt, ... )
	{
		char buffer[1024];
		va_list args;
		va_start(args, fmt);
		_vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
		va_end(args);
		WriteCrashText(file, buffer);
	}

	void DumpCrashEvents( HANDLE file, bool d3dEvents, int maximum )
	{
		const LONG end = gNextEvent;
		int emitted = 0;
		for (LONG sequence = end - 1; sequence >= 0 && sequence >= end - kEventCapacity && emitted < maximum; --sequence) {
			const DiagnosticEvent& event = gEvents[sequence % kEventCapacity];
			if (event.sequence != sequence + 1 || event.d3dEvent != d3dEvents)
				continue;
			WriteCrashFormat(file, "[F:%08llu D:%06llu][%s] %s\r\n",
				static_cast<unsigned long long>(event.frameId),
				static_cast<unsigned long long>(event.drawId), event.category, event.text);
			++emitted;
		}
	}

	bool IsExecutableProtection( DWORD protection )
	{
		if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
			return false;
		switch (protection & 0xFF) {
		case PAGE_EXECUTE:
		case PAGE_EXECUTE_READ:
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return true;
		default:
			return false;
		}
	}

	void WriteCrashAddress( HANDLE file, const char *label, uintptr_t address )
	{
		MEMORY_BASIC_INFORMATION memory = {};
		char module[MAX_PATH] = "<unmapped>";
		uintptr_t moduleOffset = 0;
		if (address != 0 && VirtualQuery(reinterpret_cast<const void *>(address),
			&memory, sizeof(memory)) == sizeof(memory)) {
			moduleOffset = address - reinterpret_cast<uintptr_t>(memory.AllocationBase);
			if (!GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase), module, ARRAYSIZE(module)))
				strcpy_s(module, IsExecutableProtection(memory.Protect) ? "<mapped executable>" : "<mapped data>");
		}
		WriteCrashFormat(file, "%s: %p  %s+0x%IX\r\n", label,
			reinterpret_cast<const void *>(address), module, moduleOffset);
	}

	void DumpCrashContext( HANDLE file, EXCEPTION_POINTERS *exceptionInfo )
	{
		if (!exceptionInfo || !exceptionInfo->ContextRecord) {
			WriteCrashText(file, "CPU context unavailable.\r\n");
			return;
		}

		const CONTEXT *context = exceptionInfo->ContextRecord;
		WriteCrashText(file, "\r\nCPU context:\r\n");
#if defined(_M_IX86)
		WriteCrashFormat(file,
			"EAX=%08X EBX=%08X ECX=%08X EDX=%08X\r\n"
			"ESI=%08X EDI=%08X EBP=%08X ESP=%08X\r\n"
			"EIP=%08X EFlags=%08X\r\n",
			context->Eax, context->Ebx, context->Ecx, context->Edx,
			context->Esi, context->Edi, context->Ebp, context->Esp,
			context->Eip, context->EFlags);
		WriteCrashAddress(file, "Instruction", context->Eip);

		DWORD stackWords[64] = {};
		SIZE_T bytesRead = 0;
		if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(context->Esp),
			stackWords, sizeof(stackWords), &bytesRead) || bytesRead == 0) {
			WriteCrashText(file, "Stack memory unavailable.\r\n");
			return;
		}
		const size_t wordCount = bytesRead / sizeof(stackWords[0]);
		WriteCrashFormat(file, "\r\nRaw stack from ESP (%u DWORDs):\r\n", static_cast<unsigned int>(wordCount));
		for (size_t i = 0; i < wordCount; i += 4) {
			WriteCrashFormat(file, "  ESP+%03X: %08X %08X %08X %08X\r\n",
				static_cast<unsigned int>(i * sizeof(DWORD)), stackWords[i],
				i + 1 < wordCount ? stackWords[i + 1] : 0,
				i + 2 < wordCount ? stackWords[i + 2] : 0,
				i + 3 < wordCount ? stackWords[i + 3] : 0);
		}

		WriteCrashText(file, "\r\nExecutable addresses found on stack:\r\n");
		bool foundExecutable = false;
		for (size_t i = 0; i < wordCount; ++i) {
			MEMORY_BASIC_INFORMATION memory = {};
			const uintptr_t candidate = stackWords[i];
			if (candidate == 0 || VirtualQuery(reinterpret_cast<const void *>(candidate),
				&memory, sizeof(memory)) != sizeof(memory) || !IsExecutableProtection(memory.Protect))
				continue;
			char label[32];
			sprintf_s(label, "ESP+0x%03X", static_cast<unsigned int>(i * sizeof(DWORD)));
			WriteCrashAddress(file, label, candidate);
			foundExecutable = true;
		}
		if (!foundExecutable)
			WriteCrashText(file, "  <none>\r\n");
#elif defined(_M_X64)
		WriteCrashFormat(file,
			"RAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX\r\n"
			"RSI=%016llX RDI=%016llX RBP=%016llX RSP=%016llX\r\n"
			"RIP=%016llX EFlags=%08X\r\n",
			static_cast<unsigned long long>(context->Rax), static_cast<unsigned long long>(context->Rbx),
			static_cast<unsigned long long>(context->Rcx), static_cast<unsigned long long>(context->Rdx),
			static_cast<unsigned long long>(context->Rsi), static_cast<unsigned long long>(context->Rdi),
			static_cast<unsigned long long>(context->Rbp), static_cast<unsigned long long>(context->Rsp),
			static_cast<unsigned long long>(context->Rip), context->EFlags);
		WriteCrashAddress(file, "Instruction", static_cast<uintptr_t>(context->Rip));
#else
		WriteCrashText(file, "Register dump is unavailable for this architecture.\r\n");
#endif
	}

	LONG WINAPI QGL_UnhandledExceptionFilter( EXCEPTION_POINTERS *exceptionInfo )
	{
		SYSTEMTIME time = {};
		GetLocalTime(&time);
		char filename[MAX_PATH];
		sprintf_s(filename, "QindieGL-crash-%04u%02u%02u-%02u%02u%02u.txt",
			time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
		HANDLE file = CreateFileA(filename, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return EXCEPTION_CONTINUE_SEARCH;

		const DWORD code = exceptionInfo && exceptionInfo->ExceptionRecord
			? exceptionInfo->ExceptionRecord->ExceptionCode : 0;
		const void *address = exceptionInfo && exceptionInfo->ExceptionRecord
			? exceptionInfo->ExceptionRecord->ExceptionAddress : nullptr;
		char module[MAX_PATH] = "<unknown>";
		MEMORY_BASIC_INFORMATION memory = {};
		if (address && VirtualQuery(address, &memory, sizeof(memory)) == sizeof(memory)) {
			GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase), module, ARRAYSIZE(module));
		}

		WriteCrashText(file, "===== QindieGL Crash Diagnostic =====\r\n");
		WriteCrashFormat(file, "Exception code: 0x%08X\r\nFault address: %p\r\nModule: %s\r\nThread ID: %u\r\n",
			code, address, module, GetCurrentThreadId());
		if (exceptionInfo && exceptionInfo->ExceptionRecord) {
			const EXCEPTION_RECORD *record = exceptionInfo->ExceptionRecord;
			WriteCrashFormat(file, "Exception parameters: %u\r\n", record->NumberParameters);
			for (DWORD i = 0; i < record->NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; ++i)
				WriteCrashFormat(file, "  [%u] 0x%IX\r\n", i, static_cast<uintptr_t>(record->ExceptionInformation[i]));
		}
		WriteCrashFormat(file, "Frame: %llu\r\nDraw: %llu\r\nLast GL error source: %s\r\nRender target: %s\r\n",
			static_cast<unsigned long long>(gDiagnostics.frameId),
			static_cast<unsigned long long>(gDiagnostics.drawId), gLastErrorSource, gRenderTarget);
		WriteCrashFormat(file, "Active buffers: %s\r\nActive textures: %s\r\nActive ARB programs: %s\r\nProjection: %s\r\n",
			gActiveBuffers, gActiveTextures, gActivePrograms, gProjectionState);
		DumpCrashContext(file, exceptionInfo);
		WriteCrashText(file, "\r\nLast important GL/WGL events (newest first):\r\n");
		DumpCrashEvents(file, false, 100);
		WriteCrashText(file, "\r\nLast D3D calls/state transitions (newest first):\r\n");
		DumpCrashEvents(file, true, 50);
		WriteCrashText(file, "======================================\r\n");
		CloseHandle(file);
		return EXCEPTION_CONTINUE_SEARCH;
	}

	void DumpCountMap( const char *emptyText, const std::map<std::string, uint64_t>& values )
	{
		if (values.empty()) {
			logPrintf("  %s\n", emptyText);
			return;
		}
		for (const auto& value : values)
			logPrintf("  %s: %llu\n", value.first.c_str(), static_cast<unsigned long long>(value.second));
	}
}

void QGL_DiagnosticsInitialize()
{
	gDiagnostics = {};
	gDiagnostics.debugMaxDrawCall = -1;
	gDiagnostics.debugDumpFrame = -1;
	gDiagnostics.debugDumpDraw = -1;
	gDiagnostics.crashDiagnostics = true;
	gDiagnostics.initialized = true;
	gNextEvent = 0;
	strcpy_s(gRenderTarget, "MAIN");
	strcpy_s(gLastErrorSource, "<none>");
	strcpy_s(gActiveBuffers, "array=0 element=0");
	strcpy_s(gActivePrograms, "vp=0 fp=0");
	strcpy_s(gActiveTextures, "none");
	strcpy_s(gProjectionState, "unavailable");
	gD3DFailures.clear();
	gUnsupportedEnums.clear();
	gYAEWorldDrawStates.clear();
	gYAEDumpedTextures.clear();
	gPreviousExceptionFilter = SetUnhandledExceptionFilter(QGL_UnhandledExceptionFilter);
	gExceptionFilterInstalled = true;
	QGL_DiagnosticsRecordEvent(false, "LIFECYCLE", "diagnostics initialized");
}

void QGL_DiagnosticsShutdown()
{
	if (!gDiagnostics.initialized)
		return;
	if (gExceptionFilterInstalled)
		SetUnhandledExceptionFilter(gPreviousExceptionFilter);
	gPreviousExceptionFilter = nullptr;
	gExceptionFilterInstalled = false;
	gDiagnostics.initialized = false;
}

void QGL_DiagnosticsConfigure( int crashDiagnostics, int debugMaxDrawCall,
	int debugDumpFrame, int debugDumpDraw )
{
	gDiagnostics.crashDiagnostics = crashDiagnostics != 0;
	gDiagnostics.debugMaxDrawCall = debugMaxDrawCall;
	gDiagnostics.debugDumpFrame = debugDumpFrame;
	gDiagnostics.debugDumpDraw = debugDumpDraw;
	if (!gDiagnostics.crashDiagnostics && gExceptionFilterInstalled) {
		SetUnhandledExceptionFilter(gPreviousExceptionFilter);
		gExceptionFilterInstalled = false;
	} else if (gDiagnostics.crashDiagnostics && !gExceptionFilterInstalled) {
		gPreviousExceptionFilter = SetUnhandledExceptionFilter(QGL_UnhandledExceptionFilter);
		gExceptionFilterInstalled = true;
	}
	logPrintfLevel(QGL_LOG_INFO, "DIAGNOSTICS",
		"configured LogLevel=%d CrashDiagnostics=%d DebugMaxDrawCall=%d DebugDumpFrame=%d DebugDumpDraw=%d",
		logGetLevel(), crashDiagnostics, debugMaxDrawCall, debugDumpFrame, debugDumpDraw);
}

uint64_t QGL_DiagnosticsGetFrameId()
{
	return gDiagnostics.frameId;
}

uint64_t QGL_DiagnosticsGetDrawId()
{
	return gDiagnostics.drawId;
}

bool QGL_DiagnosticsBeginDraw( const char *api, unsigned int mode, int count,
	int first, unsigned int indexType, const void *indices )
{
	++gDiagnostics.drawId;
	++gDiagnostics.drawsSubmitted;
	QGL_DiagnosticsRecordEvent(false, "GL_DRAW", "%s mode=%s(0x%X) count=%d first=%d type=0x%X indices=%p",
		api ? api : "<unknown>", GLModeName(mode), mode, count, first, indexType, indices);
	logPrintfLevel(QGL_LOG_TRACE, "GL_DRAW", "%s mode=%s(0x%X) count=%d first=%d type=0x%X indices=%p",
		api ? api : "<unknown>", GLModeName(mode), mode, count, first, indexType, indices);

	SnapshotState();
	QGL_ViewDiagnosticsOnDraw(gDiagnostics.frameId, gDiagnostics.drawId);
	CensusYAEWorldDraw(api ? api : "<unknown>", mode, count, first, indexType, indices);
	TraceYAEPostEffectDraw(api ? api : "<unknown>", mode, count, first, indexType, indices);
	if (ProgramHistoryActive()) {
		const float drawValues[4] = { static_cast<float>(count),
			static_cast<float>(D3DState.EnableState.vertexProgramEnabled),
			static_cast<float>(D3DState.EnableState.fragmentProgramEnabled), 0.0f };
		QGL_DiagnosticsRecordProgramOp('D', 0, ARB_GetBoundVertexProgram(),
			static_cast<int>(ARB_GetBoundFragmentProgram()), drawValues);
	}
	ProbeYAEProgramFog(api ? api : "<unknown>", count, first, indexType, indices);
	if (gDiagnostics.debugDumpDraw >= 0
		&& static_cast<int>(gDiagnostics.frameId) == gDiagnostics.debugDumpFrame
		&& static_cast<int>(gDiagnostics.drawId) == gDiagnostics.debugDumpDraw) {
		DumpSelectedDrawState(api ? api : "<unknown>", mode, count, first, indexType, indices);
	}

	if (gDiagnostics.debugMaxDrawCall >= 0
		&& static_cast<int>(gDiagnostics.drawId) > gDiagnostics.debugMaxDrawCall) {
		++gDiagnostics.drawsSkipped;
		logPrintfLevel(QGL_LOG_DEBUG, "GL_DRAW", "draw skipped by DebugMaxDrawCall=%d",
			gDiagnostics.debugMaxDrawCall);
		return false;
	}
	if (D3DState.EnableState.scissorEnabled && D3DState.ScissorState.empty) {
		++gDiagnostics.drawsSkipped;
		logPrintfLevel(QGL_LOG_TRACE, "GL_DRAW", "draw fully rejected by empty scissor box");
		return false;
	}
	return true;
}

void QGL_DiagnosticsBeginPresent()
{
	gPerformance.presentStart = PerformanceNow();
}

void QGL_DiagnosticsRecordVertexUpload( uint32_t vertices, uint32_t vertexBytes, uint32_t indexBytes )
{
	gPerformance.frameVertices += vertices;
	gPerformance.frameVertexBytes += vertexBytes;
	gPerformance.frameIndexBytes += indexBytes;
}

QGLDrawTimer::QGLDrawTimer() : m_start( 0 )
{
	if (gPerformance.drawTimerDepth++ == 0)
		m_start = PerformanceNow();
}

QGLDrawTimer::~QGLDrawTimer()
{
	if (--gPerformance.drawTimerDepth == 0 && m_start)
		gPerformance.frameDrawTicks += PerformanceNow() - m_start;
}

void QGL_DiagnosticsRecordProgramOp( char op, unsigned int target, unsigned int program,
	int index, const float *values )
{
	if (!ProgramHistoryActive())
		return;
	ProgramHistoryEntry& entry = gProgramHistory[gProgramHistoryNext % kProgramHistoryCapacity];
	++gProgramHistoryNext;
	entry.frame = static_cast<uint32_t>(gDiagnostics.frameId);
	entry.draw = static_cast<uint32_t>(gDiagnostics.drawId);
	entry.op = op;
	entry.target = target;
	entry.program = program;
	entry.index = index;
	if (values)
		memcpy(entry.values, values, sizeof(entry.values));
	else
		memset(entry.values, 0, sizeof(entry.values));
}

void QGL_DiagnosticsAfterDraw()
{
	if (gYAEPostEffectAfterDumped || !D3DGlobal.settings.game.yaeFallbackCompatibility ||
		!D3DState.EnableState.fragmentProgramEnabled || ARB_GetBoundFragmentProgram() != 8)
		return;

	gYAEPostEffectAfterDumped = true;
	_mkdir("QindieGL-dump");
	_mkdir("QindieGL-dump\\textures");
	LPDIRECT3DSURFACE9 renderTarget = nullptr;
	LPDIRECT3DSURFACE9 systemCopy = nullptr;
	HRESULT result = D3DGlobal.pDevice->GetRenderTarget(0, &renderTarget);
	if (SUCCEEDED(result) && renderTarget) {
		D3DSURFACE_DESC desc = {};
		result = renderTarget->GetDesc(&desc);
		if (SUCCEEDED(result))
			result = D3DGlobal.pDevice->CreateOffscreenPlainSurface(desc.Width, desc.Height,
				desc.Format, D3DPOOL_SYSTEMMEM, &systemCopy, nullptr);
		if (SUCCEEDED(result))
			result = D3DGlobal.pDevice->GetRenderTargetData(renderTarget, systemCopy);
		if (SUCCEEDED(result))
			result = D3DXSaveSurfaceToFileA(
				"QindieGL-dump\\textures\\yae_post_result.png", D3DXIFF_PNG,
				systemCopy, nullptr, nullptr);
	}
	if (systemCopy) systemCopy->Release();
	if (renderTarget) renderTarget->Release();
	logPrintfLevel(QGL_LOG_INFO, "YAE_POST_EFFECT",
		"result dump result=0x%08X file=QindieGL-dump\\textures\\yae_post_result.png", result);
}

void QGL_DiagnosticsEndFrame( long presentResult )
{
	QGL_DiagnosticsRecordEvent(true, "PRESENT", "Present hr=0x%08X %s",
		static_cast<unsigned int>(presentResult), DXGetErrorString(presentResult));
	logPrintfLevel(QGL_LOG_TRACE, "PRESENT", "hr=0x%08X %s draws=%llu",
		static_cast<unsigned int>(presentResult), DXGetErrorString(presentResult),
		static_cast<unsigned long long>(gDiagnostics.drawId));
	// Only a successful Present is a real presentation boundary. In particular,
	// D3DERR_WASSTILLDRAWING from the DONOTWAIT path must not fabricate a frame.
	if (SUCCEEDED(presentResult)) {
		const bool worldFrame = QGL_ViewDiagnosticsOnFrameEnd(gDiagnostics.frameId);
		RecordFramePerformance(worldFrame, gDiagnostics.drawId);
		++gDiagnostics.framesPresented;
		++gDiagnostics.frameId;
		gDiagnostics.drawId = 0;
	}
}

void QGL_DiagnosticsRecordEvent( bool d3dEvent, const char *category, const char *fmt, ... )
{
	if (!gDiagnostics.initialized || !fmt)
		return;
	const LONG sequence = InterlockedIncrement(&gNextEvent);
	DiagnosticEvent& event = gEvents[(sequence - 1) % kEventCapacity];
	event.sequence = 0;
	event.d3dEvent = d3dEvent;
	event.frameId = gDiagnostics.frameId;
	event.drawId = gDiagnostics.drawId;
	strncpy_s(event.category, category ? category : "GENERAL", _TRUNCATE);
	va_list args;
	va_start(args, fmt);
	_vsnprintf_s(event.text, sizeof(event.text), _TRUNCATE, fmt, args);
	va_end(args);
	MemoryBarrier();
	event.sequence = sequence;
}

void QGL_DiagnosticsRecordD3DFailure( const char *call, long result )
{
	++gDiagnostics.failedD3DCalls;
	char key[192];
	sprintf_s(key, "%s hr=0x%08X %s", call ? call : "<unknown>",
		static_cast<unsigned int>(result), DXGetErrorString(result));
	++gD3DFailures[key];
	QGL_DiagnosticsRecordEvent(true, "D3D_ERROR", "%s", key);
	logPrintfLevel(QGL_LOG_ERROR, "D3D_ERROR", "%s", key);
}

void QGL_DiagnosticsRecordDeviceReset( long result )
{
	++gDiagnostics.deviceResets;
	QGL_DiagnosticsRecordEvent(true, "DEVICE_RESET", "Reset hr=0x%08X %s",
		static_cast<unsigned int>(result), DXGetErrorString(result));
	if (FAILED(result))
		QGL_DiagnosticsRecordD3DFailure("IDirect3DDevice9::Reset", result);
}

void QGL_DiagnosticsRecordPBufferCreated()
{
	++gDiagnostics.pBuffersCreated;
}

void QGL_DiagnosticsRecordARBProgramUpload( bool compiled, bool failed )
{
	++gDiagnostics.arbProgramsUploaded;
	if (compiled) ++gDiagnostics.arbProgramsCompiled;
	if (failed) ++gDiagnostics.arbProgramFailures;
}

void QGL_DiagnosticsRecordVBOCreated()
{
	++gDiagnostics.vbosCreated;
}

void QGL_DiagnosticsRecordVBOBytes( int64_t delta )
{
	gDiagnostics.currentVBOBytes += delta;
	if (gDiagnostics.currentVBOBytes < 0)
		gDiagnostics.currentVBOBytes = 0;
	if (static_cast<uint64_t>(gDiagnostics.currentVBOBytes) > gDiagnostics.peakVBOBytes)
		gDiagnostics.peakVBOBytes = static_cast<uint64_t>(gDiagnostics.currentVBOBytes);
}

void QGL_DiagnosticsSetRenderTarget( const char *name )
{
	strncpy_s(gRenderTarget, name ? name : "<unknown>", _TRUNCATE);
	QGL_DiagnosticsRecordEvent(true, "RENDER_TARGET", "active=%s", gRenderTarget);
}

void QGL_SetErrorImpl( long error, const char *source )
{
	D3DGlobal.lastError = error;
	if (SUCCEEDED(error)) {
		strcpy_s(gLastErrorSource, "<none>");
		return;
	}
	strncpy_s(gLastErrorSource, source ? source : "<unknown>", _TRUNCATE);
	if (error == E_INVALID_ENUM)
		++gUnsupportedEnums[gLastErrorSource];
	QGL_DiagnosticsRecordEvent(false, "GL_ERROR", "source=%s internal=0x%08X mapsTo=%s",
		gLastErrorSource, static_cast<unsigned int>(error), GLErrorName(error));
	logPrintfLevel(QGL_LOG_DEBUG, "GL_ERROR", "source=%s internal=0x%08X mapsTo=%s",
		gLastErrorSource, static_cast<unsigned int>(error), GLErrorName(error));
}

void QGL_DiagnosticsDumpCapabilityReport()
{
	if (!logIsEnabled(QGL_LOG_INFO))
		return;
	char executable[MAX_PATH] = "<unknown>";
	GetModuleFileNameA(nullptr, executable, ARRAYSIZE(executable));

	logPrintf("===== QindieGL Capability Report =====\n");
	logPrintf("Game executable: %s\n", executable);
#if defined(_M_IX86)
	logPrintf("Architecture: x86\n");
#elif defined(_M_AMD64)
	logPrintf("Architecture: x64\n");
#else
	logPrintf("Architecture: unknown\n");
#endif
	logPrintf("Reported OpenGL identity:\n");
	logPrintf("  GL_VENDOR: %s\n", WRAPPER_GL_VENDOR_STRING);
	logPrintf("  GL_RENDERER: %s\n", D3DGlobal.szRendererName ? D3DGlobal.szRendererName : "<unknown>");
	logPrintf("  GL_VERSION: %s\n", WRAPPER_GL_VERSION_STRING);
	logPrintf("D3D adapter: %s\n", D3DGlobal.szRendererName ? D3DGlobal.szRendererName : "<unknown>");
	logPrintf("D3D9 caps:\n");
	logPrintf("  VertexShaderVersion: 0x%08X\n", D3DGlobal.hD3DCaps.VertexShaderVersion);
	logPrintf("  PixelShaderVersion: 0x%08X\n", D3DGlobal.hD3DCaps.PixelShaderVersion);
	logPrintf("  MaxTextureWidth: %u\n", D3DGlobal.hD3DCaps.MaxTextureWidth);
	logPrintf("  MaxTextureHeight: %u\n", D3DGlobal.hD3DCaps.MaxTextureHeight);
	logPrintf("  MaxSimultaneousTextures: %u\n", D3DGlobal.hD3DCaps.MaxSimultaneousTextures);
	logPrintf("  MaxStreams: %u\n", D3DGlobal.hD3DCaps.MaxStreams);
	logPrintf("  MaxAnisotropy: %u\n", D3DGlobal.hD3DCaps.MaxAnisotropy);
	logPrintf("QindieGL settings:\n");
	logPrintf("  LogLevel: %d\n", logGetLevel());
	logPrintf("  ProjectionFix: %u\n", D3DGlobal.settings.projectionFix);
	logPrintf("  DrawCallFastPath: %u\n", D3DGlobal.settings.drawcallFastPath);
	logPrintf("  EnableARBProgramsStub: %u\n", D3DGlobal.settings.enableARBProgramsStub);
	logPrintf("  YAEFallbackCompatibility: %u\n", D3DGlobal.settings.game.yaeFallbackCompatibility);
	logPrintf("  YAECompileARBPrograms: %u\n", D3DGlobal.settings.game.yaeCompileARBPrograms);
	logPrintf("  YAEEyeDistanceFog: %u\n", D3DGlobal.settings.game.yaeEyeDistanceFog);
	logPrintf("  MultiSample: %u\n", D3DGlobal.settings.multisample);
	logPrintf("  CrashDiagnostics: %u\n", D3DGlobal.settings.crashDiagnostics);
	logPrintf("  DebugMaxDrawCall: %d\n", D3DGlobal.settings.debugMaxDrawCall);
	logPrintf("  DebugDumpFrame: %d\n", D3DGlobal.settings.debugDumpFrame);
	logPrintf("  DebugDumpDraw: %d\n", D3DGlobal.settings.debugDumpDraw);
	const char *glNames[] = {
		"GL_ARB_vertex_buffer_object", "GL_ARB_vertex_program", "GL_ARB_fragment_program",
		"GL_ARB_depth_texture", "GL_ARB_shadow", "GL_EXT_texture_rectangle",
		"GL_SGIS_generate_mipmap"
	};
	logPrintf("Advertised important GL extensions:\n");
	for (const char *name : glNames)
		logPrintf("  %s = %s\n", name, HasExtension(D3DGlobal.szExtensions, name) ? "YES" : "NO");
	const char *wglNames[] = { "WGL_ARB_pbuffer", "WGL_ARB_render_texture", "WGL_ARB_pixel_format" };
	logPrintf("Advertised important WGL extensions:\n");
	for (const char *name : wglNames)
		logPrintf("  %s = %s\n", name, HasExtension(D3DGlobal.szWExtensions, name) ? "YES" : "NO");
	logPrintf("======================================\n");
}

void QGL_DiagnosticsDumpSessionSummary()
{
	if (gDiagnostics.summaryDumped || !logIsEnabled(QGL_LOG_INFO))
		return;
	gDiagnostics.summaryDumped = true;
	logPrintf("===== QindieGL Session Summary =====\n");
	logPrintf("Frames: %llu\n", static_cast<unsigned long long>(gDiagnostics.framesPresented));
	logPrintf("Draw calls: %llu\n", static_cast<unsigned long long>(gDiagnostics.drawsSubmitted));
	logPrintf("Draw calls skipped: %llu\n", static_cast<unsigned long long>(gDiagnostics.drawsSkipped));
	D3DExtension_DumpProcSummary();
	logPrintf("Unsupported enums by originating function:\n");
	DumpCountMap("none", gUnsupportedEnums);
	logPrintf("Failed D3D calls:\n");
	DumpCountMap("none", gD3DFailures);
	logPrintf("Device resets: %llu\n", static_cast<unsigned long long>(gDiagnostics.deviceResets));
	logPrintf("PBuffers created: %llu\n", static_cast<unsigned long long>(gDiagnostics.pBuffersCreated));
	logPrintf("ARB programs uploaded: %llu\n", static_cast<unsigned long long>(gDiagnostics.arbProgramsUploaded));
	logPrintf("ARB programs compiled: %llu\n", static_cast<unsigned long long>(gDiagnostics.arbProgramsCompiled));
	logPrintf("ARB program compilation failures: %llu\n", static_cast<unsigned long long>(gDiagnostics.arbProgramFailures));
	logPrintf("VBOs created: %llu\n", static_cast<unsigned long long>(gDiagnostics.vbosCreated));
	logPrintf("Peak VBO bytes: %llu\n", static_cast<unsigned long long>(gDiagnostics.peakVBOBytes));
	QGL_ViewDiagnosticsDumpSummary();
	DumpPerformanceSummary();
	logPrintf("====================================\n");
}
