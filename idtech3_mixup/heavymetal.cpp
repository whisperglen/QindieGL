
#include "heavy_metal.h"
#include "surface_sorting.h"
#include <string.h>
//#define NO_GL_PROTOTYPES
#include "Windows.h"
#include "gl_headers/gl.h"
#include "gl_headers/glext.h"


static cvar_t* r_gpu_uv_transform = 0;

static int VertexLightingRemoveLightmaps(void);
extern void WINAPI glActiveTexture(GLenum texture);

void hm_init()
{

}

void hm_deinit()
{

}

void hm_frame_ended()
{

}

#define	MAX_QPATH		256

#define MAX_SHADER_STAGES 8

#define NUM_TEXTURE_BUNDLES 2

#define	MAX_IMAGE_ANIMATIONS	64
#define BUNDLE_ANIMATE_ONCE		1

#define	LIGHTMAP_NONE		-1

typedef struct image_s image_t;
typedef struct texModInfo_s texModInfo_t;
typedef enum genFunc_e genFunc_t;

typedef enum {
	SS_BAD,
	SS_PORTAL,			// mirrors, portals, viewscreens
	SS_PORTALSKY,
	SS_ENVIRONMENT,		// sky box
	SS_OPAQUE,			// opaque

	SS_DECAL,			// scorch marks, etc.
	SS_SEE_THROUGH,		// ladders, grates, grills that may have small blended edges
	// in addition to alpha test
	SS_BANNER,

	SS_UNDERWATER,		// for items that should be drawn in front of the water plane

	SS_BLEND0,			// regular transparency and filters
	SS_BLEND1,			// generally only used for additive type effects
	SS_BLEND2,
	SS_BLEND3,

	SS_BLEND6,
	SS_STENCIL_SHADOW,
	SS_ALMOST_NEAREST,	// gun smoke puffs

	SS_NEAREST			// blood blobs
} shaderSort_t;

typedef enum {
	AGEN_IDENTITY,
	AGEN_SKIP,
	AGEN_ENTITY,
	AGEN_ONE_MINUS_ENTITY,
	AGEN_VERTEX,
	AGEN_ONE_MINUS_VERTEX,
	AGEN_LIGHTING_SPECULAR,
	AGEN_WAVEFORM,
	AGEN_PORTAL,
	AGEN_NOISE,
	AGEN_DOT,
	AGEN_ONE_MINUS_DOT,
	AGEN_CONSTANT,
	AGEN_GLOBAL_ALPHA,
	AGEN_SKYALPHA,
	AGEN_ONE_MINUS_SKYALPHA,
	AGEN_SCOORD,
	AGEN_TCOORD,
	AGEN_DIST_FADE,
	AGEN_ONE_MINUS_DIST_FADE,
	AGEN_TIKI_DIST_FADE,
	AGEN_ONE_MINUS_TIKI_DIST_FADE,
	AGEN_DOT_VIEW,
	AGEN_ONE_MINUS_DOT_VIEW,
	AGEN_HEIGHT_FADE,
} alphaGen_t;

typedef enum {
	CGEN_BAD,
	CGEN_IDENTITY_LIGHTING,	// tr.identityLight
	CGEN_IDENTITY,			// always (1,1,1,1)
	CGEN_ENTITY,			// grabbed from entity's modulate field
	CGEN_ONE_MINUS_ENTITY,	// grabbed from 1 - entity.modulate
	CGEN_EXACT_VERTEX,		// tess.vertexColors
	CGEN_VERTEX,			// tess.vertexColors * tr.identityLight
	CGEN_ONE_MINUS_VERTEX,
	CGEN_WAVEFORM,			// programmatically generated
	CGEN_MULTIPLY_BY_WAVEFORM,
	CGEN_LIGHTING_GRID,
	CGEN_LIGHTING_SPHERICAL,
	CGEN_CONSTANT,
	CGEN_NOISE,
	CGEN_GLOBAL_COLOR,
	CGEN_STATIC,
	CGEN_SCOORD,
	CGEN_TCOORD,
	CGEN_DOT,
	CGEN_ONE_MINUS_DOT
} colorGen_t;

typedef enum {
	TCGEN_BAD,
	TCGEN_IDENTITY,			// clear to 0,0
	TCGEN_LIGHTMAP,
	TCGEN_TEXTURE,
	TCGEN_ENVIRONMENT_MAPPED,
	TCGEN_VECTOR,			// S and T from world coordinates
	TCGEN_ENVIRONMENT_MAPPED2,
	TCGEN_SUN_REFLECTION,
	TCGEN_FOG
} texCoordGen_t;

typedef struct {
	image_t			*image[MAX_IMAGE_ANIMATIONS];
	int				numImageAnimations;
	float			imageAnimationSpeed;
	float			imageAnimationPhase;

	texCoordGen_t	tcGen;
	vec3_t			tcGenVectors[2];

	int				numTexMods;
	texModInfo_t	*texMods;

	int				videoMapHandle;
	qboolean		isLightmap;
	qboolean		vertexLightmap;
	qboolean		isVideoMap;
	int				flags;
} textureBundle_t;

typedef struct {
	genFunc_t	func;

	float base;
	float amplitude;
	float phase;
	float frequency;
} waveForm_t;

typedef struct {
	qboolean		active;
	qboolean		hasNormalMap;
	
	textureBundle_t	bundle[NUM_TEXTURE_BUNDLES];
	image_t			*normalMap;
	int				multitextureEnv;		// 0, GL_MODULATE, GL_ADD (FIXME: put in stage)

	waveForm_t		rgbWave;
	colorGen_t		rgbGen;

	waveForm_t		alphaWave;
    alphaGen_t		alphaGen;

    unsigned		stateBits;					// GLS_xxxx mask


	qboolean		noMipMaps;
    qboolean		noPicMip;
    qboolean		force32bit;

	float			alphaMin;
	float			alphaMax;
	vec3_t			specOrigin;

    byte			colorConst[4];			// for CGEN_CONST and AGEN_CONST
	byte			alphaConst;
	byte			alphaConstMin;
} shaderStage_t;

typedef struct shader_s {
	char		name[MAX_QPATH];		// game path, including extension
	int			lightmapIndex;			// for a shader to match, both name and lightmapIndex must match

	int			index;					// this shader == tr.shaders[index]
	int			sortedIndex;			// this shader == tr.sortedShaders[sortedIndex]

	float		sort;					// lower numbered shaders draw before higher numbered
} shader_t;

static	shaderStage_t	*stages = (shaderStage_t*)0x0072f9c0;
static	shader_t		*p_shader = (shader_t*)0x00735328;
#define shader (*p_shader)

#define		GLS_SRCBLEND_BITS					0x0000000f
#define		GLS_DSTBLEND_BITS					0x000000f0
#define GLS_DEPTHMASK_TRUE						0x00000100

static int VertexLightingRemoveLightmaps(void)
{
	int stage, nextopenstage;
	int finalstagenum = 0;

	// Loop through all stages to filter out lightmaps and fix vertex coloring
	for (stage = 0, nextopenstage = 0; stage < MAX_SHADER_STAGES; stage++) {
		shaderStage_t* pStage = &stages[stage];

		if (!pStage->active) {
			break;
		}

		// 1. If it's a lightmap, skip it entirely (this removes the lightmap)
		if (pStage->bundle[0].isLightmap || pStage->bundle[0].tcGen == TCGEN_LIGHTMAP) {
			continue;
		}

		// 2. If it's NOT a lightmap, copy it to the next available open slot.
		// This naturally preserves terrain blending stages and Surface Sprites.
		if (nextopenstage != stage) {
			stages[nextopenstage] = *pStage;
		}

		// 3. Force the foundational stage of opaque surfaces to actually be opaque.
		// Without this, diffuse maps that were originally blending ONTO a lightmap 
		// will try to blend with the void, causing transparent walls.
		if (shader.sort == SS_OPAQUE && nextopenstage == 0) {
			stages[0].stateBits &= ~(GLS_DSTBLEND_BITS | GLS_SRCBLEND_BITS);
			stages[0].stateBits |= GLS_DEPTHMASK_TRUE;
			stages[0].alphaGen = AGEN_SKIP;
		}

		// 4. Fix the color generation (rgbGen).
		// Since we removed the lightmap, the diffuse textures will be fullbright 
		// if left at CGEN_IDENTITY. We need them to catch the CPU vertex lighting.
		if (shader.sort == SS_OPAQUE) {
			if (stages[nextopenstage].rgbGen == CGEN_IDENTITY || stages[nextopenstage].rgbGen == CGEN_IDENTITY_LIGHTING || stages[nextopenstage].rgbGen == CGEN_LIGHTING_GRID) {
				if (shader.lightmapIndex == LIGHTMAP_NONE) {
					stages[nextopenstage].rgbGen = CGEN_LIGHTING_GRID;
				}
				else {
					stages[nextopenstage].rgbGen = CGEN_EXACT_VERTEX;
				}
			}
		}

		nextopenstage++;
		finalstagenum++;
	}

	// 5. Clean up any leftover garbage stages at the end of the array
	for (stage = finalstagenum; stage < MAX_SHADER_STAGES; stage++) {
		memset(&stages[stage], 0, sizeof(stages[stage]));
	}

	return finalstagenum;
}

static void ComputeTexCoords_ClearTransforms()
{
	if (r_gpu_uv_transform->integer)
	{
		GLint matrixMode = 0;
		GLint activeTexture = -1;
		glGetIntegerv(GL_MATRIX_MODE, &matrixMode);
		glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);

		glMatrixMode(GL_TEXTURE);

		for (int i = 0; i < NUM_TEXTURE_BUNDLES; i++)
		{
			glActiveTexture(GL_TEXTURE0_ARB + i);
			glLoadIdentity();
		}

		glMatrixMode(matrixMode);
		glActiveTexture(activeTexture);
	}
}

static float g_texcoords_mats[NUM_TEXTURE_BUNDLES][16];
static int g_texcoords_index = 0;

void RB_MultiplyTextureMatrix(float* mat)
{
	float* dest = g_texcoords_mats[g_texcoords_index];
	const float* A = mat;
	float B[16];

	memcpy(B, dest, sizeof(B));

	int i, j;
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			dest[i * 4 + j] =
				A[0 * 4 + j] * B[i * 4 + 0] +
				A[1 * 4 + j] * B[i * 4 + 1] +
				A[2 * 4 + j] * B[i * 4 + 2] +
				A[3 * 4 + j] * B[i * 4 + 3];
		}
	}
}

/*
================
MatrixIdentity4x4

Clears a 16-element column-major matrix to the Identity matrix.
================
*/
static void RB_TextureMatrixClear()
{
	float* m = g_texcoords_mats[g_texcoords_index];
	m[0] = 1.0f; m[4] = 0.0f; m[8] = 0.0f; m[12] = 0.0f;
	m[1] = 0.0f; m[5] = 1.0f; m[9] = 0.0f; m[13] = 0.0f;
	m[2] = 0.0f; m[6] = 0.0f; m[10] = 1.0f; m[14] = 0.0f;
	m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f; m[15] = 1.0f;
}