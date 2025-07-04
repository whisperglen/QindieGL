
#include "hooking.h"
#include <Windows.h>
#include <psapi.h>
#include <stdint.h>
#include "h2_rsurf.h"
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

typedef struct image_s //mxd. Changed in H2. Original size: 104 bytes
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

#if 1
typedef unsigned char   undefined;
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
#else
typedef struct model_s
{
	char		name[MAX_QPATH];

	int			registration_sequence;

	modtype_t	type;
	int			numframes;

	int			flags;

	//
	// volume occupied by the model graphics
	//		
	vec3_t		mins, maxs;
	float		radius;

	//
	// solid volume for clipping 
	//
	qboolean	clipbox;
	vec3_t		clipmins, clipmaxs;

	//
	// brush model
	//
	int			firstmodelsurface, nummodelsurfaces;
	int			lightmap;		// only for submodels

	int			numsubmodels;
	mmodel_t	*submodels;

	int			numplanes;
	cplane_t	*planes;

	int			numleafs;		// number of visible leafs, not counting 0
	mleaf_t		*leafs;

	int			numvertexes;
	mvertex_t	*vertexes;

	int			numedges;
	medge_t		*edges;

	int			numnodes;
	int			firstnode;
	mnode_t		*nodes;

	int			numtexinfo;
	mtexinfo_t	*texinfo;

	int			numsurfaces;
	msurface_t	*surfaces;

	//int			numsurfedges;
	//int			*surfedges;

	//int			nummarksurfaces;
	//msurface_t	**marksurfaces;

	//dvis_t		*vis;

	//byte		*lightdata;

	//// for alias models and skins
	//image_t		*skins[MAX_MD2SKINS];

	//fmdl_t		*fmodel;

	//int			extradatasize;
	//void		*extradata;
} model_t;
#endif

typedef struct cvar_s
{
	char* name;
	char* string;
	char* latched_string; // For CVAR_LATCH vars.
	int flags;
	qboolean modified; // Set each time the cvar is changed.
	float value;
	struct cvar_s* next;
} cvar2_t;

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

#define vecmax(a,m)             ((a) > m ? m : (a))

#define DotProduct(x,y)			(x[0]*y[0]+x[1]*y[1]+x[2]*y[2])
#define VectorSubtract(a,b,c)	(c[0]=a[0]-b[0],c[1]=a[1]-b[1],c[2]=a[2]-b[2])
#define VectorAdd(a,b,c)		(c[0]=a[0]+b[0],c[1]=a[1]+b[1],c[2]=a[2]+b[2])
#define VectorCopy(a,b)			(b[0]=a[0],b[1]=a[1],b[2]=a[2])
#define VectorCopyMax(a,b,m)	(b[0]=vecmax(a[0],m),b[1]=vecmax(a[1],m),b[2]=vecmax(a[2],m))
#define VectorScale(v,s,o)      ((o)[0]=(v)[0]*(s),(o)[1]=(v)[1]*(s),(o)[2]=(v)[2]*(s))
#define VectorClear(a)			(a[0]=a[1]=a[2]=0)
#define VectorNegate(a,b)		(b[0]=-a[0],b[1]=-a[1],b[2]=-a[2])
#define VectorSet(v, x, y, z)	(v[0]=(x), v[1]=(y), v[2]=(z))

extern "C" {
typedef void	(*ri_Printf) (int print_level, const char *str, ...);
typedef void	(*ri_Error) (int code, char *fmt, ...);
typedef cvar2_t* (*ri_Cvar_Get) (const char *name, const char *value, int flags);
typedef cvar2_t* (*ri_Cvar_Set)( const char *name, const char *value );

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
static cvar2_t* r_fullbright;
static cvar2_t* gl_drawflat;
static cvar2_t* gl_sortmulti;
static cvar2_t* r_nocull;
static cvar2_t* gl_dynamic;
static cvar2_t* rmx_skiplightmaps;
static cvar2_t* rmx_nocull;
static cvar2_t* rmx_novis;
static cvar2_t* rmx_generic;

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
}

#define PTR_FROM_OFFSET(typecast, offset) (typecast)((intptr_t)(offset) + (intptr_t)ref_gl_data.lpBaseOfDll)

const char* modulename = "ref_gl.dll";
static DWORD modulesize = 253952;
static MODULEINFO ref_gl_data;

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
static void h2_bridge_to_MeshFillBuffer();
static void R_RecursiveWorldNodeEx( mnode_t* node );
static void R_SortAndDrawSurfaces( drawSurf_t* surfs, int numSurfs );
static void R_AddDrawSurf( msurface_t* surf );

void h2_rsurf_init()
{
	ZeroMemory(&ref_gl_data, sizeof(ref_gl_data));
	HMODULE refdll = GetModuleHandle( modulename );
	if (refdll && GetModuleInformation(GetCurrentProcess(), refdll, &ref_gl_data, sizeof(ref_gl_data)))
	{
		logPrintf("h2_rsurf_init: ref_gl base:%p size:%d ep:%p \n", ref_gl_data.lpBaseOfDll, ref_gl_data.SizeOfImage, ref_gl_data.EntryPoint);

		//if ( modulesize == ref_gl_data.SizeOfImage ) //doesn't match, scrap it
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

			r_fullbright = ((ri_Cvar_Get)dp_ri[RI_CVAR_GET] )("r_fullbright", "0", 0);
			gl_drawflat = ((ri_Cvar_Get)dp_ri[RI_CVAR_GET] )("gl_drawflat", "0", 0);
			gl_sortmulti = ((ri_Cvar_Get)dp_ri[RI_CVAR_GET] )("gl_sortmulti", "0", 1);
			r_nocull = ((ri_Cvar_Get)dp_ri[RI_CVAR_GET] )("r_nocull", "0", 0);
			gl_dynamic = ((ri_Cvar_Get)dp_ri[RI_CVAR_GET] )("gl_dynamic", "1", 0);
			rmx_skiplightmaps = ((ri_Cvar_Get)dp_ri[RI_CVAR_GET] )("rmx_skiplightmaps", "1", 0);
			rmx_nocull = ((ri_Cvar_Get)dp_ri[RI_CVAR_GET] )("rmx_nocull", "2", 0);
			rmx_novis = ((ri_Cvar_Get)dp_ri[RI_CVAR_GET] )("rmx_novis", "1", 0);
			rmx_generic = ((ri_Cvar_Get)dp_ri[RI_CVAR_GET] )("rmx_generic", "-1", 0);

			//R_DrawWorld calls R_RecursiveWorldNode
			//e8 7e fb ff ff
			byte *code = (byte*)((intptr_t)0xcbdd + (intptr_t)ref_gl_data.lpBaseOfDll);

			intptr_t val = 0;
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
				}
			}
			else
				logPrintf("h2_rsurf_init:R_DrawWorld: the CALL instr does not match %x %x\n", code[0], val);

			//R_DrawAliasModel calls R_CullAliasModel
			//e8 d0 09 00 00
			code = PTR_FROM_OFFSET(byte*, 0x214b);
			val = 0;
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
				logPrintf("h2_rsurf_init:R_DrawAliasModel: the CALL instr does not match %x %x\n", code[0], val);

			//GL_DrawFlexFrameLerp draws the vertices
			//8b 2f 83 c7 this is where the draw loop starts
			code = PTR_FROM_OFFSET(byte*, 0x28ab);
			val = 0;
			memcpy( &val, &code[0], 4 );
			if ( val == 0xc7832f8b )
			{
				unsigned long restore;
				if ( hook_unprotect( code, 471, &restore ) )
				{
					//copy our asm bridge code over
					byte* src = (byte*)h2_bridge_to_MeshFillBuffer;
					int nopcnt = 0;
					int i = 0;
					for ( ; i < 471 && nopcnt < 5; i++ )
					{
						code[i] = src[i];
						if ( code[i] == 0x90 )
							nopcnt++;
					}

					if ( nopcnt == 5 )
					{
						//now that we reached the nop instructions, make a jmp to end of draw loop
						byte* endcall = PTR_FROM_OFFSET( byte*, 0x29d6 ); //this is where the loop ends
						val = endcall - &code[i];
						code[i-5] = 0xe9;//jmp relative
						memcpy( &code[i-4], &val, 4 );
					}

					hook_protect( code, 471, restore );
				}
			}
			else
				logPrintf("h2_rsurf_init:GL_DrawFlexFrameLerp: the instr does not match %x\n", val);
		}
		//else
		//	logPrintf("h2_rsurf_init: size of ref_gl does not match %d vs %d, patching will most likely result in crash. Aborted.\n", modulesize, ref_gl_data.SizeOfImage);
	}
	else
	{
		logPrintf("h2_rsurf_init: cannot get ref_gl info %s\n", DXGetErrorString(GetLastError()));
	}
}

void h2_rsurf_deinit()
{
	if ( modulesize == ref_gl_data.SizeOfImage )
	{
		//R_DrawWorld calls R_RecursiveWorldNode
		//e8 7e fb ff ff
		byte *code = (byte*)((intptr_t)0xcbdd + (intptr_t)ref_gl_data.lpBaseOfDll);

		intptr_t val = 0xfffffb7e;
		unsigned long restore;
		if ( hook_unprotect( code, 5, &restore ) )
		{
			code[0] = 0xe8;
			memcpy( &code[1], &val, sizeof( val ) );
			hook_protect( code, 5, restore );
		}
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

	if ( rmx_nocull->value > 1 || r_nocull->value > 1 )
	{
		return qfalse;
	}
	else
	{
		return R_CullAliasModel( bbox, e );
	}
}

static void R_MeshFillBuffer( int* order, float alpha );
//not sure if we need a ptr to function to make sure the call is absolute, not relative
//relative call would mean we have to somehow adjust that, how? search for e8?
static void (*fp_MeshFillBuffer)(int* order, float alpha) = R_MeshFillBuffer;

static __declspec(naked) void h2_bridge_to_MeshFillBuffer()
{
	__asm {
		MOV EDX,dword ptr [ESP + 0x24] //alpha
		push edx
		push edi //order
		
		call fp_MeshFillBuffer
		add esp,8

		//placeholder for our jmp relative to end of loop
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
	if (!rmx_nocull->value && R_CullBox(node->minmaxs, node->minmaxs + 3))
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

	if ( rmx_nocull->value || r_nocull->value )
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
		((ri_Printf)dp_ri[RI_PRINTF_OFF])( PRINT_ALL, "Too many surfs\n" );
		//ri.Con_Printf( PRINT_ALL, "Too many surfs\n" );
}

static void R_RenderSurfs( int two_textures );
static void R_PopulateDrawBuffer( msurface_t* surf, int is_dynamic, int is_flowing, int two_textures );
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
	qboolean two_textures = qfalse;

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
			R_RenderSurfs(two_textures);
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
			two_textures = qtrue;
		}
		else if( oldTexnum != sort.bits.texnum || oldLightmap != sort.bits.lightmap )
		{
			two_textures = qfalse;
			GL_MBind( /*GL_TEXTURE0_SGIS*/GL_TEXTURE0, sort.bits.texnum/*image->texnum*/ );
			if ( sort.bits.lightmap != SKIP_LIGHTMAP )
			{
				GL_MBind( /*GL_TEXTURE1_SGIS*/GL_TEXTURE1, /*gl_state.lightmap_textures*/gl_state_lightmap_textures + sort.bits.lightmap/*lmtex*/ );
				two_textures = qtrue;
			}
		}
		oldSort = sort.all;
		oldTexnum = sort.bits.texnum;
		oldLightmap = sort.bits.lightmap;

		R_PopulateDrawBuffer( s->surface, sort.bits.dynamic, sort.bits.flowing, two_textures );
#endif
	}

	//one more call for the remaining vertices
	R_RenderSurfs(two_textures);
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

static void R_CheckDrawBufferSpace( int vertexes, int indexes, int two_textures )
{
	if ( g_drawBuff.numVertexes + vertexes > MAX_VERTEXES ||
		g_drawBuff.numIndexes + indexes > MAX_INDEXES )
	{
		R_RenderSurfs(two_textures);
	}
}

static void R_PopulateDrawBuffer( msurface_t* surf, int is_dynamic, int is_flowing, int two_textures )
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
		R_CheckDrawBufferSpace( p->numverts, totalindexes, two_textures );

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

static void R_MeshFillBuffer(int *order, float alpha)
{
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
		//unsigned clralpha = vecmax( alpha * 255, 255 );

		int totalindexes = (3 * count) - 6;
		R_CheckDrawBufferSpace( count, totalindexes, false );

		int i;
		int index = g_drawBuff.numVertexes;
		ib = &g_drawBuff.indexes[g_drawBuff.numIndexes];
		vb = &g_drawBuff.vertexes[g_drawBuff.numVertexes];
		int start = index;

		//if ( currententity->flags & ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE ) )
		//{
		//	for ( i = 0; i < count; i++ )
		//	{
		//		if ( i > 2 )
		//		{
		//			switch ( strategy )
		//			{
		//			case 0:
		//				ib[0] = start;
		//				ib[1] = index - 1;
		//				ib += 2;
		//				break;
		//			case 1: { //triangle strip: switch winding strategy depending on even/uneven index
		//				int uneven = i & 1;
		//				ib[!uneven] = index -1;
		//				ib[uneven] = index -2;
		//				ib += 2;
		//				break; }
		//			}
		//		}

		//		index_xyz = order[2];
		//		order += 3;

		//		//qglColor4f( shadelight[0], shadelight[1], shadelight[2], alpha);
		//		//qglVertex3fv (s_lerped[index_xyz]);
		//		float* vsrc = s_lerped + index_xyz * 3;
		//		VectorCopy( vsrc, vb->xyz );
		//		vb->clr.all = 0xffffffff;
		//		//vec3_t clrval;
		//		//VectorScale( shadelight, 255, clrval );
		//		//VectorCopyMax( clrval, vb->clr.b, 255 );
		//		//vb->clr.b[3] = clralpha;

		//	} //while (--count);
		//}
		//else
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

				// normals and vertexes come from the frame list
				//float l = shadedots[verts[index_xyz].lightnormalindex];

				//qglColor4f (l* shadelight[0], l*shadelight[1], l*shadelight[2], alpha);
				//qglVertex3fv (s_lerped[index_xyz]);
				float* vsrc = s_lerped + index_xyz * 3;
				VectorCopy( vsrc, vb->xyz );
				vb->clr.all = 0xffffffff;
				//vec3_t clrval;
				//float clrscale = l * 255;
				//VectorScaleP( shadelight, clrscale, clrval );
				//VectorCopyMax( clrval, vb->clr.b, 255 );
				//vb->clr.b[3] = clralpha;
				vb++;
				ib++;
			} //while (--count);
		}

		g_drawBuff.numVertexes += count;
		g_drawBuff.numIndexes += totalindexes;
		//qglEnd ();

	}

	R_RenderSurfs( false );
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
OPENGL_API void WINAPI glClientActiveTexture( GLenum texture );
OPENGL_API void WINAPI glColorPointer( GLint size, GLenum type, GLsizei stride, const GLvoid* pointer );
OPENGL_API void WINAPI glTexCoordPointer( GLint size, GLenum type, GLsizei stride, const GLvoid* pointer );
OPENGL_API void WINAPI glDrawArrays( GLenum mode, GLint first, GLsizei count );
OPENGL_API void WINAPI glDrawElements( GLenum mode, GLsizei count, GLenum type, const GLvoid* indices );
OPENGL_API void WINAPI glDisableClientState( GLenum cap );

static void R_RenderSurfs( int two_textures )
{
	if ( g_drawBuff.numIndexes )
	{
		c_brush_polys++;

		glEnableClientState( GL_VERTEX_ARRAY );
		glVertexPointer( 3, GL_FLOAT, sizeof( struct vertexData_s ), g_drawBuff.vertexes[0].xyz );
		glEnableClientState( GL_COLOR_ARRAY );
		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( struct vertexData_s ), g_drawBuff.vertexes[0].clr.b );
		glClientActiveTexture( GL_TEXTURE0_ARB );
		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		glTexCoordPointer( 2, GL_FLOAT, sizeof( struct vertexData_s ), g_drawBuff.vertexes[0].tex0 );
		if ( two_textures )
		{
			glClientActiveTexture( GL_TEXTURE1_ARB );
			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glTexCoordPointer( 2, GL_FLOAT, sizeof( struct vertexData_s ), g_drawBuff.vertexes[0].tex1 );
		}
		//glDrawArrays( GL_POLYGON, 0, numvert );
		glDrawElements( GL_TRIANGLES, g_drawBuff.numIndexes, GL_UNSIGNED_SHORT, g_drawBuff.indexes );
		if ( two_textures )
		{
			glDisableClientState( GL_TEXTURE_COORD_ARRAY );
			glClientActiveTexture( GL_TEXTURE0_ARB );
		}
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		glDisableClientState( GL_COLOR_ARRAY );
		glDisableClientState( GL_VERTEX_ARRAY );

		g_drawBuff.numIndexes = 0;
		g_drawBuff.numVertexes = 0;
	}
}

void h2_rsurf_frame_ended()
{
	glClearColor( 0, 0, 0, 0 );
	//glClear( GL_COLOR_BUFFER_BIT );
}