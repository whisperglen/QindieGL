
#include "surface_sorting.h"

#include <Windows.h>
#include <psapi.h>
#include "detours.h"
#include <search.h>

#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_helpers.hpp"

static int g_dumping = false;

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
	if (s1->sort > s2->sort)
	{
		ret = 1;
	}
	else if (s1->sort < s2->sort)
	{
		ret = -1;
	}
	else //equals
	{
		if (s1->surface > s2->surface)
		{
			ret = 1;
		}
		else if (s1->surface < s2->surface)
		{
			ret = -1;
		}
		else //equals
		{
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

static const void* fp_qsortFast = 0;
static const void* fp_markLeaves = 0;
static const void** fpp_cvarGet = 0;
static const void** fpp_cvarSet = 0;
static const cvar_t* cvar_novis = 0;
static const int* tr_dp_skyportal = 0;
static byte* jmp_skyOnscreen = 0;
static const void* fp_addworldsurfaces = 0;
static const void* fp_dumpworldsurfaces = 0;
static byte* jmp_dumpworldsurfaces = 0;
static const byte* tr_dp_world = 0;
static byte* jmp_drawmarkedsurf = 0;
static byte* jmp_drawmarkedsurf_data = 0;
static int jmp_drawmarkedsurf_datasz = 0;
static byte* jmp_adddrawsurf = 0;
static byte* jmp_adddrawsurf_data = 0;
static int jmp_adddrawsurf_datasz = 0;
static void* fp_addworldsurf1 = 0;
static void* fp_addworldsurf2 = 0;
static const void* fp_test = 0;
static const int* tr_dp_viewcount = 0;
static byte* jmp_drawmarkedsurf2 = 0;
static byte* jmp_drawmarkedsurf2_data = 0;
static int jmp_drawmarkedsurf2_datasz = 0;

static void hook_qsortFast(void* base, unsigned num, unsigned width)
{
	HOOK_ONLINE_NOTICE();

	qsort(base, num, width, qsort_compare);

	key_inputs_t keys = keypress_get();
	if ( keys.ctrl && keys.p )
	{
		static bool once = false;
		if( !once )
		{
			once = true;

			const byte* surfaces = tr_dp_world + 0xa0;
			int numsurf = *(int*)(tr_dp_world + 0x9c);
			const byte* marksurf = tr_dp_world + 0xb0;
			int nummarksurf = *(int*)(tr_dp_world + 0xac);

			logPrintf( "Surfaces dump: %d\n", numsurf );
			for ( int i = 0; i < numsurf; i++ )
			{
				const unsigned int* ptr = (const unsigned int*)(surfaces + i * 0xc);
				logPrintf( "%d. %p %p %p - %d\n", i, ptr[0], ptr[1], ptr[2], *(int*)ptr[2] );
			}
			logPrintf( "Surfaces dump: ==========\n" );

			logPrintf( "Marksurfaces dump: %d\n", nummarksurf );
			for ( int i = 0; i < nummarksurf; i++ )
			{
				const unsigned int* ptr = (const unsigned int*)(marksurf + i * 0x28);
				logPrintf( " %d. %p %p %p %p %p %p %p %p %p %p\n", i,
					ptr[0], ptr[1], ptr[2], ptr[3], ptr[4],
					ptr[5], ptr[6], ptr[7], ptr[8], ptr[9] );
			}
			logPrintf( "Marksurfaces dump: ===========\n" );
		}

		logPrintf( "DumpSortSurf: %d %d %p\n", num, width, base );
		for ( int i = 0; i < num; i++ )
		{
			drawSurf_t* surf = (drawSurf_t*)((byte*)base + i * width);
			logPrintf( " %d. %p %p - %d", i, surf->sort, surf->surface, *surf->surface );
		}
		logPrintf( "DumpSortSurf: ========\n" );
	}
}

static void hook_markLeaves( void )
{
	HOOK_ONLINE_NOTICE();

	if ( !cvar_novis )
	{
		cvar_novis = ((cvar_t*(*)( const char *name, const char *value, int flags ))(*fpp_cvarGet))("r_novis", "0", 0x200);
	}

	int novis = cvar_novis->integer;
	if ( novis == 1 && *tr_dp_skyportal == 1 )
	{
		((void(*)( const char *, const char * ))(*fpp_cvarSet))( "r_novis", "0" );
	}
	else
	{
		//no need to re-set it after the real call
		novis = 0;
	}

	((void(*)(void))fp_markLeaves)(); //call original

	if ( novis )
	{
		((void(*)( const char *, const char * ))(*fpp_cvarSet))( "r_novis", "1" );
	}
}

static void hook_addworldsurfaces( void )
{
	HOOK_ONLINE_NOTICE();

	((void(*)())fp_dumpworldsurfaces)();

	((void(*)())fp_addworldsurfaces)();
}

static void hook_addworldsurf( void* ptr )
{
	if ( *(int*)ptr != *tr_dp_viewcount )
	{
		((void (*)(void*))fp_addworldsurf2)(ptr);
	}
}

static void hook_empty() {}

void* config_hex(const char* name, bool required, bool *not_found = 0)
{
	void* ret = 0;

	ret = D3DGlobal_ReadGameConfPtr( name );
	if (ret)
	{
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

int config_bytes(const char* name, byte* dest, int destsz, bool required, bool *not_found = 0)
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

static bool read_conf()
{
	bool not_found = false;
	bool opt_not_found = false;
	byte bufsrch[32];
	int srchsz = 0;

	fp_qsortFast = config_hex( "qsortFast", false );

	//search for hex patterns
	char name[32] = "qsortFast_pat\0";
	const int name_idxoff = strlen( name );
	for ( int i = 0; fp_qsortFast == NULL && i < 5; i++)
	{
		srchsz = config_bytes( name, bufsrch, sizeof( bufsrch ), false );
		if ( srchsz != 0 )
		{
			fp_qsortFast = hook_find_pattern( bufsrch, srchsz );
			logPrintf( "SurfaceSort: config %s addr %p\n", name, fp_qsortFast );
		}
		name[name_idxoff] = '0' + i;
	}

	fp_markLeaves = config_hex( "markLeaves", false );
	if ( fp_markLeaves )
	{
		fpp_cvarSet = (const void**)config_hex( "ri_fp_cvarSet", true, &not_found );
		fpp_cvarGet = (const void**)config_hex( "ri_fp_cvarGet", true, &not_found );
		tr_dp_skyportal = (const int*)config_hex( "tr_dp_skyportal", true, &not_found );
	}
	jmp_skyOnscreen = (byte*)config_hex( "jmp_skyOnscreen", false );

	fp_addworldsurfaces = config_hex( "fp_addworldsurfaces", false );
	if ( fp_addworldsurfaces )
	{
		fp_dumpworldsurfaces = config_hex( "fp_dumpworldsurfaces", true, &not_found );
		jmp_dumpworldsurfaces = (byte*)config_hex( "jmp_dumpworldsurfaces", true, &not_found );
	}
	tr_dp_world = (byte*)config_hex( "tr_dp_world", false );

	jmp_drawmarkedsurf = (byte*)config_hex( "jmp_drawmarkedsurf", false );
	if ( jmp_drawmarkedsurf )
	{
		srchsz = config_bytes( "jmp_drawmarkedsurf_data", bufsrch, sizeof(bufsrch), true, &not_found );
		if ( srchsz )
		{
			jmp_drawmarkedsurf_data = (byte*)malloc( srchsz );
			if ( !jmp_drawmarkedsurf_data ) { not_found = true; return false; }
			memcpy( jmp_drawmarkedsurf_data, bufsrch, srchsz );
			jmp_drawmarkedsurf_datasz = srchsz;
		}
	}

	jmp_adddrawsurf = (byte*)config_hex( "jmp_adddrawsurf", false );
	if ( jmp_adddrawsurf )
	{
		srchsz = config_bytes( "jmp_adddrawsurf_data", bufsrch, sizeof(bufsrch), true, &not_found );
		if ( srchsz )
		{
			jmp_adddrawsurf_data = (byte*)malloc( srchsz );
			if ( !jmp_adddrawsurf_data ) { not_found = true; return false; }
			memcpy( jmp_adddrawsurf_data, bufsrch, srchsz );
			jmp_adddrawsurf_datasz = srchsz;
		}
	}

	fp_addworldsurf1 = config_hex( "fp_addworldsurf1", false );
	if ( fp_addworldsurf1 )
	{
		fp_addworldsurf2 = config_hex( "fp_addworldsurf2", true, &not_found );
		tr_dp_viewcount = (int*)config_hex( "tr_dp_viewcount", true, &not_found );
	}

	jmp_drawmarkedsurf2 = (byte*)config_hex( "jmp_drawmarkedsurf2", false );
	if ( jmp_drawmarkedsurf2 )
	{
		srchsz = config_bytes( "jmp_drawmarkedsurf2_data", bufsrch, sizeof(bufsrch), true, &not_found );
		if ( srchsz )
		{
			jmp_drawmarkedsurf2_data = (byte*)malloc( srchsz );
			if ( !jmp_drawmarkedsurf2_data ) { not_found = true; return false; }
			memcpy( jmp_drawmarkedsurf2_data, bufsrch, srchsz );
			jmp_drawmarkedsurf2_datasz = srchsz;
		}
	}


	fp_test = config_hex( "fp_test", false );

	return (false == not_found);
}

#define BREAKABLE_BLOCK_BEGIN do
#define BREAKABKE_BLOCK_END while(0)
#define BREAK_ON_DETOUR_ERROR(X, Y) if ((X) != NO_ERROR) { (Y) = __LINE__; break; }

void hook_surface_sorting_do_init()
{
	LONG error;
	int errhint = 0;

	if (read_conf())
	{

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		BREAKABLE_BLOCK_BEGIN
		{
			if (fp_qsortFast)
			{
				error = DetourAttach(&(PVOID&)fp_qsortFast, hook_qsortFast);
				BREAK_ON_DETOUR_ERROR(error, errhint);
			}
			if (fp_markLeaves)
			{
				error = DetourAttach(&(PVOID&)fp_markLeaves, hook_markLeaves);
				BREAK_ON_DETOUR_ERROR(error, errhint);
			}
			if ( fp_addworldsurfaces )
			{
				error = DetourAttach(&(PVOID&)fp_addworldsurfaces, hook_addworldsurfaces);
				BREAK_ON_DETOUR_ERROR(error, errhint);
			}
			if ( fp_addworldsurf1 )
			{
				error = DetourAttach(&(PVOID&)fp_addworldsurf1, hook_addworldsurf);
				BREAK_ON_DETOUR_ERROR(error, errhint);
			}
			if ( fp_test )
			{
				error = DetourAttach(&(PVOID&)fp_test, hook_empty);
				BREAK_ON_DETOUR_ERROR(error, errhint);
			}
			error = DetourTransactionCommit();
		} BREAKABKE_BLOCK_END;

		if ( error != NO_ERROR )
		{
			DetourTransactionAbort();
		}

		if ( jmp_skyOnscreen && hook_unprotect(jmp_skyOnscreen, 1) )
		{
			jmp_skyOnscreen[0] = 0xeb;
		}

		if ( jmp_dumpworldsurfaces && hook_unprotect( jmp_dumpworldsurfaces, 1 ) )
		{
			jmp_dumpworldsurfaces[0] = 0xeb;
		}

		if ( jmp_drawmarkedsurf && hook_unprotect( jmp_drawmarkedsurf, jmp_drawmarkedsurf_datasz ) )
		{
			memcpy( jmp_drawmarkedsurf, jmp_drawmarkedsurf_data, jmp_drawmarkedsurf_datasz );
		}
		
		if ( jmp_adddrawsurf && hook_unprotect( jmp_adddrawsurf, jmp_adddrawsurf_datasz ) )
		{
			memcpy( jmp_adddrawsurf, jmp_adddrawsurf_data, jmp_adddrawsurf_datasz );
		}

		if ( jmp_drawmarkedsurf2 && hook_unprotect( jmp_drawmarkedsurf2, jmp_drawmarkedsurf2_datasz ) )
		{
			memcpy( jmp_drawmarkedsurf2, jmp_drawmarkedsurf2_data, jmp_drawmarkedsurf2_datasz );
		}

		logPrintf("SurfaceSort: detouring result: %d hint: %d\n", error, errhint);
	}
	else
	{
		logPrintf("SurfaceSort: ERROR configuration incomplete\n");
	}
}

void hook_surface_sorting_frame_ended()
{
}