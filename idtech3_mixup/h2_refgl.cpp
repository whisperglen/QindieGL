
#include "hooking.h"
#include <Windows.h>
#include <psapi.h>
#include <stdint.h>
#include "h2_refgl.h"
#include "detours.h"
#define NO_GL_PROTOTYPES
#include "gl_headers/gl.h"
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_utils.hpp"
#include "d3d_state.hpp"
#include "d3d_helpers.hpp"
#include "rmx_gen.h"
#include "rmx_light.h"
#include <map>
#include <string>
#include <vector>
#include <list>
#include "fnvhash/fnv.h"
#include <intrin.h>

typedef float vec_t;
typedef float vec3_t[3];
typedef float matrix3_t[3][3];

typedef unsigned short ushort;
typedef unsigned uint;
typedef unsigned long ulong;
typedef unsigned char byte;

typedef enum { qfalse, qtrue } qboolean;

typedef struct cplane_s
{
	vec3_t normal;
	float dist;
	byte type;		// For fast side tests.
	byte signbits;	// signx + (signy << 1) + (signz << 1)
	byte pad[2];
} cplane_t;

#define MAX_QPATH			64

typedef enum
{
	it_skin		= 1,
	it_sprite	= 2,
	it_wall		= 4,
	it_pic		= 5,
	it_sky		= 6
} imagetype_t;

typedef struct paletteRGB_s
{
	byte r;
	byte g;
	byte b;
} paletteRGB_t;

typedef struct image_s
{
	struct image_s* next;
	char name[MAX_QPATH];				// Game path, including extension.
	imagetype_t type;
	int width;
	int height;
	int registration_sequence;			// 0 = free
	struct msurface_s* texturechain;	// For sort-by-texture world drawing.
	struct msurface_s* multitexturechain;
	int texnum;							// gl texture binding.
	byte has_alpha;
	byte num_frames;
	struct paletteRGB_s* palette;		// .M8 palette.
} image_t;

typedef struct mtexinfo_s
{
	float vecs[2][4];
	int flags;
	int numframes;
	struct mtexinfo_s* next; // Animation chain
	image_t* image;
} mtexinfo_t;

#define	VERTEXSIZE	7

typedef struct glpoly_s
{
	struct glpoly_s* next;
	struct glpoly_s* chain;
	int numverts;
	int flags;
	float verts[4][VERTEXSIZE]; // Variable sized (xyz s1t1 s2t2)
} glpoly_t;

#define MAXLIGHTMAPS	4

typedef struct msurface_s
{
	int visframe; // Should be drawn when node is crossed

	cplane_t* plane;
	int flags;

	int firstedge; // Look up in model->surfedges[], negative numbers are backwards edges
	int numedges;

	short texturemins[2];
	short extents[2];

	int light_s; // gl lightmap coordinates
	int light_t;

	int dlight_s; // gl lightmap coordinates for dynamic lightmaps
	int dlight_t;

	glpoly_t* polys; // Multiple if warped
	struct msurface_s* texturechain;
	struct msurface_s* lightmapchain;

	mtexinfo_t* texinfo;

	// Lighting info
	int dlightframe;
	int dlightbits;

	int lightmaptexturenum;
	byte styles[MAXLIGHTMAPS];
	float cached_light[MAXLIGHTMAPS]; // Values currently used in lightmap
	byte* samples; // [numstyles * surfsize]
} msurface_t;

typedef struct mnode_s
{
	// Common with leaf
	int contents; // -1, to differentiate from leafs
	int visframe; // Node needs to be traversed if current

	float minmaxs[6]; // For bounding box culling

	struct mnode_s* parent;

	// Node specific
	cplane_t* plane;
	struct mnode_s* children[2];

	ushort firstsurface;
	ushort numsurfaces;
} mnode_t;

typedef struct mleaf_s
{
	// Common with node
	int contents; // Will be a negative contents number
	int visframe; // Node needs to be traversed if current

	float minmaxs[6]; // For bounding box culling

	struct mnode_s* parent;

	// Leaf specific
	int cluster;
	int area;

	msurface_t** firstmarksurface;
	int nummarksurfaces;
} mleaf_t;

typedef enum
{
	mod_bad,
	mod_brush,
	mod_sprite,
	mod_alias,
	mod_unknown,
	mod_fmdl,	// H2
	mod_book	// H2
} modtype_t;

typedef struct
{
	vec3_t mins;
	vec3_t maxs;
	vec3_t origin;	// For sounds or lights
	float radius;
	int headnode;
	int visleafs;	// Not including the solid leaf 0
	int firstface;
	int numfaces;
} mmodel_t;

typedef struct
{
	vec3_t position;
} mvertex_t;

typedef struct
{
	ushort v[2];
	uint cachededgeoffset;
} medge_t;

typedef struct
{
	int numclusters;
	int bitofs[8][2]; // bitofs[numclusters][2]
} dvis_t;

typedef struct
{
	float rgb[3];	// 0.0 - 2.0.
	float white;	// Highest of rgb.
} lightstyle_t;

#define MAX_FRAMES		64

typedef unsigned char undefined;
typedef struct model_s
{
	char name[64];
	int registratin_sequence;
	int type;
	int numframes;
	int flags;
	undefined field5_0x50;
	undefined field6_0x51;
	undefined field7_0x52;
	undefined field8_0x53;
	undefined field9_0x54;
	undefined field10_0x55;
	undefined field11_0x56;
	undefined field12_0x57;
	undefined field13_0x58;
	undefined field14_0x59;
	undefined field15_0x5a;
	undefined field16_0x5b;
	undefined field17_0x5c;
	undefined field18_0x5d;
	undefined field19_0x5e;
	undefined field20_0x5f;
	undefined field21_0x60;
	undefined field22_0x61;
	undefined field23_0x62;
	undefined field24_0x63;
	undefined field25_0x64;
	undefined field26_0x65;
	undefined field27_0x66;
	undefined field28_0x67;
	float radius;
	int clipbox;
	undefined field31_0x70;
	undefined field32_0x71;
	undefined field33_0x72;
	undefined field34_0x73;
	undefined field35_0x74;
	undefined field36_0x75;
	undefined field37_0x76;
	undefined field38_0x77;
	undefined field39_0x78;
	undefined field40_0x79;
	undefined field41_0x7a;
	undefined field42_0x7b;
	undefined field43_0x7c;
	undefined field44_0x7d;
	undefined field45_0x7e;
	undefined field46_0x7f;
	undefined field47_0x80;
	undefined field48_0x81;
	undefined field49_0x82;
	undefined field50_0x83;
	undefined field51_0x84;
	undefined field52_0x85;
	undefined field53_0x86;
	undefined field54_0x87;
	int firstmodelsurface;
	int nummodelsurfaces;
	int lightmap;
	int numsubmodels;
	int *submodels;
	int numplanes;
	int *planes;
	int numleafs;
	int *leafs;
	int numvertexes;
	int *vertexes;
	int numedges;
	int *edges;
	int numnodes;
	int firstnode;
	int *nodes;
	int numtexinfo;
	int *texinfo;
	int numsurfaces;
	msurface_t *surfaces;
	int numsurfedges;
	int *surfedges;
	int nummarksurfaces;
	int **marksurfaces;
	int *vis;
	char *lightdata;
	int *skins[64];
	int *fmodel;
	int *extradata;
	int extradatasize;
}  model_t;

typedef struct cvar_s
{
	char* name;
	char* string;
	char* latched_string; // For CVAR_LATCH vars.
	int flags;
	qboolean modified; // Set each time the cvar is changed.
	float value;
	struct cvar_s* next;
} cvarq2_t;

#define CONTENTS_SOLID			0x00000001

#define SURF_PLANEBACK		2
#define SURF_DRAWSKY		4
#define SURF_DRAWTURB		16

#define SURF_TALL_WALL		0x400

#define SURF_SKY			0x4
#define SURF_WARP			0x8			// Turbulent water warp.
#define SURF_TRANS33		0x10
#define SURF_TRANS66		0x20
#define SURF_FLOWING		0x40

#define PLANE_X			0
#define PLANE_Y			1
#define PLANE_Z			2

#define VectorCopy(a,b)			(b[0]=a[0],b[1]=a[1],b[2]=a[2])

#define PRINT_ALL			0
#define PRINT_DEVELOPER		1		// Only print when "developer 1".
#define PRINT_ALERT			2

#define	EXEC_NOW	0		// don't return until completed
#define	EXEC_INSERT	1		// insert at current position, but don't run yet
#define	EXEC_APPEND	2		// add to end of the command buffer

#define GL_LIGHTMAP_FORMAT GL_RGBA

#define RI_PRINTF_OFF 4
#define RI_ERROR_OFF 3
#define RI_CVAR_GET 5
#define RI_CVAR_SET 7
#define RI_ADDCMD 9
#define RI_EXECTXT 0xd

#define riPRINTF(LVL,...) ((ri_Printf)dp_ri[RI_PRINTF_OFF])( LVL, __VA_ARGS__ )
#define riERROR(CODE,...) ((ri_Error)dp_ri[RI_ERROR_OFF])( CODE, __VA_ARGS__ )
#define riCVAR_GET(NAME,VAL,FLAGS) ((ri_Cvar_Get)dp_ri[RI_CVAR_GET] )(NAME,VAL,FLAGS)
#define riCVAR_SET(NAME,VAL) ((ri_Cvar_Set)dp_ri[RI_CVAR_SET] )(NAME,VAL)
#define riEXEC_TEXT(WHEN,TEXT) ((ri_Cbuf_ExecuteText)dp_ri[RI_EXECTXT])(WHEN,TEXT)
#define riADD_CMD(NAME,FN) ((ri_AddCommand)dp_ri[RI_ADDCMD])(NAME,FN)

#define vecmax(a,m)             ((a) > m ? m : (a))

#define DotProduct(x,y)			(x[0]*y[0]+x[1]*y[1]+x[2]*y[2])
#define VectorSubtract(a,b,c)	(c[0]=a[0]-b[0],c[1]=a[1]-b[1],c[2]=a[2]-b[2])
#define VectorAdd(a,b,c)		(c[0]=a[0]+b[0],c[1]=a[1]+b[1],c[2]=a[2]+b[2])
#define VectorCopy(a,b)			(b[0]=a[0],b[1]=a[1],b[2]=a[2])
#define VectorCopyClamp(a,b,m)	(b[0]=vecmax(a[0],m),b[1]=vecmax(a[1],m),b[2]=vecmax(a[2],m))
#define VectorScale(v,s,o)      ((o)[0]=(v)[0]*(s),(o)[1]=(v)[1]*(s),(o)[2]=(v)[2]*(s))
#define VectorClear(a)			(a[0]=a[1]=a[2]=0)
#define VectorNegate(a,b)		(b[0]=-a[0],b[1]=-a[1],b[2]=-a[2])
#define VectorSet(v, x, y, z)	(v[0]=(x), v[1]=(y), v[2]=(z))
#define VectorMA( v, s, b, o )  ( ( o )[0] = ( v )[0] + ( b )[0] * ( s ),( o )[1] = ( v )[1] + ( b )[1] * ( s ),( o )[2] = ( v )[2] + ( b )[2] * ( s ) )

inline void CrossProduct( const vec3_t v1, const vec3_t v2, vec3_t cross ) {
	cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
	cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
	cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

void VectorNormalize( vec3_t v )
{
	float length, ilength;

	length = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];

	if (length)
	{
#if 1
		_mm_store_ss(&ilength, _mm_rsqrt_ss(_mm_set_ss(length)));
#else
		ilength = 1 / sqrtf(length);
#endif
		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
	}
}

#ifndef M_PI
#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif

#define Q_ftol( f ) _mm_cvt_ss2si( _mm_set1_ps(f) )

extern "C" {
typedef void	(*ri_Printf) (int print_level, const char *str, ...);
typedef void	(*ri_Error) (int code, char *fmt, ...);
typedef cvarq2_t* (*ri_Cvar_Get) (const char *name, const char *value, int flags);
typedef cvarq2_t* (*ri_Cvar_Set)( const char *name, const char *value );
typedef void (*ri_Cbuf_ExecuteText)( int exec_when, const char *text );
typedef void (*ri_AddCommand)(const char* name, void (*cmd)(void));

static intptr_t* dp_ri;// = 0x5fd60

static int* dp_r_visframecount;// = 0x5fe38;
#define r_visframecount (*dp_r_visframecount)
static int* dp_r_framecount;// = 0x5fd20;
#define r_framecount (*dp_r_framecount)
static float* modelorg;// = 0x5fb40;
//static byte* r_worldmodel;// = 0x5fbd0;
static model_t** dp_r_worldmodel;// = 0x5fbd0;
#define r_worldmodel (*dp_r_worldmodel)
#define r_worldmodel_surfaces (*((msurface_t**)((byte*)r_worldmodel + 0xd4)))
static msurface_t** dp_r_alpha_surfaces;// = 0x5fb4c
#define r_alpha_surfaces (*dp_r_alpha_surfaces)
static int *dp_num_sorted_multitextures;// = 0x3da08
#define num_sorted_multitextures (*dp_num_sorted_multitextures)
static byte** dp_r_newrefdef_areabits;// = 0x5fcb8
#define r_newrefdef_areabits (*dp_r_newrefdef_areabits)
static float* dp_r_newrefdef_time;// = 0x5fcb0
#define r_newrefdef_time (*dp_r_newrefdef_time)
static lightstyle_t* r_newrefdef_lightstyles;// = 0x5fcbc
static int* dp_gl_state_lightmap_textures;// = 0x5ff6c
#define gl_state_lightmap_textures (*dp_gl_state_lightmap_textures)
static int* dp_c_brush_polys;// = 0x5fd40
#define c_brush_polys (*dp_c_brush_polys)
float **dp_s_lerped;// = 0x5f9cc
#define s_lerped (*dp_s_lerped)
float *shadelight;// = 0x383b0
float **dp_shadedots;// = 0x2e8c8
#define shadedots (*dp_shadedots)
byte **dp_currententity;// = 0x5fe7c
#define currententity (*dp_currententity)
byte **dp_currentskelverts;// = 0x5f984
#define currentskelverts (*dp_currentskelverts)
int **dp_fmodel;// = 0x5f9fc
#define fmodel (*dp_fmodel)
float *bytedirs;// = 0x2a130
image_t **dp_r_notexture;// = 0x5fbc4
#define r_notexture (*dp_r_notexture)
int *dp_currenttmu;// = 0x5ff78
#define currenttmu (*dp_currenttmu)
int *currenttexture;// = 0x5ff70
float *r_turbsin;// = 0x30684
static cvarq2_t* r_fullbright;
static cvarq2_t* gl_drawflat;
static cvarq2_t* gl_sortmulti;
static cvarq2_t* gl_showtris;
static cvarq2_t* gl_shownormals;
static cvarq2_t* r_nocull;
static cvarq2_t* gl_dynamic;
static cvarq2_t* quake_amount;
static cvarq2_t* rmx_skiplightmaps;
static cvarq2_t* rmx_novis;
static cvarq2_t* rmx_normals;
static cvarq2_t* rmx_coronas;
static cvarq2_t* rmx_generic;
static cvarq2_t* rmx_dyn_linger;
static cvarq2_t* rmx_alphacull;

static image_t* (*R_TextureAnimation)(const mtexinfo_t* tex);// = 0xae70;
static void (APIENTRY** fp_qglMultiTexCoord2fARB)(GLenum target, GLfloat s, GLfloat t);// = 0x53428 0x53770
#define qglMultiTexCoord2fARB (*fp_qglMultiTexCoord2fARB)
static void (APIENTRY** fp_qglMTexCoord2fSGIS)(GLenum, GLfloat, GLfloat);// = 0x53770
#define qglMTexCoord2fSGIS (*fp_qglMTexCoord2fSGIS)
static qboolean( *R_CullBox )(vec3_t mins, vec3_t maxs);// = 0x7800
static void (*R_AddSkySurface)(const msurface_t* fa);// = 0xf420
static void (*GL_RenderLightmappedPoly_ARB)(msurface_t* surf);// = 0xbf50
static void (*R_BuildLightMap)( msurface_t* surf, byte* dest, int stride );// = 0x52b0
static void (*R_SetCacheState)( msurface_t* surf );// = 0x5270
static void (*GL_MBind)(GLenum target, int texnum);// = 0x3650
static void (*GL_EnableMultitexture)( qboolean enable );// = 0x33a0
static qboolean (*R_CullAliasModel)( vec3_t bbox[8], void* /*entity_t* e*/ );// = 0x2b20
static image_t* (*GL_FindImage)( const char* name, const imagetype_t type );// = 0x4090
}

#define PTR_FROM_OFFSET(typecast, offset) (typecast)((intptr_t)(offset) + (intptr_t)ref_gl_data.lpBaseOfDll)

const char* modulename = "ref_gl.dll";
static DWORD modulesize = 253952;
static MODULEINFO ref_gl_data;

static image_t *r_whiteimage = NULL;

typedef struct drawSurf_s {
	unsigned    sort;
	msurface_t *surface;
} drawSurf_t;

#define MAX_DRAWSURFS           0x10000
typedef struct {
	int         numSurfs;
	drawSurf_t  surfs[MAX_DRAWSURFS];
} surfList_t;

static surfList_t g_surfList = { 0 };

static void h2_intercept_RecursiveWorldNode( mnode_t* node );
static qboolean h2_intercept_R_CullAliasModel( vec3_t bbox[8], void* e /*entity_t* e*/ );
static void h2_bridge_to_Model_BuildVBuff();
static void h2_check_crtent_frame_vs_oldframe();
static void R_RecursiveWorldNodeEx( mnode_t* node, BOOL inpvs );
static void R_SortAndDrawSurfaces( drawSurf_t* surfs, int numSurfs );
static void R_AddDrawSurf( msurface_t* surf );

static void h2_generic_fixes();
static void h2_generic_fixes_deinit();
static void h2_flashlight_toggle();

void h2_refgl_init()
{
	ZeroMemory(&ref_gl_data, sizeof(ref_gl_data));
	HMODULE refdll = GetModuleHandle( modulename );
	if (refdll && GetModuleInformation(GetCurrentProcess(), refdll, &ref_gl_data, sizeof(ref_gl_data)))
	{
		logPrintf("h2_refgl_init: ref_gl base:%p size:%d ep:%p \n", ref_gl_data.lpBaseOfDll, ref_gl_data.SizeOfImage, ref_gl_data.EntryPoint);

		//if ( modulesize == ref_gl_data.SizeOfImage ) //doesn't match, disable this; maybe find another way to match the dll version
		{
			dp_r_visframecount = PTR_FROM_OFFSET(int*, 0x5fe38 );
			dp_r_framecount = PTR_FROM_OFFSET(int*, 0x5fd20 );
			modelorg = PTR_FROM_OFFSET(float*, 0x5fb40 );
			dp_r_newrefdef_areabits = PTR_FROM_OFFSET(byte**, 0x5fcb8 );
			dp_r_worldmodel = PTR_FROM_OFFSET(model_t**, 0x5fbd0 );
			dp_r_alpha_surfaces = PTR_FROM_OFFSET(msurface_t**, 0x5fb4c );
			dp_num_sorted_multitextures = PTR_FROM_OFFSET(int*, 0x3da08 );
			dp_ri = PTR_FROM_OFFSET(intptr_t*, 0x5fd60 );
			dp_r_newrefdef_time = PTR_FROM_OFFSET(float*, 0x5fcb0 );
			r_newrefdef_lightstyles = PTR_FROM_OFFSET(lightstyle_t*, 0x5fcbc );
			dp_gl_state_lightmap_textures = PTR_FROM_OFFSET(int*, 0x5ff6c );
			dp_c_brush_polys = PTR_FROM_OFFSET(int*, 0x5fd40 );
			dp_s_lerped = PTR_FROM_OFFSET( float**, 0x5f9cc );
			shadelight = PTR_FROM_OFFSET( float*, 0x383b0 );
			dp_shadedots = PTR_FROM_OFFSET( float**, 0x2e8c8 );
			dp_currententity = PTR_FROM_OFFSET( byte**, 0x5fe7c );
			dp_fmodel = PTR_FROM_OFFSET( int**, 0x5f9fc );
			dp_currentskelverts = PTR_FROM_OFFSET( byte**, 0x5f984 );
			bytedirs = PTR_FROM_OFFSET( float*, 0x2a130 );
			dp_r_notexture = PTR_FROM_OFFSET( image_t**, 0x5fbc4 );
			dp_currenttmu = PTR_FROM_OFFSET( int*, 0x5ff78 );
			currenttexture = PTR_FROM_OFFSET( int*, 0x5ff70 );
			r_turbsin = PTR_FROM_OFFSET( float*, 0x30684 );

			R_TextureAnimation = (image_t*(*)(const mtexinfo_t *))((intptr_t)0xae70 + (intptr_t)ref_gl_data.lpBaseOfDll);
			fp_qglMultiTexCoord2fARB = (void (APIENTRY **)(GLenum,GLfloat,GLfloat))((intptr_t)0x53770 + (intptr_t)ref_gl_data.lpBaseOfDll);
			fp_qglMTexCoord2fSGIS = (void (APIENTRY **)(GLenum,GLfloat,GLfloat))((intptr_t)0x53428 + (intptr_t)ref_gl_data.lpBaseOfDll);
			R_CullBox = (qboolean (*)(float [],float []))((intptr_t)0x7800 + (intptr_t)ref_gl_data.lpBaseOfDll);
			R_AddSkySurface = (void (*)(const msurface_t *))((intptr_t)0xf420 + (intptr_t)ref_gl_data.lpBaseOfDll);
			GL_RenderLightmappedPoly_ARB = (void (*)(msurface_t *))((intptr_t)0xbf50 + (intptr_t)ref_gl_data.lpBaseOfDll);
			R_BuildLightMap = (void (*)(msurface_t *,byte *,int))((intptr_t)0x52b0 + (intptr_t)ref_gl_data.lpBaseOfDll);
			R_SetCacheState = (void (*)(msurface_t *))((intptr_t)0x5270 + (intptr_t)ref_gl_data.lpBaseOfDll);
			GL_MBind = (void (*)(GLenum,int))((intptr_t)0x3650 + (intptr_t)ref_gl_data.lpBaseOfDll);
			GL_EnableMultitexture = (void (*)(qboolean))((intptr_t)0x33a0 + (intptr_t)ref_gl_data.lpBaseOfDll);
			R_CullAliasModel = PTR_FROM_OFFSET( qboolean (*)(vec3_t [],void *), 0x2b20 );
			GL_FindImage = PTR_FROM_OFFSET( image_t *(*)(const char *,const imagetype_t), 0x4090 );

			//sanity check: ideally we'd want to map this to quake2.dll
			intptr_t fp = dp_ri[RI_CVAR_GET];
			if ( fp == NULL )
			{
				logPrintf( "h2_refgl_init:CvarGet fp is NULL\n" );
				return;
			}
			r_fullbright = riCVAR_GET("r_fullbright", "0", 0);
			gl_drawflat = riCVAR_GET("gl_drawflat", "0", 0);
			gl_sortmulti = riCVAR_GET("gl_sortmulti", "0", 1);
			gl_showtris = riCVAR_GET("gl_showtris", "0", 0);
			gl_shownormals = riCVAR_GET("gl_shownormals", "0", 0);
			r_nocull = riCVAR_GET("r_nocull", "0", 0);
			gl_dynamic = riCVAR_GET("gl_dynamic", "1", 0);
			quake_amount = riCVAR_GET("quake_amount", "0", 0);
			rmx_skiplightmaps = riCVAR_GET("rmx_skiplightmaps", "0", 0);
			rmx_novis = riCVAR_GET("rmx_novis", "1", 0);
			rmx_normals = riCVAR_GET("rmx_normals", "0", 0);
			rmx_generic = riCVAR_GET("rmx_generic", "-1", 0);
			rmx_dyn_linger = riCVAR_GET("rmx_dyn_linger", "1", 1);
			rmx_coronas = riCVAR_GET("rmx_coronas", "1", 1);
			rmx_alphacull = riCVAR_GET("rmx_alphacull", "0", 1);

			riADD_CMD( "rmx_flashlight_toggle", h2_flashlight_toggle );

			qdx_lights_dynamic_linger( int(rmx_dyn_linger->value) );

			//Pin ref_gl handle so that it does not get unloaded on FreeLibrary
			// This is a hack to help with ref_gl.dll and opengl32.dll being
			// unloaded when changing videomodes
			HMODULE hm = 0;
			GetModuleHandleEx( GET_MODULE_HANDLE_EX_FLAG_PIN, modulename, &hm );
			GetModuleHandleEx( GET_MODULE_HANDLE_EX_FLAG_PIN, "opengl32.dll", &hm );

			byte *code;
			intptr_t val;

			if ( D3DGlobal_ReadGameConfPtr( "patch_h2_rwn" ) )
			{
				//R_DrawWorld calls R_RecursiveWorldNode
				//e8 7e fb ff ff
				code = (byte*)((intptr_t)0xcbdd + (intptr_t)ref_gl_data.lpBaseOfDll);
				memcpy( &val, &code[1], 4 );
				if ( code[0] == 0xe8 && val == 0xfffffb7e )
				{
					val = (intptr_t)h2_intercept_RecursiveWorldNode - (intptr_t)&code[5];
					//do we need to check something here? can val be larger than 2GB for 32bit?
					unsigned long restore;
					if ( hook_unprotect( code, 5, &restore ) )
					{
						memcpy( &code[1], &val, sizeof( val ) );
						hook_protect( code, 5, restore );

						logPrintf( "h2_refgl_init:R_RecursiveWorldNode was patched\n" );
					}
				}
				else
					logPrintf( "h2_refgl_init:R_DrawWorld: the CALL instr does not match %x %x\n", code[0], val );
			}

			//R_DrawAliasModel calls R_CullAliasModel
			//e8 d0 09 00 00
			code = PTR_FROM_OFFSET(byte*, 0x214b);
			memcpy( &val, &code[1], 4 );
			if ( code[0] == 0xe8 && val == 0x000009d0 )
			{
				val = (intptr_t)h2_intercept_R_CullAliasModel - (intptr_t)&code[5];
				//do we need to check something here? can val be larger than 2GB for 32bit?
				unsigned long restore;
				if ( hook_unprotect( code, 5, &restore ) )
				{
					memcpy( &code[1], &val, sizeof( val ) );
					hook_protect( code, 7, restore );
				}
			}
			else
				logPrintf("h2_refgl_init:R_DrawAliasModel: the CALL instr does not match %x %x\n", code[0], val);

			if ( D3DGlobal_ReadGameConfPtr( "patch_h2_dffl" ) )
			{
				unsigned long restore;
				//GL_DrawFlexFrameLerp draws the vertices				
				//8b 2f 83 c7 this is where the draw loop starts
				code = PTR_FROM_OFFSET( byte*, 0x28ab );
				memcpy( &val, &code[0], 4 );
				if ( val == 0xc7832f8b )
				{
					if ( hook_unprotect( code, 471, &restore ) )
					{
						//copy our asm bridge code over
						byte* src = (byte*)h2_bridge_to_Model_BuildVBuff;
						int nopcnt = 0;
						int i = 0;
						while (i < 471)
						{
							code[i] = src[i];

							if ( code[i] == 0x90 )
								nopcnt++;
							else
								nopcnt = 0;

							i++;

							if ( nopcnt >= 5 )
								break;
						}

						if ( nopcnt == 5 )
						{
							//now that we reached the nop instructions, make a jmp to end of draw loop
							byte* endcall = PTR_FROM_OFFSET( byte*, 0x29d6 ); //this is where the loop ends
							val = endcall - &code[i];
							code[i-5] = 0xe9;//jmp relative
							memcpy( &code[i-4], &val, 4 );

							logPrintf( "h2_refgl_init:GL_DrawFlexFrameLerp was patched\n" );
						}

						hook_protect( code, 471, restore );
					}
				}
				else
					logPrintf( "h2_refgl_init:GL_DrawFlexFrameLerp: the dffl instr does not match %x\n", val );


				//GL_DrawFlexFrameLerp calculates vertex normals
				//0f 84 83 00 00 00
				code = PTR_FROM_OFFSET(byte*, 0x26ee);
				memcpy( &val, &code[0], 4 );
				if ( val == 0x0083840f )
				{
					if ( hook_unprotect( code, 6, &restore ) )
					{
						//nop over jmp
						memset( code, 0x90, 6 );

						hook_protect( code, 6, restore );
					}
				}
				else
					logPrintf( "h2_refgl_init:GL_DrawFlexFrameLerp: the nrml instr does not match %x\n", val );

				//R_DrawInlineBModel check for planeback and view angle
				//74 0d dc 15
				code = PTR_FROM_OFFSET(byte*, 0xc2e3);
				memcpy( &val, &code[0], 4 );
				if ( val == 0x15dc0d74 )
				{
					if ( hook_unprotect( code, 14, &restore ) )
					{
						//multiple comparisons here, modding 2 jumps seems to be the least changes
						code[0] = 0x90;
						code[1] = 0x90;
						code[13] = 0xeb;

						hook_protect( code, 14, restore );
					}
				}
				else
					logPrintf( "h2_refgl_init:R_DrawInlineBModel: the do not draw instr does not match %x\n", val );

				//skip draw brush model
				//code = PTR_FROM_OFFSET(byte*, 0x7a84);
				//if ( hook_unprotect( code, 5, &restore ) )
				//{
				//	memset( code, 0x90, 5 );

				//	hook_protect( code, 5, restore );
				//}
			}

			//R_DrawAliasModel checks r_lerpmodels
			//8b 15 fc fe
			code = PTR_FROM_OFFSET(byte*, 0x2558);
			memcpy( &val, &code[0], 4 );
			if ( val == 0xfefc158b )
			{
				unsigned long restore;
				if ( hook_unprotect( code, 25, &restore ) )
				{
					//copy our asm bridge code over
					byte* src = (byte*)h2_check_crtent_frame_vs_oldframe;
					int i = 0;
					for ( ; i < 25; i++ )
					{
						code[i] = src[i];
					}
					if ( src[i] != 0x90 )
					{
						//too much function, not enough space
						logPrintf("h2_refgl_init:R_DrawAliasModel: next instr is not NOP\n");
					}

					hook_protect( code, 25, restore );
				}
			}
			else
				logPrintf("h2_refgl_init:R_DrawAliasModel: the instr does not match %x\n", val);
		}
		//else
		//	logPrintf("h2_refgl_init: size of ref_gl does not match %d vs %d, patching will most likely result in crash. Aborted.\n", modulesize, ref_gl_data.SizeOfImage);
	}
	else
	{
		logPrintf("h2_refgl_init: cannot get ref_gl info %s\n", DXGetErrorString(GetLastError()));
	}

	h2_generic_fixes();
}

void h2_refgl_deinit()
{
	if ( 0 ) //I don't know how to handle this for the case when dll is releaded (opengl32 and/or ref_gl)
	{
		unsigned long restore;

		//R_DrawWorld calls R_RecursiveWorldNode
		//e8 7e fb ff ff
		byte *code = (byte*)((intptr_t)0xcbdd + (intptr_t)ref_gl_data.lpBaseOfDll);

		intptr_t val = 0xfffffb7e;
		if ( hook_unprotect( code, 5, &restore ) )
		{
			code[0] = 0xe8;
			memcpy( &code[1], &val, sizeof( val ) );
			hook_protect( code, 5, restore );
		}

		//R_DrawAliasModel calls R_CullAliasModel
		//e8 d0 09 00 00
		code = PTR_FROM_OFFSET(byte*, 0x214b);

		val = 0x000009d0;
		if ( hook_unprotect( code, 5, &restore ) )
		{
			code[0] = 0xe8;
			memcpy( &code[1], &val, sizeof( val ) );
			hook_protect( code, 5, restore );
		}

		//GL_DrawFlexFrameLerp draws the vertices
		//8b 2f 83 c7 this is where the draw loop starts
		//we can't restore it unless we save the bytes
	}

	h2_generic_fixes_deinit();
}

static void h2_flashlight_toggle()
{
	rmx_flashlight_enable();
}

OPENGL_API void WINAPI glPushDebugGroup( GLenum source, GLuint id, GLsizei length, const char* message );
OPENGL_API void WINAPI glPopDebugGroup( void );

static void h2_intercept_RecursiveWorldNode( mnode_t* node )
{
	HOOK_ONLINE_NOTICE();

	g_surfList.numSurfs = 0;

	glPushDebugGroup(0, 0, -1, "R_RecursiveWorldNodeEx");

	R_RecursiveWorldNodeEx( node, TRUE );

	if ( rmx_skiplightmaps->value )
	{
		GL_EnableMultitexture( qfalse );
	}

	R_SortAndDrawSurfaces( g_surfList.surfs, g_surfList.numSurfs );

	glPopDebugGroup();
}

static qboolean h2_intercept_R_CullAliasModel( vec3_t bbox[8], void* e /*entity_t* e*/ )
{
	HOOK_ONLINE_NOTICE();

	if ( r_nocull->value > 1 )
	{
		return qfalse;
	}
	else
	{
		return R_CullAliasModel( bbox, e );
	}
}

static void R_Model_BuildVBuffAndDraw( int* order, float *normals_array, int *p_alpha );
//static void (*fp_Model_BuildVBuff)(int* order, float *normals_array) = R_Model_BuildVBuffAndDraw;

static __declspec(naked) void h2_bridge_to_Model_BuildVBuff()
{
	__asm {
		// save the clobbered registers
		push eax
		push ecx
		push edx

		lea ecx,[ESP + 0x18 + 0xc] //alpha is 0x18 bytes lower than normals_array
		lea edx,[ESP + 0x30 + 0xc] //normals_array
		push ecx
		push edx
		push edi //order

		//call fp_Model_BuildVBuff
		mov eax, R_Model_BuildVBuffAndDraw
		call eax
		add esp,12

		//restore clobbered registers, in reverse order
		pop edx
		pop ecx
		pop eax

		//placeholder for our jmp relative to end of loop
		nop
		nop
		nop
		nop
		nop
	}
}

static __declspec(naked) void h2_check_crtent_frame_vs_oldframe()
{
	__asm {
		//ecx has the address of curententity
		MOV        EAX,dword ptr [ECX + 0x1c] //frame
		CMP        EAX,dword ptr [ECX + 0x44] //oldframe
		JNZ        skip_clear_backlerp
		//MOV        EAX,dword ptr [ECX + 0xa4] //swapFrame
		//CMP        EAX,dword ptr [ECX + 0xa8] //oldSwapFrame
		CMP        dword ptr [ECX + 0xa4],-0x1 //swapFrame != -1
		jnz        skip_clear_backlerp
		mov        dword ptr [ecx+48h],edi //backlerp = 0
	skip_clear_backlerp:
		nop
		nop
		nop
		nop
		nop
		nop
		nop
		nop
		nop
		nop
	}
}

static void R_RecursiveWorldNodeEx(mnode_t* node, BOOL inpvs)
{
	msurface_t* surf;
	int c;
	int side;
	int sidebit;

	if (node->contents == CONTENTS_SOLID)
		return;
	
	if(node->visframe != r_visframecount)
	{
		if( !rmx_novis->value )
			return;

		inpvs = FALSE;
	}

	//R_CullBox checks for r_nocull
	if (R_CullBox(node->minmaxs, node->minmaxs + 3))
		return;

	// If a leaf node, draw stuff
	if (node->contents != -1)
	{
		const mleaf_t* pleaf = (mleaf_t*)node;

		// Check for door connected areas
		if (/*r_newrefdef.areabits*/r_newrefdef_areabits)
		{
			if (! (/*r_newrefdef.areabits*/r_newrefdef_areabits[pleaf->area>>3] & (1<<(pleaf->area&7)) ) )
			{
				if(rmx_novis->value < 2)
					return;		// not visible

				inpvs = FALSE;
			}
		}

		msurface_t** mark = pleaf->firstmarksurface;
		for (int i = pleaf->nummarksurfaces; i > 0; i--)
		{
			(*mark)->visframe = r_framecount;
			mark++;
		}

		return;
	}

	float dot = 0;

	// Node is just a decision point, so go down the appropriate sides.
	// Find which side of the node we are on.
	const cplane_t* plane = node->plane;

	switch ( plane->type )
	{
	case PLANE_X:
		dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		dot = modelorg[2] - plane->dist;
		break;
	default:
		dot = DotProduct( modelorg, plane->normal ) - plane->dist;
		break;
	}

	if (dot >= 0.0f)
	{
		side = 0;
		sidebit = 0;
	}
	else
	{
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

	// Recurse down the children, front side first
	R_RecursiveWorldNodeEx(node->children[side], inpvs);

	//alphacull 1 and 2 hides frontface alpha surfs not in pvs
	bool show_ff_alpha = inpvs || (rmx_alphacull->value < 1);

	for (c = node->numsurfaces, surf = r_worldmodel->surfaces + node->firstsurface; c > 0; c--, surf++)
	{
		if (surf->visframe != r_framecount || (surf->flags & SURF_PLANEBACK) != sidebit)
			continue; // Wrong frame or side

		if (surf->texinfo->flags & SURF_SKY)
		{
			// Just adds to visible sky bounds
			R_AddSkySurface(surf);
		}
		else if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
		{
			// Add to the translucent chain
			if ( show_ff_alpha )
			{
				surf->texturechain = r_alpha_surfaces;
				r_alpha_surfaces = surf;
			}
		}
		else if ( (qglMultiTexCoord2fARB != NULL || qglMTexCoord2fSGIS != NULL)
					&& !(surf->flags & (SURF_DRAWTURB|SURF_TALL_WALL)))
		{
			//if ((int)gl_sortmulti->value)
			//{
			//	// The polygon is visible, so add it to the multi-texture sorted chain
			//	image_t* image = R_TextureAnimation(surf->texinfo);
			//	surf->texturechain = image->multitexturechain;
			//	image->multitexturechain = surf;
			//	num_sorted_multitextures += 1;
			//}
			//else
			{
				R_AddDrawSurf( surf );
				//GL_RenderLightmappedPoly_ARB(surf);
			}
		}
		else
		{
			// The polygon is visible, so add it to the texture sorted chain
			// FIXME: this is a hack for animation
			if ( inpvs )
			{
				image_t* image = R_TextureAnimation(surf->texinfo);
				surf->texturechain = image->texturechain;
				image->texturechain = surf;
			}
		}
	}

	// Recurse down the back side
	R_RecursiveWorldNodeEx( node->children[!side], inpvs );

	//alphacull 2 hides backface alpha surfs, in addition to alphacull 1 hiding surfs not in pvs
	bool show_bf_alpha = 0;// show_ff_alpha && (rmx_alphacull->value < 2);

	if ( r_nocull->value )
	{
		if (side == 0)
		{
			sidebit = SURF_PLANEBACK;
		}
		else
		{
			sidebit = 0;
		}

		for (c = node->numsurfaces, surf = r_worldmodel->surfaces + node->firstsurface; c > 0; c--, surf++)
		{
			if (surf->visframe != r_framecount || (surf->flags & SURF_PLANEBACK) != sidebit)
				continue; // Wrong frame or side

			if (surf->texinfo->flags & SURF_SKY)
			{
				// Just adds to visible sky bounds
				R_AddSkySurface(surf);
			}
			else if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
			{
				//these just tank the fps, and since they are not visible let's just not render for now
				// Add to the translucent chain
				if ( show_bf_alpha )
				{
					surf->texturechain = r_alpha_surfaces;
					r_alpha_surfaces = surf;
				}
			}
			else if ( (qglMultiTexCoord2fARB != NULL || qglMTexCoord2fSGIS != NULL)
				&& !(surf->flags & (SURF_DRAWTURB|SURF_TALL_WALL)))
			{
				//if ((int)gl_sortmulti->value)
				//{
				//	// The polygon is visible, so add it to the multi-texture sorted chain
				//	image_t* image = R_TextureAnimation(surf->texinfo);
				//	surf->texturechain = image->multitexturechain;
				//	image->multitexturechain = surf;
				//	num_sorted_multitextures += 1;
				//}
				//else
				{
					R_AddDrawSurf( surf );
					//GL_RenderLightmappedPoly_ARB(surf);
				}
			}
			else
			{
				// The polygon is visible, so add it to the texture sorted chain
				// FIXME: this is a hack for animation
				if ( inpvs )
				{
					image_t* image = R_TextureAnimation(surf->texinfo);
					surf->texturechain = image->texturechain;
					image->texturechain = surf;
				}
			}
		}
	}
}

#define SKIP_LIGHTMAP 0x7fffu

#define RENDER_TWOTEXTURES 1u
#define RENDER_NORMALS     2u
#define RECALC_NORMALS     4u

union sort_pack_u
{
	unsigned all;
	struct {
		unsigned lightmap : 15;
		unsigned flowing : 1;
		unsigned texnum : 15;
		unsigned dynamic : 1;
	} bits;
};

static void R_AddDrawSurf(msurface_t *surf)
{
	int index;

	index = g_surfList.numSurfs;
	if ( index < MAX_DRAWSURFS )
	{
		union sort_pack_u sort = { 0 };

		/** First check for dynamic light; these should go last in the list */
		qboolean dynamic = qfalse;
		int map;
		for ( map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++ )
		{
			if ( /*r_newrefdef.lightstyles*/r_newrefdef_lightstyles[surf->styles[map]].white != surf->cached_light[map] )
			{
				dynamic = qtrue;
				break;
			}
		}

		// dynamic this frame or dynamic previously
		if ( (surf->dlightframe == r_framecount) || dynamic )
		{
			if ( gl_dynamic->value )
			{
				if ( !(surf->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP|SURF_TALL_WALL)) )
				{
					//sort.bits.dynamic = 1;
				}
			}
		}

		sort.bits.texnum = R_TextureAnimation( surf->texinfo )->texnum;
		sort.bits.flowing = 0;// surf->texinfo->flags& SURF_FLOWING;
		if ( rmx_skiplightmaps->value )
			sort.bits.lightmap = SKIP_LIGHTMAP;
		else
			sort.bits.lightmap = surf->lightmaptexturenum;

		g_surfList.surfs[index].sort = sort.all;
		g_surfList.surfs[index].surface = surf;
		g_surfList.numSurfs++;
	}
	else
		riPRINTF( PRINT_ALL, "Too many surfs\n" );
		//ri.Con_Printf( PRINT_ALL, "Too many surfs\n" );
}

static void R_RenderSurfs( int flags );
static void R_PopulateDrawBuffer( msurface_t* surf, int is_dynamic, int is_flowing, uint32_t flags );
static int h2_surfaces_compare( const void* arg1, const void* arg2 );

OPENGL_API void WINAPI glTexSubImage2D( GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* pixels );

static void R_SortAndDrawSurfaces( drawSurf_t* surfs, int numSurfs )
{
	qsort( surfs, numSurfs, sizeof( drawSurf_t ), h2_surfaces_compare );

	unsigned oldSort = ~0u;
	//int oldDynamic = -1;
	int oldTexnum = -1;
	//int oldFlowing = -1;
	int oldLightmap = -1;
	uint32_t flags = 0;

	union sort_pack_u sort = { 0 };
	drawSurf_t* s = surfs;

	for ( int i = 0; i < numSurfs; i++, s++ )
	{
#if 0
		GL_RenderLightmappedPoly_ARB( s->surface );
#else

		sort.all = s->sort;

		if ( sort.bits.dynamic || oldTexnum != sort.bits.texnum || oldLightmap != sort.bits.lightmap )
		{
			//render what was accumulated so far, with the bound textures
			R_RenderSurfs(flags);
		}

		//check if new textures need to be bound
		if ( sort.bits.dynamic )
		{
			int		map;
			unsigned lmtex;
			msurface_t* surf = s->surface;

			for ( map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++ )
			{
				if ( /*r_newrefdef.lightstyles*/r_newrefdef_lightstyles[surf->styles[map]].white != surf->cached_light[map] )
					break;
			}

			static unsigned	temp[128*128];
			const int smax = (surf->extents[0]>>4)+1;
			const int tmax = (surf->extents[1]>>4)+1;

			R_BuildLightMap( surf, (byte *)temp, smax*4 );

			if ( ( surf->styles[map] >= 32 || surf->styles[map] == 0 ) && ( surf->dlightframe != r_framecount ) )
			{
				R_SetCacheState( surf );
				GL_MBind( /*GL_TEXTURE1_SGIS*/GL_TEXTURE1, /*gl_state.lightmap_textures*/gl_state_lightmap_textures + surf->lightmaptexturenum );
				lmtex = surf->lightmaptexturenum;
			}
			else
			{
				GL_MBind( /*GL_TEXTURE1_SGIS*/GL_TEXTURE1, /*gl_state.lightmap_textures*/gl_state_lightmap_textures + 0 );
				lmtex = 0;
			}

			glTexSubImage2D( GL_TEXTURE_2D, 0,
				surf->light_s, surf->light_t, 
				smax, tmax, 
				GL_LIGHTMAP_FORMAT, 
				GL_UNSIGNED_BYTE, temp );

			GL_MBind( /*GL_TEXTURE0_SGIS*/GL_TEXTURE0, sort.bits.texnum/*image->texnum*/ );
			GL_MBind( /*GL_TEXTURE1_SGIS*/GL_TEXTURE1, /*gl_state.lightmap_textures*/gl_state_lightmap_textures + lmtex );
			flags = RENDER_TWOTEXTURES;
		}
		else if( oldTexnum != sort.bits.texnum || oldLightmap != sort.bits.lightmap )
		{
			flags = 0;
			GL_MBind( /*GL_TEXTURE0_SGIS*/GL_TEXTURE0, sort.bits.texnum/*image->texnum*/ );
			if ( sort.bits.lightmap != SKIP_LIGHTMAP )
			{
				GL_MBind( /*GL_TEXTURE1_SGIS*/GL_TEXTURE1, /*gl_state.lightmap_textures*/gl_state_lightmap_textures + sort.bits.lightmap/*lmtex*/ );
				flags = RENDER_TWOTEXTURES;
			}
		}
		oldSort = sort.all;
		oldTexnum = sort.bits.texnum;
		oldLightmap = sort.bits.lightmap;

		R_PopulateDrawBuffer( s->surface, sort.bits.dynamic, sort.bits.flowing, flags );
#endif
	}

	//one more call for the remaining vertices
	R_RenderSurfs(flags);
}

typedef union colorinfo_u
{
	byte b[4];
	unsigned all;
} colorinfo_t;

#define MAX_VERTEXES 4000
#define MAX_INDEXES (6*MAX_VERTEXES)
struct vertexData_s
{
	float xyz[3];
	float normal[3];
	colorinfo_t clr;
	float tex0[2];
	float tex1[2];
};

struct drawbuff_s
{
	int numVertexes;
	int numIndexes;
	struct vertexData_s vertexes[MAX_VERTEXES];
	unsigned short indexes[MAX_INDEXES];
} g_drawBuff;

static void R_CheckDrawBufferSpace( int vertexes, int indexes, uint32_t flags )
{
	if ( g_drawBuff.numVertexes + vertexes > MAX_VERTEXES ||
		g_drawBuff.numIndexes + indexes > MAX_INDEXES )
	{
		R_RenderSurfs(flags);
	}
}

static void R_PopulateDrawBuffer( msurface_t* surf, int is_dynamic, int is_flowing, uint32_t flags )
{
	int i;
	float *v;
	glpoly_t *p;
	float scroll = 0;

	//c_brush_polys++;

	if ( is_flowing )
	{
		scroll = -64.0f * ( (/*r_newrefdef.time*/r_newrefdef_time / 40.0f) - (int)(/*r_newrefdef.time*/r_newrefdef_time / 40.0f) );
		if(scroll == 0.0f)
			scroll = -64.0f;
	}

	for ( p = surf->polys; p; p = p->chain )
	{
		int totalindexes = (3 * p->numverts) - 6;
		R_CheckDrawBufferSpace( p->numverts, totalindexes, flags );

		int index = g_drawBuff.numVertexes;
		struct vertexData_s* draw = &g_drawBuff.vertexes[g_drawBuff.numVertexes];
		unsigned short* ibuf = &g_drawBuff.indexes[g_drawBuff.numIndexes];
		unsigned short startidx = index;
		v = p->verts[0];
		for (i = 0 ; i < p->numverts; i++, v+= VERTEXSIZE)
		{
			if ( i > 2 )
			{
				ibuf[0] = startidx;
				ibuf[1] = index - 1;
				ibuf += 2;
			}
			ibuf[0] = index++;
			VectorCopy( v, draw->xyz );
			draw->clr.all = 0xffffffff;
			draw->tex0[0] = v[3]+scroll;
			draw->tex0[1] = v[4];
			draw->tex1[0] = v[5];
			draw->tex1[1] = v[6];
			draw++;
			ibuf++;
		}

		g_drawBuff.numVertexes += p->numverts;
		g_drawBuff.numIndexes += totalindexes;
	}
}

OPENGL_API void WINAPI glPolygonMode( GLenum face, GLenum mode );
OPENGL_API void WINAPI glColor3f( GLfloat red, GLfloat green, GLfloat blue );
OPENGL_API void WINAPI glVertex3fv( const GLfloat *v );
OPENGL_API void WINAPI glNormal3fv( const GLfloat *v );
OPENGL_API void WINAPI glTexCoord2f( GLfloat s, GLfloat t );
OPENGL_API void WINAPI glBegin( GLenum mode );
OPENGL_API void WINAPI glEnd();
OPENGL_API void WINAPI glEnable( GLenum cap );
OPENGL_API void WINAPI glDisable( GLenum cap );

static float* g_calcnormals = NULL;

static void R_Model_BuildVBuffAndDraw(int *order, float *normals_array, int *p_alpha)
{
	HOOK_ONLINE_NOTICE();

	//if ( g_calcnormals != normals_array )
	//{
	//	__debugbreak();
	//}

	//if ( *alpha )
	//{
	//	__debugbreak();
	//}

	uint32_t render_flags = 0;
	if(rmx_normals->value)
		render_flags |= RENDER_NORMALS;
	
	while ( 1 )
	{
		int strategy;
		// get the vertex count and primitive type
		int count = *order++;
		if (!count)
			break;		// done
		if (count < 0)
		{
			count = -count;
			//qglBegin (GL_TRIANGLE_FAN);
			strategy = 0;
		}
		else
		{
			//qglBegin (GL_TRIANGLE_STRIP);
			strategy = 1;
		}
		int index_xyz;
		struct vertexData_s* vb;
		unsigned short* ib;
		unsigned char alpha = 255;// (*p_alpha) ? 255 : 127;

		int totalindexes = (3 * count) - 6;
		R_CheckDrawBufferSpace( count, totalindexes, render_flags );

		int i;
		int index = g_drawBuff.numVertexes;
		ib = &g_drawBuff.indexes[g_drawBuff.numIndexes];
		vb = &g_drawBuff.vertexes[g_drawBuff.numVertexes];
		int start = index;


		{
			for ( i = 0; i < count; i++ )
			{
				if ( i > 2 )
				{
					switch ( strategy )
					{
					case 0:
						ib[0] = start;
						ib[1] = index - 1;
						ib += 2;
						break;
					case 1: { //triangle strip: switch winding strategy depending on even/uneven index
						int uneven = i & 1;
						ib[!uneven] = index -1;
						ib[uneven] = index -2;
						ib += 2;
						break; }
					}
				}
				ib[0] = index++;

				// texture coordinates come from the draw list
				//qglTexCoord2f (((float *)order)[0], ((float *)order)[1]);
				vb->tex0[0] = ((float *)order)[0];
				vb->tex0[1] = ((float *)order)[1];
				index_xyz = order[2];
				order += 3;

				const float no_normals[] = { 1.0f, 0.0f, 0.0f };
				const float *normals = no_normals;

				if ( (currententity[0x30] & 8) != 0 ) //check if FULLBRIGHT
				{
					vb->clr.all = 0xffffffff;
					render_flags &= ~RENDER_NORMALS;
				}
				else
				{
					// normals and vertexes come from the frame list
					//float l = shadedots[verts[index_xyz].lightnormalindex];
					float l;
					if ( fmodel[0xc] == 0 ) //fmodel->frames == NULL
					{
						l = shadedots[((byte*)fmodel[0x14])[index_xyz]]; //fmodel->lightnormalindex[]
						normals = bytedirs + ((byte*)fmodel[0x14])[index_xyz] * 3;
					}
					else
					{
						byte *off = currentskelverts + index_xyz * 4 + 3;
						l = shadedots[*off];
						normals = normals_array + index_xyz * 3;
					}
					//qglColor4f (l* shadelight[0], l*shadelight[1], l*shadelight[2], alpha);
					vec3_t colors;
					float colorscale = l * 255;
					VectorScale( shadelight, colorscale, colors );
					VectorCopyClamp( colors, vb->clr.b, 255 );
					vb->clr.b[3] = alpha;
				}
				float* vsrc = s_lerped + index_xyz * 3;
				//qglVertex3fv (s_lerped[index_xyz]);
				VectorCopy( vsrc, vb->xyz );
				VectorCopy( normals, vb->normal );
				vb++;
				ib++;

			} //while (--count);
		}

		g_drawBuff.numVertexes += count;
		g_drawBuff.numIndexes += totalindexes;
		//qglEnd ();

	}

	R_RenderSurfs( render_flags );
}

static int h2_surfaces_compare( const void *arg1, const void *arg2 )
{
	int ret = 0;

	drawSurf_t* s1 = (drawSurf_t*)arg1;
	drawSurf_t* s2 = (drawSurf_t*)arg2;

	if ( (s1->sort ) > (s2->sort ) )
	{
		ret = 1;
	}
	else if ( (s1->sort ) < (s2->sort ) )
	{
		ret = -1;
	}
	else //equals
	{
		if ( s1->surface > s2->surface )
		{
			ret = 1;
		}
		else if ( s1->surface < s2->surface )
		{
			ret = -1;
		}
		else //equals
		{
		}
	}

	return ret;
}

static inline int h2_vertexes_compare2( const struct vertexData_s* a, const struct vertexData_s* b )
{
	const float epsilon = 1e-9f;

	const float dx = a->xyz[0] - b->xyz[0];
	if ( abs( dx ) < epsilon )
	{
		const float dy = a->xyz[1] - b->xyz[1];
		if ( abs( dy ) < epsilon )
		{
			const float dz = a->xyz[2] - b->xyz[2];
			if ( abs( dz ) < epsilon )
			{
				return 0;
			}
			else return (signbit( dz ) ? -1 : 1);
		}
		else return (signbit(dy) ? -1 : 1);
	}
	else return (signbit( dx ) ? -1 : 1);
}

static int h2_vertexes_compare( void const* x0, void const* x1 )
{
	const struct vertexData_s * a = &g_drawBuff.vertexes[*((uint16_t*)x0)];
	const struct vertexData_s * b = &g_drawBuff.vertexes[*((uint16_t*)x1)];
	int res = h2_vertexes_compare2( a, b );

	if ( res == 0 )
	{
		if ( a < b )
			return -1;
		return 1;
	}
	return res;
}

inline int IsVectorZero( const vec3_t v )
{
	return (v[0] == 0.f) && (v[1] == 0.f) && (v[2] == 0.f);
}

inline void MergeNormal( const vec3_t normal, uint index, const uint16_t* remapvpos )
{
	float* ndst, dot;
	int vzero;
	if (remapvpos)
		index = remapvpos[index];
	ndst = g_drawBuff.vertexes[index].normal;

	VectorAdd( normal, ndst, ndst );
	VectorNormalize( ndst );
}

static void h2_recalculate_normals()
{
	static uint16_t sortvpos[MAX_VERTEXES];
	static uint16_t remapvpos[MAX_VERTEXES];
	static uint16_t dupvpos[2*MAX_VERTEXES];

	for ( int i = 0; i < g_drawBuff.numVertexes; i++ )
	{
		sortvpos[i] = i;
		remapvpos[i] = i;
	}

	if ( 1 )
	{
		qsort( sortvpos, g_drawBuff.numVertexes, sizeof( uint16_t ), h2_vertexes_compare );

		uint16_t* it = dupvpos;
		int count = 0;
		int backi = sortvpos[0];
		const struct vertexData_s* backv = &g_drawBuff.vertexes[backi];
		for ( int i = 1; i < g_drawBuff.numVertexes; i++ )
		{
			int fronti = sortvpos[i];
			const struct vertexData_s* frontv = &g_drawBuff.vertexes[fronti];
			if ( 0 == h2_vertexes_compare2( backv, frontv ) )
			{
				remapvpos[fronti] = backi;
				if ( count == 0 )
				{
					count++;
					it[count] = backi;
				}
				count++;
				it[count] = fronti;
			}
			else
			{
				backv = frontv;
				backi = fronti;
				if ( count )
				{
					it[0] = count;
					it += count +1;
					count = 0;
				}
			}
		}
		it[0] = count;
		it[count+1] = 0;
	}

	struct vertexData_s* v = g_drawBuff.vertexes;
	for ( int i = 0; i < g_drawBuff.numVertexes; i++, v++ )
	{
		VectorClear( v->normal );
	}

	for ( int i = 0; i < g_drawBuff.numIndexes; i += 3 )
	{
		uint i0 = g_drawBuff.indexes[i];
		uint i1 = g_drawBuff.indexes[i+1];
		uint i2 = g_drawBuff.indexes[i+2];

		const float *v0 = g_drawBuff.vertexes[i0].xyz;
		const float *v1 = g_drawBuff.vertexes[i1].xyz;
		const float *v2 = g_drawBuff.vertexes[i2].xyz;

		vec3_t e1, e2, normal;
		VectorSubtract( v0, v1, e1 );
		VectorSubtract( v2, v1, e2 );
		CrossProduct( e1, e2, normal );
		VectorNormalize( normal );
		
		const uint16_t* param = 1 ? NULL : remapvpos;

		MergeNormal( normal, i0, param);
		MergeNormal( normal, i1, param);
		MergeNormal( normal, i2, param);
	}

	uint16_t* it = dupvpos;
	while ( it[0] )
	{
		int count = it[0];
		it++;

		uint32_t visited = 0;
		uint32_t visitend = (1 << count) - 1;
		for ( int i = 0; i < count && visited != visitend; i++ )
		{
			vec3_t sum;
			uint32_t visitnow = 0;
			int indexi = it[i];
			visited |= 1 << i;
			float* ni = g_drawBuff.vertexes[indexi].normal;
			VectorCopy(ni, sum);
			for ( int j = i + 1; j < count; j++ )
			{
				uint32_t visitid = 1 << j;
				if (visitid & visited)
					continue;

				int indexj = it[j];
				float* nj = g_drawBuff.vertexes[indexj].normal;
				float dot = DotProduct( nj, ni );
				if ( dot > 0.01f )
				{
					VectorAdd(nj, sum, sum);
					remapvpos[indexj] = indexi;
					visitnow |= visitid;
				}
				else if (remapvpos[indexj] == indexi)
				{
					remapvpos[indexj] = indexj;
				}
			}

			VectorNormalize(sum);
			VectorCopy(sum, ni);
			for (int j = i + 1; j < count; j++)
			{
				uint32_t visitid = 1 << j;
				if (visitid & visitnow)
				{
					int indexj = it[j];
					float* nj = g_drawBuff.vertexes[indexj].normal;
					VectorCopy(sum, nj);
				}
			}

			visited |= visitnow;
		}

		//for (int i = 0; i < count; i++)
		//{
		//	int index = it[i];
		//	if (remapvpos[index] != index)
		//	{
		//		float* ni = g_drawBuff.vertexes[index].normal;
		//		MergeNormal(ni, remapvpos[index], NULL);
		//	}
		//}

		//for (int i = 0; i < count; i++)
		//{
		//	int index = it[i];
		//	if (remapvpos[index] != index)
		//	{
		//		float* ni = g_drawBuff.vertexes[index].normal;
		//		float* nr = g_drawBuff.vertexes[remapvpos[index]].normal;
		//		VectorCopy(nr, ni);
		//	}
		//}

		it += count;
	}
}

static void h2_draw_normals()
{
	int oldtexture = currenttexture[currenttmu];
	if ( r_whiteimage == NULL )
		r_whiteimage = GL_FindImage( "textures/general/white.m8", it_wall );
	GL_MBind(GL_TEXTURE0, r_whiteimage->texnum);
	//glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
	glColor3f( 1, 1, 1 );

	glBegin( GL_LINES );

	for ( int i = 0; i < g_drawBuff.numVertexes; i++ )
	{
		const float* vsrc = g_drawBuff.vertexes[i].xyz;
		const float* normals = g_drawBuff.vertexes[i].normal;
		vec3_t tmp;
		glVertex3fv( vsrc );
		VectorMA( vsrc, 3, normals, tmp);
		glVertex3fv( tmp );
	}

	glEnd();
	GL_MBind(GL_TEXTURE0, oldtexture);

	//glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}

OPENGL_API void WINAPI glClear( GLbitfield mask );
OPENGL_API void WINAPI glClearColor( GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha );
OPENGL_API void WINAPI glEnableClientState( GLenum cap );
OPENGL_API void WINAPI glVertexPointer( GLint size, GLenum type, GLsizei stride, const GLvoid* pointer );
OPENGL_API void WINAPI glNormalPointer( GLenum type, GLsizei stride, const GLvoid *pointer );
OPENGL_API void WINAPI glClientActiveTexture( GLenum texture );
OPENGL_API void WINAPI glColorPointer( GLint size, GLenum type, GLsizei stride, const GLvoid* pointer );
OPENGL_API void WINAPI glTexCoordPointer( GLint size, GLenum type, GLsizei stride, const GLvoid* pointer );
OPENGL_API void WINAPI glDrawArrays( GLenum mode, GLint first, GLsizei count );
OPENGL_API void WINAPI glDrawElements( GLenum mode, GLsizei count, GLenum type, const GLvoid* indices );
OPENGL_API void WINAPI glDisableClientState( GLenum cap );

static void h2_draw_triangles()
{
	int oldtexture = currenttexture[currenttmu];
	if ( r_whiteimage == NULL )
		r_whiteimage = GL_FindImage( "textures/general/white.m8", it_wall );
	GL_MBind(GL_TEXTURE0, r_whiteimage->texnum);
	glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
	glColor3f( 1, 1, 1 );

	glEnableClientState( GL_VERTEX_ARRAY );
	glVertexPointer( 3, GL_FLOAT, sizeof( struct vertexData_s ), g_drawBuff.vertexes[0].xyz );
	glDrawElements( GL_TRIANGLES, g_drawBuff.numIndexes, GL_UNSIGNED_SHORT, g_drawBuff.indexes );
	glDisableClientState( GL_VERTEX_ARRAY );

	GL_MBind(GL_TEXTURE0, oldtexture);

	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}

static void R_RenderSurfs( int flags )
{
	if ( g_drawBuff.numIndexes )
	{
		c_brush_polys++;

		if ( flags & RECALC_NORMALS || rmx_normals->value >= 2 )
		{
			h2_recalculate_normals();
			if ( rmx_normals->value >= 3 )
				flags |= RENDER_NORMALS;
		}

		glEnableClientState( GL_VERTEX_ARRAY );
		glVertexPointer( 3, GL_FLOAT, sizeof( struct vertexData_s ), g_drawBuff.vertexes[0].xyz );
		if ( flags & RENDER_NORMALS )
		{
			glEnableClientState( GL_NORMAL_ARRAY );
			glNormalPointer( GL_FLOAT, sizeof( struct vertexData_s ), g_drawBuff.vertexes[0].normal );
		}
		glEnableClientState( GL_COLOR_ARRAY );
		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( struct vertexData_s ), g_drawBuff.vertexes[0].clr.b );
		glClientActiveTexture( GL_TEXTURE0_ARB );
		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		glTexCoordPointer( 2, GL_FLOAT, sizeof( struct vertexData_s ), g_drawBuff.vertexes[0].tex0 );
		if ( flags & RENDER_TWOTEXTURES )
		{
			glClientActiveTexture( GL_TEXTURE1_ARB );
			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glTexCoordPointer( 2, GL_FLOAT, sizeof( struct vertexData_s ), g_drawBuff.vertexes[0].tex1 );
		}
		//glDrawArrays( GL_POLYGON, 0, numvert );
		glDrawElements( GL_TRIANGLES, g_drawBuff.numIndexes, GL_UNSIGNED_SHORT, g_drawBuff.indexes );
		if ( flags & RENDER_TWOTEXTURES )
		{
			glDisableClientState( GL_TEXTURE_COORD_ARRAY );
			glClientActiveTexture( GL_TEXTURE0_ARB );
		}
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		glDisableClientState( GL_COLOR_ARRAY );
		glDisableClientState( GL_NORMAL_ARRAY );
		glDisableClientState( GL_VERTEX_ARRAY );

		if ( gl_shownormals->value && (flags & RENDER_NORMALS) )
		{
			h2_draw_normals();
		}

		if ( gl_showtris->value )
		{
			h2_draw_triangles();
		}

		g_drawBuff.numIndexes = 0;
		g_drawBuff.numVertexes = 0;
	}
}

#define TURBSCALE (float)(256.0f / (2 * M_PI))

static void hk_R_EmitWaterPolys(msurface_t* fa, qboolean undulate)
{
	HOOK_ONLINE_NOTICE();

	glpoly_t	*p, *bp;
	float		rdt = /*r_newrefdef.time*/r_newrefdef_time;
	float		scroll;
	uint32_t	render_flags = 0;
	DWORD		color = D3DCOLOR_ARGB( 255, 255, 255, 255 );

	//take color from previous calls to glColor
	//if ( D3DState.CurrentState.isSet.bits.color )
		color = D3DState.CurrentState.currentColor;
	
	if (fa->texinfo->flags & SURF_FLOWING)
		scroll = -64.0f * ((rdt * 0.5f) - floorf(rdt * 0.5f));
	else
		scroll = 0.0f;

	for (bp = fa->polys; bp != NULL; bp = bp->next)
	{
		p = bp;

		struct vertexData_s* vb;
		unsigned short* ib;
		unsigned alpha = 255;

		int totalindexes = (3 * p->numverts) - 6;
		R_CheckDrawBufferSpace( p->numverts, totalindexes, render_flags );

		int i;
		int index = g_drawBuff.numVertexes;
		ib = &g_drawBuff.indexes[g_drawBuff.numIndexes];
		vb = &g_drawBuff.vertexes[g_drawBuff.numVertexes];
		int start = index;

		//glBegin(GL_TRIANGLE_FAN);

		float* v = p->verts[0];
		for ( i = 0; i < p->numverts; i++, v += VERTEXSIZE )
		{
			if ( i > 2 )
			{
				ib[0] = start;
				ib[1] = index - 1;
				ib += 2;
			}
			ib[0] = index++;
			
			vb->clr.all = color;

			const float os = v[3];
			const float ot = v[4];

			float s = os + r_turbsin[Q_ftol( (ot * 0.125f + rdt) * TURBSCALE ) & 255];
			s += scroll;
			s *= (1.0/64);

			float t = ot + r_turbsin[Q_ftol( (os * 0.125f + rdt) * TURBSCALE ) & 255];
			t *= (1.0/64);

			//glTexCoord2f(s, t);
			vb->tex0[0] = s;
			vb->tex0[1] = t;

			//glVertex3fv(v);
			VectorCopy( v, vb->xyz );

			if ( undulate )
			{
				vb->xyz[2] += r_turbsin[Q_ftol( ((v[0] * 2.3f + v[1]) * 0.015f + rdt * 3.0f) * TURBSCALE ) & 255] * 0.25f +
					r_turbsin[Q_ftol( ((v[1] * 2.3f + v[0]) * 0.015f + rdt * 6.0f) * TURBSCALE ) & 255] * 0.125f;
			}

			vb++;
			ib++;
		}
		//glEnd();
		g_drawBuff.numVertexes += p->numverts;
		g_drawBuff.numIndexes += totalindexes;
	}

	R_RenderSurfs( render_flags );
}

static void hk_R_EmitUnderwaterPolys(msurface_t* fa)
{
	HOOK_ONLINE_NOTICE();

	glpoly_t	*p, *bp;
	float		rdt = /*r_newrefdef.time*/r_newrefdef_time;
	//float		scroll;
	uint32_t	render_flags = 0;
	DWORD		color = D3DCOLOR_ARGB( 255, 255, 255, 255 );

	//take color from previous calls to glColor
	//if ( D3DState.CurrentState.isSet.bits.color )
		color = D3DState.CurrentState.currentColor;

	for (bp = fa->polys; bp != NULL; bp = bp->next)
	{
		p = bp;

		struct vertexData_s* vb;
		unsigned short* ib;
		unsigned alpha = 255;

		int totalindexes = (3 * p->numverts) - 6;
		R_CheckDrawBufferSpace( p->numverts, totalindexes, render_flags );

		int i;
		int index = g_drawBuff.numVertexes;
		ib = &g_drawBuff.indexes[g_drawBuff.numIndexes];
		vb = &g_drawBuff.vertexes[g_drawBuff.numVertexes];
		int start = index;

		//glBegin(GL_TRIANGLE_FAN);

		float* v = p->verts[0];
		for ( i = 0; i < p->numverts; i++, v += VERTEXSIZE )
		{
			if ( i > 2 )
			{
				ib[0] = start;
				ib[1] = index - 1;
				ib += 2;
			}
			ib[0] = index++;

			vb->clr.all = color;

			//glTexCoord2f(s, t);
			vb->tex0[0] = v[3];
			vb->tex0[1] = v[4];

			//glVertex3fv(v);
			VectorCopy( v, vb->xyz );

			vb->xyz[2] += r_turbsin[Q_ftol( ((v[0] * 2.3f + v[1]) * 0.015f + rdt * 3.0f) * TURBSCALE ) & 255] * 0.5f +
					r_turbsin[Q_ftol( ((v[1] * 2.3f + v[0]) * 0.015f + rdt * 6.0f) * TURBSCALE ) & 255] * 0.25f;

			vb++;
			ib++;
		}
		//glEnd();
		g_drawBuff.numVertexes += p->numverts;
		g_drawBuff.numIndexes += totalindexes;
	}

	R_RenderSurfs( render_flags );
}

static void hk_R_EmitQuakeFloorPolys(msurface_t* fa)
{
	HOOK_ONLINE_NOTICE();

	glpoly_t	*p, *bp;
	float		rdt = /*r_newrefdef.time*/r_newrefdef_time;
	//float		scroll;
	uint32_t	render_flags = 0;
	DWORD		color = D3DCOLOR_ARGB( 255, 255, 255, 255 );

	//take color from previous calls to glColor
	//if ( D3DState.CurrentState.isSet.bits.color )
	color = D3DState.CurrentState.currentColor;

	for (bp = fa->polys; bp != NULL; bp = bp->next)
	{
		p = bp;

		struct vertexData_s* vb;
		unsigned short* ib;
		unsigned alpha = 255;

		int totalindexes = (3 * p->numverts) - 6;
		R_CheckDrawBufferSpace( p->numverts, totalindexes, render_flags );

		int i;
		int index = g_drawBuff.numVertexes;
		ib = &g_drawBuff.indexes[g_drawBuff.numIndexes];
		vb = &g_drawBuff.vertexes[g_drawBuff.numVertexes];
		int start = index;

		//glBegin(GL_TRIANGLE_FAN);

		float* v = p->verts[0];
		for ( i = 0; i < p->numverts; i++, v += VERTEXSIZE )
		{
			if ( i > 2 )
			{
				ib[0] = start;
				ib[1] = index - 1;
				ib += 2;
			}
			ib[0] = index++;

			vb->clr.all = color;

			//glTexCoord2f(s, t);
			vb->tex0[0] = v[3];
			vb->tex0[1] = v[4];

			//glVertex3fv(v);
			VectorCopy( v, vb->xyz );

			vb->xyz[2] += r_turbsin[Q_ftol( ((v[0] * 2.3f + v[1]) * 0.015f + rdt * 3.0f) * TURBSCALE ) & 255] * (quake_amount->value * 0.05f) * 0.5f +
				r_turbsin[Q_ftol( ((v[1] * 2.3f + v[0]) * 0.015f + rdt * 6.0f) * TURBSCALE ) & 255] * (quake_amount->value * 0.05f) * 0.25f;

			vb++;
			ib++;
		}
		//glEnd();
		g_drawBuff.numVertexes += p->numverts;
		g_drawBuff.numIndexes += totalindexes;
	}

	R_RenderSurfs( render_flags );
}

void h2_refgl_frame_ended()
{
	if ( rmx_dyn_linger->modified )
	{
		rmx_dyn_linger->modified = qfalse;

		qdx_lights_dynamic_linger( int(rmx_dyn_linger->value) );
	}

	if ( rmx_coronas->modified && !rmx_coronas->value )
	{
		qdx_lights_clear( LIGHT_CORONA );
	}

	glClearColor( 0, 0, 0, 0 );
	//glClear( GL_COLOR_BUFFER_BIT );
}

typedef struct ResMngr_Block_s
{
	char *start;
	unsigned int size;
	struct ResMngr_Block_s *next;
} ResMngr_Block_t;

typedef struct ResourceManager_s
{
	size_t resSize;
	unsigned int resPerBlock;
	unsigned int nodeSize;
	struct ResMngr_Block_s *blockList;
	char **free;
} ResourceManager_t;

typedef struct SinglyLinkedList_s
{
	struct SinglyLinkedListNode_s *rearSentinel;
	struct SinglyLinkedListNode_s *front;
	struct SinglyLinkedListNode_s *current;
} SinglyLinkedList_t;

#define VID_NUM_MODES 20

static resolution_info_t vid_resolutions[VID_NUM_MODES] = { 
	"320 240  ", 320, 240,
	"400 300  ", 400, 300,
	"512 384  ", 512, 384,
	"640 480  ", 640, 480,
	"800 600  ", 800, 600,
	"960 720  ", 960, 720,
	"1024 768 ", 1024, 768,
	"1152 864 ", 1152, 864,
	"1280 960 ", 1280, 960,
	"1600 1200", 1600, 1200
};
static int vid_resolutions_found = 10;
static int vid_resolutions_initialised = 0;
static const char* vid_resolutions_struct[VID_NUM_MODES +1] =
{
	"320 240  ",
	"400 300  ",
	"512 384  ",
	"640 480  ",
	"800 600  ",
	"960 720  ",
	"1024 768 ",
	"1152 864 ",
	"1280 960 ",
	"1600 1200",
	0
};

static void h2_vid_initialise_resolutions();

static void hk_ResMngr_DeallocateResource( ResourceManager_t *resource, void *toDeallocate, size_t size );// = 0x2650
static void *fp_ResMngr_DeallocateResource = 0;
static void hk_SLList_Des( SinglyLinkedList_t* this_ptr );// = 0x26d0
static void *fp_SLList_Des = 0;
static ResourceManager_t *dp_res_mgr;// = 0x10f20
static void hk_R_BeginRegistration( const char* model ); // = 0x6fb0
static void (*fp_R_BeginRegistration) ( const char* model ) = 0;
static void hk_R_RenderView( int* refdef );// = 0x8910
static void (*fp_R_RenderView)( int* refdef ) = 0;
static void hk_R_EmitWaterPolys(msurface_t* fa, qboolean undulate);// = 0xeb60
static void (*fp_R_EmitWaterPolys)(msurface_t* fa, qboolean undulate) = 0;
static void hk_R_EmitUnderwaterPolys(msurface_t* fa);// = 0xed50
static void (*fp_R_EmitUnderwaterPolys)(msurface_t* fa) = 0;
static void hk_R_EmitQuakeFloorPolys(msurface_t* fa);// = 0xee70
static void (*fp_R_EmitQuakeFloorPolys)(msurface_t* fa) = 0;
static qboolean hk_VID_GetModeInfo( int* width, int* height, const int mode );// = 0x34bc0
static qboolean (*fp_VID_GetModeInfo)( int* width, int* height, const int mode ) = 0;
static void hk_calcnormals( int param_1, float param_2, float param_3, int param_4, int param_5, float* param_6 );
static void (*fp_calcnormals)( int param_1, float param_2, float param_3, int param_4, int param_5, float* param_6 );// = 0x2a90
static void (*fp_IN_DeactivateMouse)();// = 0x1d240
static byte** dp_sv_client;// = 0x7becc
#define sv_client *(dp_sv_client)

#define REFDEF_VIEWORG_OFF 7
#define REFDEF_CLIENTVIEWORG_OFF 10
#define REFDEF_VIEWANGLES_OFF 13
#define REFDEF_NUMENTITIES_OFF 24
#define REFDEF_ENTITIES_OFF 25
#define REFDEF_NUMAENTITIES_OFF 26
#define REFDEF_AENTITIES_OFF 27
#define REFDEF_NUMLIGHTS_OFF 28
#define REFDEF_LIGHTS_OFF 29
#define REFDEF_NUMPARTICLES_OFF 30
#define REFDEF_PARTICLES_OFF 31
#define REFDEF_NUMAPARTICLES_OFF 32
#define REFDEF_APARTICLES_OFF 33

static void* hk_malloc( size_t size );
static void* (*fp_malloc)( size_t ) = 0;
static void hk_free(void* block);
static void (*fp_free)(void*) = 0;

typedef LONG (WINAPI *DetourAction_FP)(_Inout_ PVOID *ppPointer, _In_ PVOID pDetour);
static void h2_detour_action( DetourAction_FP );
static gameparam_t __cdecl h2_implement_api( gameops_t op, gameparam_t p0, gameparam_t p1, gameparam_t p2 );

static void h2_generic_fixes()
{
	const char* modulename = "H2Common.dll";
	MODULEINFO h2common_data, quake2_data;

	modulename = "H2Common.dll";
	ZeroMemory( &h2common_data, sizeof( h2common_data ) );
	HMODULE h2comndll = GetModuleHandle( modulename );
	if ( !h2comndll || !GetModuleInformation( GetCurrentProcess(), h2comndll, &h2common_data, sizeof( h2common_data ) ) )
	{
		logPrintf( "h2_generic_fixes: failed to get %s moduleinfo\n", modulename );
		return;
	}

	modulename = "quake2.dll";
	ZeroMemory( &quake2_data, sizeof( quake2_data ) );
	HMODULE quake2dll = GetModuleHandle( modulename );
	if ( !quake2dll || !GetModuleInformation( GetCurrentProcess(), quake2dll, &quake2_data, sizeof( quake2_data ) ) )
	{
		logPrintf( "h2_generic_fixes: failed to get %s moduleinfo\n", modulename );
		return;
	}

	//The following 2 should already be fixed in H2Common.dll if that one is installed
	fp_ResMngr_DeallocateResource = (byte*)h2common_data.lpBaseOfDll + 0x2650;
	fp_SLList_Des = (byte*)h2common_data.lpBaseOfDll + 0x26d0;
	dp_res_mgr = (ResourceManager_t *)((byte*)h2common_data.lpBaseOfDll + 0x10f20);
	fp_R_BeginRegistration = PTR_FROM_OFFSET( void( *)(const char *), 0x6fb0 );
	fp_R_RenderView = PTR_FROM_OFFSET( void( *)(int *), 0x8910 );
	fp_R_EmitWaterPolys = PTR_FROM_OFFSET( void( *)(msurface_t *, qboolean), 0xeb60 );
	fp_R_EmitUnderwaterPolys = PTR_FROM_OFFSET( void( *)(msurface_t *), 0xed50 );
	fp_R_EmitQuakeFloorPolys = PTR_FROM_OFFSET( void( *)(msurface_t *), 0xee70 );
	fp_VID_GetModeInfo = (qboolean( *)(int*, int*, int))((byte*)quake2_data.lpBaseOfDll + 0x34bc0);
	//fp_calcnormals = PTR_FROM_OFFSET( void (*)(int,float,float,int,int,float*), 0x2a90 );
	fp_IN_DeactivateMouse = (void(*)())((byte*)quake2_data.lpBaseOfDll + 0x1d240);
	dp_sv_client = (byte**)((byte*)quake2_data.lpBaseOfDll + 0x7becc);

	//fp_malloc = (void*(*)(size_t))DetourFindFunction( modulename, "malloc" );
	//fp_free = (void(*)(void*))DetourFindFunction( modulename, "free" );
	//if ( !fp_malloc || !fp_free )
	//{
	//	logPrintf( "h2_generic_fixes: failed to find ptr malloc/free\n" );
	//}

	if ( D3DGlobal_ReadGameConfPtr( "patch_h2_gamestart" ) )
	{
		byte* code = (byte*)quake2_data.lpBaseOfDll + 0x2da85;
		if ( code[0] == 0x74 )
		{
			unsigned long restore;
			if ( hook_unprotect( code, 1, &restore ) )
			{
				code[0] = 0xeb;
				hook_protect( code, 1, restore );

				logPrintf( "h2_generic_fixes: game start was patched\n" );
			}
		}
	}

	//sometimes quake2.dll calls into CLientEffects when that dll is unloaded, usually at NewGame
	//let's prevent ClientEffects from being unloaded
	if ( D3DGlobal_ReadGameConfPtr( "patch_h2_unloadclientdll" ) )
	{
		//CLFX_LoadDll calls Sys_UnloadGameDll
		byte* code = (byte*)quake2_data.lpBaseOfDll + 0x33f4c;
		if ( code[0] == 0xe8 )
		{
			unsigned long restore;
			if ( hook_unprotect( code, 5, &restore ) )
			{
				memset( code, 0x90, 5 );
				hook_protect( code, 5, restore );

				logPrintf( "h2_generic_fixes: CLFX_LoadDll was patched\n" );
			}
		}
	}

	{
		// Replace resolution strings
		//14 07 06 10
		byte* code = (byte*)quake2_data.lpBaseOfDll + 0x3f9a8; //address of resolutions
		intptr_t value;
		memcpy( &value, code, sizeof( value ) );
		if ( value == 0x10060714 )
		{
			unsigned long restore;
			if ( hook_unprotect( code, sizeof( value ), &restore ) )
			{
				value = (intptr_t)vid_resolutions_struct;
				memcpy( code, &value, sizeof( value ) );
				hook_protect( code, sizeof( value ), restore );

				logPrintf( "h2_generic_fixes: resolutions was patched\n" );
			}

		}
		else
			logPrintf( "h2_generic_fixes: the resolutions ptr does not match %x\n", value );
	}

	h2_detour_action( DetourAttach );

	rmx_set_game_api(h2_implement_api);
}

static void h2_detour_action(DetourAction_FP DetourAction)
{
#define CHECK_ABORT(FN) if ( error != NO_ERROR ) \
	{ abort=true; logPrintf( "h2_detour_action: %s failed for %s(%d)\n", (DetourAction == DetourAttach ? "Attach" : "Detach"), (FN), error ); break; }

	LONG error = NO_ERROR;
	bool abort = false;
	do {
		DetourTransactionBegin();
		DetourUpdateThread( GetCurrentThread() );

		if ( fp_ResMngr_DeallocateResource )
		{
			error = DetourAction( &(PVOID&)fp_ResMngr_DeallocateResource, hk_ResMngr_DeallocateResource );
			CHECK_ABORT( "ResMngr_DeallocateResource" );
		}
		if ( fp_SLList_Des )
		{
			error = DetourAction( &(PVOID&)fp_SLList_Des, hk_SLList_Des );
			CHECK_ABORT( "SLList_Des" );
		}
		if ( fp_R_BeginRegistration )
		{
			error = DetourAction( &(PVOID&)fp_R_BeginRegistration, hk_R_BeginRegistration );
			CHECK_ABORT( "R_BeginRegistration" );
		}
		if ( fp_R_RenderView )
		{
			error = DetourAction( &(PVOID&)fp_R_RenderView, hk_R_RenderView );
			CHECK_ABORT( "R_RenderView" );
		}
		if ( fp_R_EmitWaterPolys )
		{
			error = DetourAction( &(PVOID&)fp_R_EmitWaterPolys, hk_R_EmitWaterPolys );
			CHECK_ABORT( "R_EmitWaterPolys" );
		}
		if ( fp_R_EmitUnderwaterPolys )
		{
			error = DetourAction( &(PVOID&)fp_R_EmitUnderwaterPolys, hk_R_EmitUnderwaterPolys );
			CHECK_ABORT( "R_EmitUnderwaterPolys" );
		}
		if ( fp_R_EmitQuakeFloorPolys )
		{
			error = DetourAction( &(PVOID&)fp_R_EmitQuakeFloorPolys, hk_R_EmitQuakeFloorPolys );
			CHECK_ABORT( "R_EmitQuakeFloorPolys" );
		}
		if ( fp_VID_GetModeInfo )
		{
			error = DetourAction( &(PVOID&)fp_VID_GetModeInfo, hk_VID_GetModeInfo );
			CHECK_ABORT( "VID_GetModeInfo" );
		}
		if ( fp_calcnormals )
		{
			error = DetourAction( &(PVOID&)fp_calcnormals, hk_calcnormals );
			CHECK_ABORT( "calcnormals" );
		}
		if ( fp_malloc )
		{
			error = DetourAction( &(PVOID&)fp_malloc, hk_malloc );
			CHECK_ABORT( "malloc" );
		}
		else
		{
			//fp_malloc = malloc;
		}
		if ( fp_free )
		{
			error = DetourAction( &(PVOID&)fp_free, hk_free );
			CHECK_ABORT( "free" );
		}
		else
		{
			//fp_free = free;
		}
	} while ( 0 );

	if ( abort )
	{
		error = DetourTransactionAbort();
		if ( error != NO_ERROR ) logPrintf( "h2_generic_fixes: DetourTransactionAbort failed: %d \n", error );
	}
	else
	{
		error = DetourTransactionCommit();
		if ( error != NO_ERROR ) logPrintf( "h2_generic_fixes: DetourTransactionCommit failed: %d \n", error );
	}
}

static void h2_generic_fixes_deinit()
{
	h2_detour_action( DetourDetach );
	rmx_set_game_api( NULL );
}

static void h2_vid_initialise_resolutions()
{
	if ( vid_resolutions_initialised == 0 )
	{
		int found = D3DGlobal_GetResolutions( vid_resolutions, VID_NUM_MODES );

		if ( found )
		{
			vid_resolutions_initialised = 1;

			int i;
			for ( i = 0; i < found; i++ )
			{
				vid_resolutions_struct[i] = vid_resolutions[i].display_name;
			}
			vid_resolutions_struct[i] = 0;

			vid_resolutions_found = found;
		}
	}
}

static qboolean hk_VID_GetModeInfo(int* width, int* height, const int mode)
{
	HOOK_ONLINE_NOTICE();

	if ( vid_resolutions_initialised == 0 )
	{
		h2_vid_initialise_resolutions();
	}

	if (mode >= 0 && mode < vid_resolutions_found)
	{
		*width =  vid_resolutions[mode].width;
		*height = vid_resolutions[mode].height;

		return qtrue;
	}

	return qfalse;
}

std::map<uint32_t, int> g_halosvalidation;

static void hk_R_BeginRegistration( const char* model )
{
	HOOK_ONLINE_NOTICE();

	static std::string mapname("");
	if ( mapname.compare( model ) != 0 )
	{
		//do some cleaning
		g_halosvalidation.clear();
		qdx_begin_loading_map( model );

		mapname.assign( model );
	}
	
	fp_R_BeginRegistration( model );
}

typedef struct paletteRGBA_s
{
	union
	{
		struct
		{
			byte r;
			byte g;
			byte b;
			byte a;
		};
		uint c;
		byte c_array[4];
	};
} paletteRGBA_t;

typedef struct
{
	vec3_t origin;
	paletteRGBA_t color;
	float scale;
} particle_t;

typedef struct entity_s
{
	struct model_s** model; // Opaque type outside refresh. // Q2: struct model_s*
	float angles[3];
	float origin[3];
	int frame;

	// Model scale.
	float scale;

	// Scale of model - but only for client entity models - not server-side models.
	// Required for scaling mins and maxs that are used to cull models - mins and maxs are scaled on the server side,
	// but not on the client side when the models are loaded in.
	float cl_scale;

	// Distance to the camera origin, gets set every frame by AddEffectsToView.
	float depth;

	paletteRGBA_t color;
	int flags;

	//a lot of other stuff which we don't need so far, sice we access this struct from a list of pointers
} entity_t;

// angle indexes
#define PITCH               0       // up / down
#define YAW                 1       // left / right
#define ROLL                2       // fall over

void AngleVectors( const vec3_t angles, vec3_t forward )
{
	float angle;
	float sr, sp, sy, cr, cp, cy;

	angle = angles[YAW] * float(M_PI * 2 / 360);
	sy = sin( angle );
	cy = cos( angle );
	angle = angles[PITCH] * float(M_PI * 2 / 360);
	sp = sin( angle );
	cp = cos( angle );

	if ( forward ) {
		forward[0] = cp * cy;
		forward[1] = cp * sy;
		forward[2] = -sp;
	}
}

static void hk_R_RenderView( int* refdef )
{
	HOOK_ONLINE_NOTICE();

	if ( r_worldmodel != NULL )
	{
		//grab lights
		int numLights = refdef[REFDEF_NUMLIGHTS_OFF];
		particle_t* dlights = (particle_t*)refdef[REFDEF_LIGHTS_OFF];

		for ( int i = 0; i < numLights; i++, dlights++ )
		{
			paletteRGBA_t clrb = dlights->color;
			float color[3] = { (float)clrb.r / 255, (float)clrb.g / 255, (float)clrb.b / 255 };
			qdx_light_add( LIGHT_DYNAMIC, i, dlights->origin, NULL, color, dlights->scale );
		}

		int numEntities = refdef[REFDEF_NUMENTITIES_OFF];
		entity_t** entities = (entity_t**)refdef[REFDEF_ENTITIES_OFF];

		int numAlphaEntities = refdef[REFDEF_NUMAENTITIES_OFF];
		entity_t** alpha_entities = (entity_t**)refdef[REFDEF_AENTITIES_OFF];

#define HALO_STR "sprites/lens/halo"
#define FLARE_STR "sprites/lens/flare"

		if ( rmx_coronas->value )
		{
			for ( int i = 0; i < numAlphaEntities; i++, alpha_entities++ )
			{
				model_t** model = (*alpha_entities)->model;
				if ( 0 == strncmp( HALO_STR, (*model)->name, sizeof( HALO_STR ) -1 ) )
				{
					paletteRGBA_t clrb = (*alpha_entities)->color;
					uint32_t hash = fnv_32a_buf( &clrb, sizeof( clrb ), 42 );
					hash = fnv_32a_buf( (*alpha_entities)->origin, 3*sizeof( float ), hash );
					auto it = g_halosvalidation.find( hash );
					if ( it != g_halosvalidation.end() )
					{
						if ( it->second >= 3 )
						{
							it = g_halosvalidation.erase( it );

							float color[3] = { (float)clrb.r / 255, (float)clrb.g / 255, (float)clrb.b / 255 };
							int added = qdx_light_add( LIGHT_CORONA, i, (*alpha_entities)->origin, NULL, color, (*alpha_entities)->scale );
							//if ( added )
							//{
							//	riPRINTF( PRINT_ALL, "aded light for %s\n", (*model)->name );
							//}
						}
						else
						{
							it->second += 2;
						}
					}
					else
					{
						g_halosvalidation[hash] = 1;
					}
				}
			}
		}

		auto it = g_halosvalidation.begin();
		while ( it != g_halosvalidation.end() )
		{
			if ( it->second <= 0 )
			{
				it = g_halosvalidation.erase( it );
			}
			else
			{
				it->second--;

				it++;
			}
		}

		//this one gets bad pointers when loading game
		//player origin
		//float* origin = (float*)(sv_client + 4);
		//byte** client = (byte**)(sv_client + 0x1ac);
		//float* aimangles = (float*)(*client + 0x1834 + 3*sizeof( float ));
		//float forward[3];
		//AngleVectors( aimangles, forward );
		//rmx_setplayerpos( origin, forward );
		float forward[3];
		AngleVectors( (float*)&refdef[REFDEF_VIEWANGLES_OFF], forward );
		rmx_setplayerpos( (float*)&refdef[REFDEF_CLIENTVIEWORG_OFF], forward );
	}

	fp_R_RenderView( refdef );
}

static void hk_calcnormals( int param_1, float param_2, float param_3, int param_4, int param_5, float* param_6 )
{
	HOOK_ONLINE_NOTICE();

	g_calcnormals = param_6;
	fp_calcnormals( param_1, param_2, param_3, param_4, param_5, param_6 );
}

static gameparam_t __cdecl h2_implement_api( gameops_t op, gameparam_t p0, gameparam_t p1, gameparam_t p2 )
{
	gameparam_t ret = { 0 };

	switch ( op )
	{
	case OP_GETVAR: {
		cvarq2_t* cv = riCVAR_GET(p0.strval, "0", 0 );
		ret = int(cv->value);
		break; }
	case OP_SETVAR:
		riCVAR_SET( p0.strval, p1.strval );
		break;
	case OP_EXECMD:
		riEXEC_TEXT( EXEC_APPEND, p0.strval );
		break;
	case OP_CONPRINT: {
		riPRINTF( PRINT_ALL, "%s", p1.strval );
		break; }
	case OP_DEACTMOUSE:
		fp_IN_DeactivateMouse();
		break;
	default:
		riPRINTF(PRINT_WARNING, "Unsupported OP:%d\n", op );
		break;
	}

	return ret;
}

static void hk_ResMngr_DeallocateResource( ResourceManager_t *resource, void *toDeallocate, size_t size )
{
	HOOK_ONLINE_NOTICE();

	char **toPush = NULL;

	if ( toDeallocate )
	{
		toPush = (char **)(toDeallocate)-1;

		*toPush = (char *)(resource->free);

		resource->free = toPush;
	}
	else
	{
		//__debugbreak();
		DO_ONCE()
		{
			riPRINTF( PRINT_ALL, "DeallocateResource: null ptr\n" );
		}
	}
}

typedef union GenericUnion4_u
{
	byte t_byte;
	short t_short;
	int t_int;
	unsigned int t_uint;
	float t_float;
	float *t_float_p;
	struct edict_s *t_edict_p;
	void *t_void_p;
	paletteRGBA_t t_RGBA;
} GenericUnion4_t;

typedef struct SinglyLinkedListNode_s
{
	union GenericUnion4_u data;
	struct SinglyLinkedListNode_s* next;
} SinglyLinkedListNode_t;

#define ResMngr_DeallocateResource hk_ResMngr_DeallocateResource
#define SLL_NODE_SIZE sizeof(SinglyLinkedListNode_t)

static void hk_SLList_Des(SinglyLinkedList_t* this_ptr)
{
	HOOK_ONLINE_NOTICE();

	if ( this_ptr )
	{
		SinglyLinkedListNode_t* node = this_ptr->front;
		while ( node && node != this_ptr->rearSentinel )
		{
			SinglyLinkedListNode_t* next = node->next;
			ResMngr_DeallocateResource( /*&res_mgr*/dp_res_mgr, node, SLL_NODE_SIZE );
			node = next;
		}

		ResMngr_DeallocateResource( /*&res_mgr*/dp_res_mgr, this_ptr->rearSentinel, SLL_NODE_SIZE );
		this_ptr->front = this_ptr->current = this_ptr->rearSentinel = NULL;
	}
}

#define ALLOC_EXTRA_BYTES 512
static uint32_t g_alloc_count = 1;
struct allocb_s
{
	byte pre[ALLOC_EXTRA_BYTES];
	uint32_t id;
	uint32_t size;
};

static void* __cdecl hk_malloc( size_t size )
{
	struct allocb_s *p = (struct allocb_s*)fp_malloc(size + sizeof(struct allocb_s) + ALLOC_EXTRA_BYTES);

	memset( p->pre, 0x5a, ALLOC_EXTRA_BYTES );
	p->id = g_alloc_count++;
	p->size = size;

	byte *ret = (byte*)p + sizeof( *p );
	memset( ret, 0, size );
	memset( ret + size, 0x5a, ALLOC_EXTRA_BYTES );

	return ret;
}

static void __cdecl hk_free( void* block )
{
	struct allocb_s *p = (struct allocb_s *)((byte*)block - sizeof(*p));

	for ( int i = 0; i < ALLOC_EXTRA_BYTES; i++ )
	{
		if ( p->pre[i] != 0x5a )
		{
			//__debugbreak();
		}
	}

	byte *post = (byte*)p + sizeof( *p ) + p->size;

	for ( int i = 0; i < ALLOC_EXTRA_BYTES; i++ )
	{
		if ( post[i] != 0x5a )
		{
			//__debugbreak();
		}
	}

	fp_free( (byte*)p + sizeof( *p ) );
}