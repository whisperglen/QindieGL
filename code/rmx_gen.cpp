
#include "rmx_gen.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <map>
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"


#define MINI_CASE_SENSITIVE
#include "ini.h"

static mINI::INIFile g_inifile("game_customise.ini");
static mINI::INIStructure g_iniconf;
static BOOL g_ini_first_init = FALSE;
static std::string active_map( "" );

static void iniconf_first_init()
{
	if ( g_ini_first_init == FALSE )
	{
		g_ini_first_init = TRUE;
		char name[128];
		snprintf( name, sizeof name, "%s_customise.ini", D3DGlobal_GetGameName() );
		g_inifile = mINI::INIFile(name);
		g_inifile.read( g_iniconf );
	}
}

mINI::INIStructure& qdx_get_iniconf()
{
	return g_iniconf;
}

void qdx_save_iniconf()
{
	g_inifile.write( g_iniconf, true );
}

const char* qdx_get_active_map()
{
	return active_map.c_str();
}

void qdx_begin_loading_map( const char* mapname )
{
	static char section[256];

	const char *name = strrchr(mapname, '/');
	if ( name == NULL )
	{
		name = mapname;
	}
	else
	{
		name++;
	}

	const char* ext = strrchr(mapname, '.');
	int namelen = 0;
	if(ext)
	{
		namelen = ext - name;
	}
	else
	{
		namelen = strlen(name);
	}

	active_map.assign( name, namelen );
}

void qdx_storemapconfflt( const char* base, const char* valname, float value, bool inGlobal )
{

}

float qdx_readmapconfflt( const char* base, const char* valname, float default_val )
{
	return 0;
}

static std::map<std::string, int> asserted_fns;
#define ASSERT_MAX_PRINTS 1

void qdx_assert_failed_str(const char* expression, const char* function, unsigned line, const char* file)
{
	const char* fn = strrchr(file, '\\');
	if (!fn) fn = strrchr(file, '/');
	if (!fn) fn = "fnf";

	bool will_print = false;
	bool supressed_msg = false;

	std::string mykey = std::string(function);
	auto searched = asserted_fns.find(mykey);
	if (asserted_fns.end() != searched)
	{
		int nums = searched->second;
		if (nums <= ASSERT_MAX_PRINTS)
		{
			will_print = true;
			supressed_msg = (nums == ASSERT_MAX_PRINTS);
		}
		searched->second = nums + 1;
	}
	else
	{
		asserted_fns[mykey] = 1;
		will_print = true;
	}

	if (will_print)
	{
#ifdef NDEBUG
		if(supressed_msg)
			logPrintf("Error: assert failed and supressing: %s in %s:%d %s\n", expression, function, line, fn);
		else
			logPrintf("Error: assert failed: %s in %s:%d %s\n", expression, function, line, fn);
#else
		logPrintf("Error: assert failed: %s in %s:%d %s\n", expression, function, line, fn);
#endif
	}
}
