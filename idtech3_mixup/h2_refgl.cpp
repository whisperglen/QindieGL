
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
#include <map>

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

#define GL_LIGHTMAP_FORMAT GL_RGBA

#define RI_PRINTF_OFF 4
#define RI_ERROR_OFF 3
#define RI_CVAR_GET 5
#define RI_CVAR_SET 7

#define riPRINTF(LVL,...) ((ri_Printf)dp_ri[RI_PRINTF_OFF])( LVL, __VA_ARGS__ )
#define riERROR(CODE,...) ((ri_Error)dp_ri[RI_ERROR_OFF])( CODE, __VA_ARGS__ )
#define riCVAR_GET(NAME,VAL,FLAGS) ((ri_Cvar_Get)dp_ri[RI_CVAR_GET] )(NAME,VAL,FLAGS)
#define riCVAR_SET(NAME,VAL) ((ri_Cvar_Set)dp_ri[RI_CVAR_SET] )(NAME,VAL)

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

extern "C" {
typedef void	(*ri_Printf) (int print_level, const char *str, ...);
typedef void	(*ri_Error) (int code, char *fmt, ...);
typedef cvarq2_t* (*ri_Cvar_Get) (const char *name, const char *value, int flags);
typedef cvarq2_t* (*ri_Cvar_Set)( const char *name, const char *value );

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
int **dp_currententity;// = 0x5fe7c
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
static cvarq2_t* r_fullbright;
static cvarq2_t* gl_drawflat;
static cvarq2_t* gl_sortmulti;
static cvarq2_t* r_nocull;
static cvarq2_t* gl_dynamic;
static cvarq2_t* rmx_skiplightmaps;
static cvarq2_t* rmx_novis;
static cvarq2_t* rmx_normals;
static cvarq2_t* rmx_generic;

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
static void h2_bridge_to_MeshBuildVertexBuffer();
static void h2_check_crtent_frame_vs_oldframe();
static void R_RecursiveWorldNodeEx( mnode_t* node );
static void R_SortAndDrawSurfaces( drawSurf_t* surfs, int numSurfs );
static void R_AddDrawSurf( msurface_t* surf );

void h2_generic_fixes();

void h2_refgl_init()
{
	ZeroMemory(&ref_gl_data, sizeof(ref_gl_data));
	HMODULE refdll = GetModuleHandle( modulename );
	if (refdll && GetModuleInformation(GetCurrentProcess(), refdll, &ref_gl_data, sizeof(ref_gl_data)))
	{
		logPrintf("h2_refgl_init: ref_gl base:%p size:%d ep:%p \n", ref_gl_data.lpBaseOfDll, ref_gl_data.SizeOfImage, ref_gl_data.EntryPoint);

		//if ( modulesize == ref_gl_data.SizeOfImage ) //doesn't match, disable this; maybe find another way to match the dll version
		{
			dp_r_visframecount = (int*)((intptr_t)0x5fe38 + (intptr_t)ref_gl_data.lpBaseOfDll);
			dp_r_framecount = (int*)((intptr_t)0x5fd20 + (intptr_t)ref_gl_data.lpBaseOfDll);
			modelorg = (float*)((intptr_t)0x5fb40 + (intptr_t)ref_gl_data.lpBaseOfDll);
			dp_r_newrefdef_areabits = (byte**)((intptr_t)0x5fcb8 + (intptr_t)ref_gl_data.lpBaseOfDll);
			dp_r_worldmodel = (model_t**)((intptr_t)0x5fbd0 + (intptr_t)ref_gl_data.lpBaseOfDll);
			dp_r_alpha_surfaces = (msurface_t**)((intptr_t)0x5fb4c + (intptr_t)ref_gl_data.lpBaseOfDll);
			dp_num_sorted_multitextures = (int*)((intptr_t)0x3da08 + (intptr_t)ref_gl_data.lpBaseOfDll);
			dp_ri = (intptr_t*)((intptr_t)0x5fd60 + (intptr_t)ref_gl_data.lpBaseOfDll);
			dp_r_newrefdef_time = (float*)((intptr_t)0x5fcb0 + (intptr_t)ref_gl_data.lpBaseOfDll);
			r_newrefdef_lightstyles = (lightstyle_t*)((intptr_t)0x5fcbc + (intptr_t)ref_gl_data.lpBaseOfDll);
			dp_gl_state_lightmap_textures = (int*)((intptr_t)0x5ff6c + (intptr_t)ref_gl_data.lpBaseOfDll);
			dp_c_brush_polys = (int*)((intptr_t)0x5fd40 + (intptr_t)ref_gl_data.lpBaseOfDll);
			dp_s_lerped = PTR_FROM_OFFSET( float**, 0x5f9cc );
			shadelight = PTR_FROM_OFFSET( float*, 0x383b0 );
			dp_shadedots = PTR_FROM_OFFSET( float**, 0x2e8c8 );
			dp_currententity = PTR_FROM_OFFSET( int**, 0x5fe7c );
			dp_fmodel = PTR_FROM_OFFSET( int**, 0x5f9fc );
			dp_currentskelverts = PTR_FROM_OFFSET( byte**, 0x5f984 );
			bytedirs = PTR_FROM_OFFSET( float*, 0x2a130 );
			dp_r_notexture = PTR_FROM_OFFSET( image_t**, 0x5fbc4 );
			dp_currenttmu = PTR_FROM_OFFSET( int*, 0x5ff78 );
			currenttexture = PTR_FROM_OFFSET( int*, 0x5ff70 );

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

			r_fullbright = riCVAR_GET("r_fullbright", "0", 0);
			gl_drawflat = riCVAR_GET("gl_drawflat", "0", 0);
			gl_sortmulti = riCVAR_GET("gl_sortmulti", "0", 1);
			r_nocull = riCVAR_GET("r_nocull", "0", 0);
			gl_dynamic = riCVAR_GET("gl_dynamic", "1", 0);
			rmx_skiplightmaps = riCVAR_GET("rmx_skiplightmaps", "0", 0);
			rmx_novis = riCVAR_GET("rmx_novis", "1", 0);
			rmx_normals = riCVAR_GET("rmx_normals", "0", 0);
			rmx_generic = riCVAR_GET("rmx_generic", "-1", 0);

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
						hook_protect( code, 7, restore );

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
						byte* src = (byte*)h2_bridge_to_MeshBuildVertexBuffer;
						int nopcnt = 0;
						int i = 0;
						while (i < 471)
						{
							code[i] = src[i];

							if ( code[i] == 0x90 )
								nopcnt++;
							else
								nopcnt = 0;

							if ( nopcnt >= 5 )
								break;

							i++;
						}

						if ( nopcnt == 5 )
						{
							//now that we reached the nop instructions, make a jmp to end of draw loop
							byte* endcall = PTR_FROM_OFFSET( byte*, 0x29d6 ); //this is where the loop ends
							val = endcall - &code[i+1];
							code[i-4] = 0xe9;//jmp relative
							memcpy( &code[i-3], &val, 4 );

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
						//too much comparison
						logPrintf("h2_refgl_init:R_DrawAliasModel: next instris not NOP\n");
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
	if ( modulesize == ref_gl_data.SizeOfImage )
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
}

OPENGL_API void WINAPI glPushDebugGroup( GLenum source, GLuint id, GLsizei length, const char* message );
OPENGL_API void WINAPI glPopDebugGroup( void );

static void h2_intercept_RecursiveWorldNode( mnode_t* node )
{
	HOOK_ONLINE_NOTICE();

	g_surfList.numSurfs = 0;

	glPushDebugGroup(0, 0, -1, "R_RecursiveWorldNodeEx");

	R_RecursiveWorldNodeEx( node );

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

static void R_MeshBuildVertexBufferAndDraw( int* order, float *normals_array );
//not sure if we need a ptr to function to make sure the call is absolute, not relative
//relative call would mean we have to somehow adjust that, how? search for e8?
static void (*fp_MeshBuildVertexBuffer)(int* order, float *normals_array) = R_MeshBuildVertexBufferAndDraw;

static __declspec(naked) void h2_bridge_to_MeshBuildVertexBuffer()
{
	__asm {
		lea edx,[ESP + 0x30] //normals_array
		push edx
		push edi //order

		call fp_MeshBuildVertexBuffer
		add esp,8

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

static void R_RecursiveWorldNodeEx(mnode_t* node)
{
	msurface_t* surf;
	int c;
	int side;
	int sidebit;

	if (node->contents == CONTENTS_SOLID)
		return;
	
	if(!rmx_novis->value && node->visframe != r_visframecount)
		return;

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
				return;		// not visible
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
	R_RecursiveWorldNodeEx(node->children[side]);

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
			surf->texturechain = r_alpha_surfaces;
			r_alpha_surfaces = surf;
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
			image_t* image = R_TextureAnimation(surf->texinfo);
			surf->texturechain = image->texturechain;
			image->texturechain = surf;
		}
	}

	// Recurse down the back side
	R_RecursiveWorldNodeEx( node->children[!side] );

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
				//surf->texturechain = r_alpha_surfaces;
				//r_alpha_surfaces = surf;
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
				image_t* image = R_TextureAnimation(surf->texinfo);
				surf->texturechain = image->texturechain;
				image->texturechain = surf;
			}
		}
	}
}

#define SKIP_LIGHTMAP 0x7fffu

#define RENDER_TWOTEXTURES 1
#define RENDER_NORMALS     2

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
static void R_PopulateDrawBuffer( msurface_t* surf, int is_dynamic, int is_flowing, int flags );
static int qsort_compare( const void* arg1, const void* arg2 );

OPENGL_API void WINAPI glTexSubImage2D( GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* pixels );

static void R_SortAndDrawSurfaces( drawSurf_t* surfs, int numSurfs )
{
	qsort( surfs, numSurfs, sizeof( drawSurf_t ), qsort_compare );

	unsigned oldSort = ~0u;
	//int oldDynamic = -1;
	int oldTexnum = -1;
	//int oldFlowing = -1;
	int oldLightmap = -1;
	int flags = 0;

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

static void R_CheckDrawBufferSpace( int vertexes, int indexes, int flags )
{
	if ( g_drawBuff.numVertexes + vertexes > MAX_VERTEXES ||
		g_drawBuff.numIndexes + indexes > MAX_INDEXES )
	{
		R_RenderSurfs(flags);
	}
}

static void R_PopulateDrawBuffer( msurface_t* surf, int is_dynamic, int is_flowing, int flags )
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
OPENGL_API void WINAPI glBegin( GLenum mode );
OPENGL_API void WINAPI glEnd();
OPENGL_API void WINAPI glEnable( GLenum cap );
OPENGL_API void WINAPI glDisable( GLenum cap );

static void R_MeshBuildVertexBufferAndDraw(int *order, float *normals_array)
{
	HOOK_ONLINE_NOTICE();

	int oldtexture;
	int render_flags = rmx_normals->value ? RENDER_NORMALS : 0;
	
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
		unsigned alpha = 255;

		int totalindexes = (3 * count) - 6;
		R_CheckDrawBufferSpace( count, totalindexes, render_flags );

		int i;
		int index = g_drawBuff.numVertexes;
		ib = &g_drawBuff.indexes[g_drawBuff.numIndexes];
		vb = &g_drawBuff.vertexes[g_drawBuff.numVertexes];
		int start = index;

		if ( rmx_normals->value > 1 )
		{
			oldtexture = currenttexture[currenttmu];
			if ( r_whiteimage == NULL )
				r_whiteimage = GL_FindImage( "textures/general/white.m8", it_wall );
			GL_MBind(GL_TEXTURE0, r_whiteimage->texnum);
			//glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
			glColor3f( 1, 1, 1 );

			glBegin( GL_LINES );
		}

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

				if ( (currententity[0xc] & 8) != 0 ) //check if FULLBRIGHT
				{
					vb->clr.all = 0xffffffff;
					render_flags = 0;
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

				if ( rmx_normals->value > 1 )
				{
					vec3_t tmp;
					glVertex3fv( vsrc );
					VectorMA( vsrc, 2, normals, tmp);
					glVertex3fv( tmp );
				}
			} //while (--count);
		}

		g_drawBuff.numVertexes += count;
		g_drawBuff.numIndexes += totalindexes;
		//qglEnd ();

		if ( rmx_normals->value > 1 )
		{
			glEnd();
			GL_MBind(GL_TEXTURE0, oldtexture);

			//glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
		}
	}

	R_RenderSurfs( render_flags );
}

static int qsort_compare( const void *arg1, const void *arg2 )
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

static void R_RenderSurfs( int flags )
{
	if ( g_drawBuff.numIndexes )
	{
		c_brush_polys++;

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

		g_drawBuff.numIndexes = 0;
		g_drawBuff.numVertexes = 0;
	}
}

void h2_refgl_frame_ended()
{
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

static void hk_ResMngr_DeallocateResource( ResourceManager_t *resource, void *toDeallocate, size_t size );// = 0x2650
static void *fp_ResMngr_DeallocateResource = 0;
static void* __cdecl hk_malloc( size_t size );
static void* (__cdecl *fp_malloc)( size_t ) = 0;
static void __cdecl hk_free(void* block);
static void (__cdecl *fp_free)(void*) = 0;

void h2_generic_fixes()
{
	const char* modulename = "H2Common.dll";
	MODULEINFO h2common_data, quake2_data;

	modulename = "H2Common.dll";
	ZeroMemory(&h2common_data, sizeof(h2common_data));
	HMODULE h2comndll = GetModuleHandle( modulename );
	if ( !h2comndll || !GetModuleInformation( GetCurrentProcess(), h2comndll, &h2common_data, sizeof( h2common_data ) ) )
	{
		logPrintf( "h2_generic_fixes: failed to get %s moduleinfo\n", modulename );
		return;
	}
	//fp_ResMngr_DeallocateResource = (byte*)h2common_data.lpBaseOfDll + 0x2650;

	modulename = "quake2.dll";
	ZeroMemory(&quake2_data, sizeof(quake2_data));
	HMODULE quake2dll = GetModuleHandle( modulename );
	if ( !quake2dll || !GetModuleInformation( GetCurrentProcess(), quake2dll, &quake2_data, sizeof( quake2_data ) ) )
	{
		logPrintf( "h2_generic_fixes: failed to get %s moduleinfo\n", modulename );
		return;
	}

	//fp_malloc = (void*(*)(size_t))DetourFindFunction( modulename, "malloc" );
	//fp_free = (void(*)(void*))DetourFindFunction( modulename, "free" );
	//if ( !fp_malloc || !fp_free )
	//{
	//	logPrintf( "h2_generic_fixes: failed to find ptr malloc/free\n" );
	//}

#define CHECK_ABORT(FN) if ( error != NO_ERROR ) \
	{ abort=true; logPrintf( "h2_generic_fixes: failed to intercept %s: %d \n", (FN), error ); break; }

	LONG error = NO_ERROR;
	bool abort = false;
	do {
		DetourTransactionBegin();
		DetourUpdateThread( GetCurrentThread() );

		if ( fp_ResMngr_DeallocateResource )
		{
			error = DetourAttach( &(PVOID&)fp_ResMngr_DeallocateResource, hk_ResMngr_DeallocateResource );
			CHECK_ABORT( "ResMngr_DeallocateResource" );
		}
		if ( fp_malloc )
		{
			error = DetourAttach( &(PVOID&)fp_malloc, hk_malloc );
			CHECK_ABORT( "malloc" );
		}
		else
		{
			fp_malloc = malloc;
		}
		if ( fp_free )
		{
			error = DetourAttach( &(PVOID&)fp_free, hk_free );
			CHECK_ABORT( "free" );
		}
		else
		{
			fp_free = free;
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


static void hk_ResMngr_DeallocateResource( ResourceManager_t *resource, void *toDeallocate, size_t size )
{
	HOOK_ONLINE_NOTICE();

	char **toPush = NULL;

	//assert(size == resource->resSize);

	//assert(resource->free);	// see same assert at top of AllocateResource

	if ( toDeallocate )
	{
		toPush = (char **)(toDeallocate)-1;

		// set toPop->next to current unallocated front
		*toPush = (char *)(resource->free);

		// set unallocated to the node removed from allocated
		resource->free = toPush;
	}
	else
	{
		__debugbreak();
		riPRINTF( PRINT_ALERT, "DeallocateResource: null ptr\n" );
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
			__debugbreak();
		}
	}

	byte *post = (byte*)p + sizeof( *p ) + p->size;

	for ( int i = 0; i < ALLOC_EXTRA_BYTES; i++ )
	{
		if ( post[i] != 0x5a )
		{
			__debugbreak();
		}
	}

	fp_free( (byte*)p + sizeof( *p ) );
}