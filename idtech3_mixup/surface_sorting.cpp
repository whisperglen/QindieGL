
#include "surface_sorting.h"

#include <Windows.h>
#include <psapi.h>
#include "detours.h"
#include <search.h>
#include <string>

#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_helpers.hpp"
#include "d3d_utils.hpp"

static int g_dump_data = false;

typedef enum {
	SF_BAD,
	SF_SKIP,                // ignore
	SF_FACE,
	SF_GRID,
	SF_TRIANGLES,
	SF_POLY,
	SF_MD3,
	SF_MDC,
	SF_MDS,
	SF_FLARE,
	SF_ENTITY,
	SF_DISPLAY_LIST,

	SF_NUM_SURFACE_TYPES,
	SF_MAX = 0xffffffff         // ensures that sizeof( surfaceType_t ) == sizeof( int )
} surfaceType_t;

typedef struct drawSurf_s {
	unsigned sort;                      // bit combination for fast compares
	surfaceType_t       *surface;       // any of surface*_t
} drawSurf_t;

uint32_t qsort_extract(uint32_t val, uint32_t shift, uint32_t mask)
{
	return ((val >> shift) & mask);
}

static int qsort_compare( const void *arg1, const void *arg2 )
{
	int ret = 0;

	drawSurf_t* s1 = (drawSurf_t*)arg1;
	drawSurf_t* s2 = (drawSurf_t*)arg2;
	uint32_t *e1 = (uint32_t *)(s1->surface);
	uint32_t *e2 = (uint32_t *)(s2->surface);
	//if ( e1[0] > e2[0] )
	//{
	//	ret = 1;
	//}
	//else if ( e1[0] < e2[0] )
	//{
	//	ret = -1;
	//}
	//else

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
	}

	return ret;
}

typedef float vec_t;
typedef vec_t vec3_t[3];
typedef enum {qfalse, qtrue}    qboolean;

typedef struct cvar_s {
	char        *name;
	char        *string;
	char        *resetString;       // cvar_restart will reset to this value
	char        *latchedString;     // for CVAR_LATCH vars
	int flags;
	qboolean modified;              // set each time the cvar is changed
	int modificationCount;          // incremented each time the cvar is changed
	float value;                    // atof( string )
	int integer;                    // atoi( string )
	struct cvar_s *next;
	struct cvar_s *hashNext;
} cvar_t;

typedef struct jmp_helper_s
{
	byte* addr;
	byte* data;
	int datasz;
	intptr_t verify;
} jmp_helper_t;

static const void* fp_qsortFast = 0;
static const void* fp_qsortFast_uc0 = 0;
static const void* fp_markLeaves = 0;
static const void* fp_cvarGet = 0;
static const void* fp_cvarSet = 0;
static const cvar_t* cvar_novis = 0;
static const int* tr_dp_skyportal = 0;
static byte* jmp_skyOnscreen = 0;
static const void* fp_addworldsurf1 = 0;
static const void* fp_addworldsurf2 = 0;
static const void* fp_retvoid = 0;
static const void* fp_retzero = 0;
static const void* fp_retone = 0;
static const void* fp_retnegone = 0;
static const int* tr_dp_viewcount = 0;
static const void* fp_setupfrustum = 0;
static float* tr_dp_viewfov = 0;
static const void* fp_addworldsurfaces = 0;
static const void* fp_dumpdxf = 0;
static jmp_helper_t jmp_helpers[20];
static byte jmp_helper_eb = 0xeb;

static void hook_qsortFast(void* base, unsigned int num, unsigned width)
{
	HOOK_ONLINE_NOTICE();

	if ( width != 8 )
	{
		PRINT_ONCE("ERROR: qsortFast assert width: %d\n", width);
		return;
	}
	if ( num > 100000 )
	{
		PRINT_ONCE( "ERROR: qsortFast assert numSurfs: %d\n", num );
		return;
	}

	if ( g_dump_data )
	{
		//drawSurf_t* surf = (drawSurf_t*)base;
		//logPrintf("Dumping presorted data %p %d %d\n", base, num, width);
		//for ( int i = 0; i < num; i++, surf++ )
		//{
		//	logPrintf("%d. %x %p %d\n", i, surf->sort, surf->surface, *surf->surface);
		//}
	}
	//{
	//	//unsigned int mask = ~0x3FF80;
	//	unsigned int mask = 3;
	//	drawSurf_t* surf = (drawSurf_t*)base;
	//	for ( int i = 0; i < num; i++, surf++ )
	//	{
	//		surf->sort = surf->sort | mask;
	//	}
	//}
	qsort(base, num, width, qsort_compare);
	if ( g_dump_data )
	{
		//drawSurf_t* surf = (drawSurf_t*)base;
		//logPrintf("Dumping sorted data %p %d %d\n", base, num, width);
		//for ( int i = 0; i < num; i++, surf++ )
		//{
		//	logPrintf("%d. %x %p %d\n", i, surf->sort, surf->surface, *surf->surface);
		//}
	}
}

static __declspec(naked) void hook_qsortFast_uc0()
{
	//EAX = 00000456 EBX = 00000008 ECX = 1F23C900
	__asm {
		/* prolog */
		push ebp
		//mov  ebp, esp

		/* call cdecl function */
		push ebx
		push eax
		push ecx
		//call fp_qsortFast
		call hook_qsortFast
		add esp,12

		/* epilog */
		//mov esp, ebp
		pop ebp
		ret
	}
}

static void hook_markLeaves( void )
{
	HOOK_ONLINE_NOTICE();

	if ( !cvar_novis )
	{
		cvar_novis = ((cvar_t*(*)( const char *name, const char *value, int flags ))fp_cvarGet)("r_novis", "0", 0x200);
	}

	int novis = cvar_novis->integer;
	if ( novis == 1 && *tr_dp_skyportal == 1 )
	{
		((void(*)( const char *, const char * ))fp_cvarSet)( "r_novis", "0" );
	}
	else
	{
		//no need to re-set it after the real call
		novis = 0;
	}

	((void(*)(void))fp_markLeaves)(); //call original

	if ( novis )
	{
		((void(*)( const char *, const char * ))fp_cvarSet)( "r_novis", "1" );
	}
}

static void hook_addworldsurf( void* pdata, int val )
{
	HOOK_ONLINE_NOTICE();

	if ( *(int*)pdata != *tr_dp_viewcount )
	{
		__asm {
			mov eax, pdata
			push eax
			mov eax, val
			call fp_addworldsurf2
			add esp, 4
		}
		//((void (*)(void*))fp_addworldsurf2)(ptr);
	}
}

static void hook_addworldsurfaces()
{
	HOOK_ONLINE_NOTICE();

	((void(*)())fp_dumpdxf)();
	((void(*)())fp_addworldsurfaces)();
}

typedef float vec3_t[3];

#define DotProduct( x,y )         ( ( x )[0] * ( y )[0] + ( x )[1] * ( y )[1] + ( x )[2] * ( y )[2] )
#define PLANE_NON_AXIAL 3

typedef struct cplane_s {
	vec3_t normal;
	float dist;
	byte type;              // for fast side tests: 0,1,2 = axial, 3 = nonaxial
	byte signbits;          // signx + (signy<<1) + (signz<<2), used as lookup during collision
	byte pad[2];
} cplane_t;

static void SetPlaneSignbits( cplane_t *out ) {
	int bits, j;

	// for fast box on planeside test
	bits = 0;
	for ( j = 0 ; j < 3 ; j++ ) {
		if ( out->normal[j] < 0 ) {
			bits |= 1 << j;
		}
	}
	out->signbits = bits;
}

static void hook_setupfrustum()
{
	HOOK_ONLINE_NOTICE();

	//funcptr (void*)0x004c95f0;
	//float* fovX = (float*)0x01535108;
	//float* fovY = (float*)0x0153510c;
	//cplane_t* frustum = (cplane_t*)0x01535198;
	float* axis0 = (float*)0x01534fdc;
	//float *origin = (float*)0x01534FD0;

	if ( g_dump_data )
	{
		logPrintf("Axis dump:\n");
		float* axis = axis0;
		for ( int i = 0; i < 3; i++, axis += 3 )
		{
			logPrintf( "%g %g %g\n", axis[0], axis[1], axis[2] );
		}
	}

	float orgfovx = tr_dp_viewfov[0];
	float orgfovy = tr_dp_viewfov[1];

	tr_dp_viewfov[0] = 179.0f;
	tr_dp_viewfov[1] = 179.0f;

	if ( 0 )
	{
		//inverse axis and render backside
		static vec3_t backup_axis[3];
		{
			memcpy( backup_axis, axis0, sizeof( backup_axis ) );
			float* axis = axis0;
			for ( int i = 0; i < 2; i += 1 )
			{
				axis[i*3 + 0] = -axis[i*3 + 0];
				axis[i*3 + 1] = -axis[i*3 + 1];
				axis[i*3 + 2] = -axis[i*3 + 2];
			}
			key_inputs_t keys = keypress_get( true );
			if ( keys.y )
			{
				((void(*)())0x4c9090)();
			}
		}

		((void(*)())fp_setupfrustum)();
		((void(*)())0x4caad0)();

		//restore axis and render forward looking
		memcpy( axis0, backup_axis, sizeof( backup_axis ) );
	}

	((void(*)())fp_setupfrustum)();
	//now let the normal rendering flow execute

	tr_dp_viewfov[0] = orgfovx;
	tr_dp_viewfov[1] = orgfovy;
	
	//for ( int i = 0; i < 4; i++ )
	//{
	//	frustum[i].normal[0] = axis0[0];
	//	frustum[i].normal[1] = axis0[1];
	//	frustum[i].normal[2] = axis0[2];
	//	frustum[i].type = PLANE_NON_AXIAL;
	//	frustum[i].dist = DotProduct(origin, frustum[i].normal);
	//	SetPlaneSignbits(&frustum[i]);
	//}
}

static void hook_retvoid()
{
	HOOK_ONLINE_NOTICE();

	return;
}

static int hook_retzero()
{
	HOOK_ONLINE_NOTICE();

	return 0;
}

static int hook_retone()
{
	HOOK_ONLINE_NOTICE();

	return 1;
}

static int hook_retnegone()
{
	HOOK_ONLINE_NOTICE();

	return -1;
}

static int config_int( const char* name, bool required, bool* not_found = 0 )
{
	int ret = 0;

	ret = D3DGlobal_ReadGameConf( name );
	if (ret)
	{
		logPrintf("SurfaceSort: config %s found %d\n", name, ret);
		return ret;
	}

	if (not_found)
	{
		*not_found = true;
	}
	if (required)
	{
		logPrintf("SurfaceSort: config %s not found\n", name);
	}
	return 0;
}

static void* config_hex(const char* name, bool required, bool *not_found = 0)
{
	void* ret = 0;

	ret = D3DGlobal_ReadGameConfPtr( name );
	if (ret)
	{
		ret = hook_offset_to_addr( ret );
		logPrintf("SurfaceSort: config %s found %p\n", name, ret);
		return ret;
	}

	if (not_found)
	{
		*not_found = true;
	}
	if (required)
	{
		logPrintf("SurfaceSort: config %s not found\n", name);
	}
	return 0;
}

static int config_bytes(const char* name, byte* dest, int destsz, bool required, bool *not_found = 0)
{
	int ret = 0;
	char local[64];

	int found = D3DGlobal_ReadGameConfStr( name, local, sizeof(local) );
	if (found)
	{
		memset( dest, 0, destsz );
		char* ctx = 0;
		char* tkn = strtok_s( local, " ", &ctx );
		for ( int i = 0; tkn != 0 && i < destsz; i++ )
		{
			ret++;
			dest[i] = (byte)strtoul( tkn, NULL, 16 );
			tkn = strtok_s( NULL, " ", &ctx );
		}
		
		logPrintf( "SurfaceSort: config %s read %d bytes %x %x %x %x ..\n", name, ret,
			destsz > 0 ? dest[0] : 0,
			destsz > 1 ? dest[1] : 0,
			destsz > 2 ? dest[2] : 0,
			destsz > 3 ? dest[3] : 0 );
		return ret;
	}

	if (not_found)
	{
		*not_found = true;
	}
	if (required)
	{
		logPrintf("SurfaceSort: config %s not found\n", name);
	}
	return 0;
}

static const void* config_pattern( const char* name, bool required, bool* not_found = 0 )
{
	const void* ret = 0;
	byte bufsrch[32];
	int srchsz = 0;
	std::string tname;
	tname.assign( name ); tname.append( "_pat" );
	srchsz = config_bytes( tname.c_str(), bufsrch, sizeof( bufsrch ), required, not_found );
	if ( srchsz )
	{
		ret = hook_find_pattern( bufsrch, srchsz );
		if ( ret )
		{
			tname.assign( name ); tname.append( "_off" );
			int offset = config_int( tname.c_str(), false );
			ret = (byte*)ret + offset;
			logPrintf( "SurfaceSort: pattern config %s addr %p\n", name, ret );
		}
		else
		{
			if (not_found)
			{
				*not_found = true;
			}
			if (required)
			{
				logPrintf("SurfaceSort: config %s not found\n", name);
			}
		}
	}

	return ret;
}

static bool read_conf()
{
	bool not_found = false;
	bool opt_not_found = false;
	byte bufsrch[32];  int srchsz = 0;
	char name[32];
	int i, j;

	const int numvarsqsf = 5;
	{ /** Search for __cdecl qsortFast addresses */
		const char* searchstr = "qsortFast";

		//see if there is a function hex address
		fp_qsortFast = config_hex( searchstr, false );

		//search for hex patterns
		strncpy_s( name, sizeof( name ), searchstr, _TRUNCATE );
		for ( int i = 0; fp_qsortFast == NULL && i < numvarsqsf; i++ )
		{
			fp_qsortFast = config_pattern( name, false );
			snprintf( name, sizeof( name ), "%s%d", searchstr, i );
		}
	}

	{ /** Search for __usercall veriant0 qsortFast addresses */
		const char* searchstr = "qsortFast_uc0";

		//see if there is a function hex address
		fp_qsortFast_uc0 = config_hex( searchstr, false );

		//search for hex patterns
		strncpy_s( name, sizeof( name ), searchstr, _TRUNCATE );
		for ( int i = 0; fp_qsortFast_uc0 == NULL && i < numvarsqsf; i++ )
		{
			fp_qsortFast_uc0 = config_pattern( name, false );
			snprintf( name, sizeof( name ), "%s%d", searchstr, i );
		}
	}

	fp_markLeaves = config_hex( "markLeaves", false );
	if ( fp_markLeaves )
	{
		fp_cvarSet = (const void**)config_hex( "fp_cvarSet", true, &not_found );
		fp_cvarGet = (const void**)config_hex( "fp_cvarGet", true, &not_found );
		tr_dp_skyportal = (const int*)config_hex( "tr_dp_skyportal", true, &not_found );
	}
	jmp_skyOnscreen = (byte*)config_hex( "jmp_skyOnscreen", false );

	//search for jmp
	memset( jmp_helpers, 0, sizeof( jmp_helpers ) );
	for ( i = 0, j = 0; i < ARRAYSIZE( jmp_helpers ); i++ )
	{
		snprintf( name, sizeof( name ), "jmp_target%d", i );
		byte* addr = (byte*)config_hex( name, false );
		if ( !addr ) addr = (byte*)config_pattern( name, false );
		if ( addr != 0 )
		{
			jmp_helpers[j].addr = addr;
			strncat_s( name, sizeof( name ), "_data", _TRUNCATE );
			srchsz = config_bytes( name, bufsrch, sizeof( bufsrch ), false );
			if ( srchsz )
			{
				jmp_helpers[j].data = (byte*)malloc( srchsz );
				if( !jmp_helpers[j].data ) { not_found = true; return false; }
				memcpy( jmp_helpers[j].data, bufsrch, srchsz );
				jmp_helpers[j].datasz = srchsz;
			}
			else
			{
				jmp_helpers[j].data = &jmp_helper_eb;
				jmp_helpers[j].datasz = 1;
			}
			snprintf( name, sizeof( name ), "jmp_target%d_vrf", i );
			jmp_helpers[j].verify = (intptr_t)config_hex( name, false );
			//logPrintf( "SurfaceSort: config %s addr %p custom %d\n", name, jmp_helpers[j].addr, jmp_helpers[j].data != &jmp_helper_eb );

			j++;
		}
	}
	
	fp_addworldsurf1 = config_pattern( "fp_addworldsurf1", false );
	if ( fp_addworldsurf1 )
	{
		fp_addworldsurf2 = config_pattern( "fp_addworldsurf2", true, &not_found );
		tr_dp_viewcount = (int*)hook_loadptr(config_pattern( "tr_dp_viewcount", true, &not_found ));
	}

	fp_addworldsurfaces = config_pattern( "fp_addworldsurfaces", false );
	if ( fp_addworldsurfaces )
	{
		fp_dumpdxf = config_pattern( "fp_dumpdxf", true, &not_found );
	}

	fp_setupfrustum = config_pattern( "fp_setupfrustum", false );
	if ( fp_setupfrustum )
	{
		tr_dp_viewfov = (float*)hook_loadptr(config_pattern( "tr_dp_viewfov", true, &not_found ));
	}

	fp_retvoid = config_hex( "fp_retvoid", false );
	fp_retzero = config_hex( "fp_retzero", false );
	fp_retone = config_hex( "fp_retone", false );
	fp_retnegone = config_hex( "fp_retnegone", false );

	return (false == not_found);
}

#define BREAKABLE_BLOCK_BEGIN do
#define BREAKABKE_BLOCK_END while(0)
#define BREAK_ON_DETOUR_ERROR(X, Y) if ((X) != NO_ERROR) { (Y) = __LINE__; break; }

typedef LONG (WINAPI *DetourAction_FP)(_Inout_ PVOID *ppPointer, _In_ PVOID pDetour);

static void do_detour_action( DetourAction_FP detourAction )
{
	LONG error = NO_ERROR;
	int errhint = 0;

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	BREAKABLE_BLOCK_BEGIN
	{
		if (fp_qsortFast)
		{
			error = detourAction(&(PVOID&)fp_qsortFast, hook_qsortFast);
			BREAK_ON_DETOUR_ERROR(error, errhint);
		}
		if (fp_qsortFast_uc0)
		{
			error = detourAction(&(PVOID&)fp_qsortFast_uc0, hook_qsortFast_uc0);
			BREAK_ON_DETOUR_ERROR(error, errhint);
		}
		if (fp_markLeaves)
		{
			error = detourAction(&(PVOID&)fp_markLeaves, hook_markLeaves);
			BREAK_ON_DETOUR_ERROR(error, errhint);
		}
		if ( fp_addworldsurfaces )
		{
			error = detourAction(&(PVOID&)fp_addworldsurfaces, hook_addworldsurfaces);
			BREAK_ON_DETOUR_ERROR(error, errhint);
		}
		if ( fp_addworldsurf1 )
		{
			error = detourAction(&(PVOID&)fp_addworldsurf1, hook_addworldsurf);
			BREAK_ON_DETOUR_ERROR(error, errhint);
		}
		if ( fp_setupfrustum )
		{
			error = detourAction(&(PVOID&)fp_setupfrustum, hook_setupfrustum);
			BREAK_ON_DETOUR_ERROR(error, errhint);
		}
		if ( fp_retvoid )
		{
			error = detourAction(&(PVOID&)fp_retvoid, hook_retvoid);
			BREAK_ON_DETOUR_ERROR(error, errhint);
		}
		if ( fp_retzero )
		{
			error = detourAction(&(PVOID&)fp_retzero, hook_retzero);
			BREAK_ON_DETOUR_ERROR(error, errhint);
		}
		if ( fp_retone )
		{
			error = detourAction(&(PVOID&)fp_retone, hook_retone);
			BREAK_ON_DETOUR_ERROR(error, errhint);
		}
		if ( fp_retnegone )
		{
			error = detourAction(&(PVOID&)fp_retnegone, hook_retnegone);
			BREAK_ON_DETOUR_ERROR(error, errhint);
		}
		error = DetourTransactionCommit();
	} BREAKABKE_BLOCK_END;

	if ( error != NO_ERROR )
	{
		DetourTransactionAbort();
	}

	logPrintf("SurfaceSort: detouring result: %d hint: %d\n", error, errhint);
}

void hook_surface_sorting_do_init()
{

	if (read_conf())
	{
		do_detour_action(DetourAttach);

		unsigned long restore;

		if ( jmp_skyOnscreen && hook_unprotect(jmp_skyOnscreen, 1, &restore) )
		{
			jmp_skyOnscreen[0] = 0xeb;
			hook_protect( jmp_skyOnscreen, 1, restore );
		}

		for ( int i = 0; i < ARRAYSIZE( jmp_helpers ); i++ )
		{
			if ( jmp_helpers[i].addr )
			{
				if ( jmp_helpers[i].verify )
				{
					if ( 0 != memcmp( jmp_helpers[i].addr, &jmp_helpers[i].verify, sizeof( intptr_t ) ) )
					{
						logPrintf( "JMP does not verify skipping:%p verf:%p\n", jmp_helpers[i].addr, jmp_helpers[i].verify );
						continue;
					}
				}
				if ( hook_unprotect( jmp_helpers[i].addr, jmp_helpers[i].datasz, &restore ) )
				{
					memcpy( jmp_helpers[i].addr, jmp_helpers[i].data, jmp_helpers[i].datasz );
					hook_protect( jmp_helpers[i].addr, jmp_helpers[i].datasz, restore );
				}
			}
		}
	}
	else
	{
		logPrintf("SurfaceSort: ERROR configuration incomplete\n");
	}
}

void hook_surface_sorting_do_deinit()
{
	do_detour_action( DetourDetach );
}

void hook_surface_sorting_frame_ended()
{
	if ( g_dump_data )
	{
		g_dump_data = false;
	}
	else
	{
		key_inputs_t keys = keypress_get();
		if ( keys.ctrl && keys.p )
		{
			g_dump_data = true;
		}
	}
}