
#include "surface_sorting.h"

#include <Windows.h>
#include <psapi.h>
#include "detours.h"
#include <search.h>

#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"

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

static void hook_qsortFast(void* base, unsigned num, unsigned width)
{
	HOOK_ONLINE_NOTICE();

	qsort(base, num, width, qsort_compare);
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
		
		logPrintf( "SurfaceSort: config %s found %d bytes %x %x %x %x\n", name, ret,
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

	const char* name;
	fp_qsortFast = config_hex( "qsortFast", false );
	name = "qsortFast_pat";
	srchsz = config_bytes( name, bufsrch, sizeof( bufsrch ), false );
	if ( !fp_qsortFast && srchsz != 0 )
	{
		fp_qsortFast = hook_find_pattern( bufsrch, srchsz );
		logPrintf("SurfaceSort: config %s found %p\n", name, fp_qsortFast);
	}

	fp_markLeaves = config_hex( "markLeaves", false );
	if ( fp_markLeaves )
	{
		fpp_cvarSet = (const void**)config_hex( "ri_fp_cvarSet", true, &not_found );
		fpp_cvarGet = (const void**)config_hex( "ri_fp_cvarGet", true, &not_found );
		tr_dp_skyportal = (const int*)config_hex( "tr_dp_skyportal", true, &not_found );
	}
	jmp_skyOnscreen = (byte*)config_hex( "jmp_skyOnscreen", false );

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

		logPrintf("SurfaceSort: detouring result: %d hint: %d\n", error, errhint);
	}
	else
	{
		logPrintf("SurfaceSort: ERROR configuration incomplete\n");
	}
}