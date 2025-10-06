
#include "rmx_gen.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <map>
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"

#include "ini.h"

#define INICONF_GLOBAL "global"
#define INICONF_SETTINGS "Settings"

remixapi_Interface remixInterface = { 0 };
BOOL remixOnline = FALSE;
static mINI::INIFile g_inifile("game_customise.ini");
static mINI::INIStructure g_iniconf;
static BOOL g_ini_first_init = FALSE;
static std::string active_map( "" );
static game_api g_game_api = NULL;

static void iniconf_first_init()
{
	if ( g_ini_first_init == FALSE )
	{
		g_ini_first_init = TRUE;
		char name[128];
		const char* gamename = D3DGlobal_GetGameName();
		const char* strippedgn = strchr( gamename, '.' );
		if ( strippedgn ) gamename = strippedgn +1;
		snprintf( name, sizeof name, "%s_customise.ini", gamename );
		g_inifile = mINI::INIFile(name);
		g_inifile.read( g_iniconf );
	}
}

void rmx_init_device(void *hwnd, void *device)
{
	iniconf_first_init();
	qdx_imgui_init(hwnd, device);
}

void rmx_deinit_device()
{
	qdx_imgui_deinit();
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

	qdx_lights_clear(LIGHT_ALL);
	//qdx_surface_aabb_clearall();

	if (g_inifile.read(g_iniconf))
	{
		if ( remixOnline )
		{
			mINI::INIMap<std::string>* default_opts;
			mINI::INIMap<std::string>* map_opts = 0;

			// Apply RTX.conf options
			snprintf( section, sizeof( section ), "rtxconf.%.*s", namelen, name );
			rmx_console_printf(PRINT_ALL, "RTX.conf section %s\n", section);
			if ( g_iniconf.has( section ) )
			{
				map_opts = &g_iniconf[ section ];
			}
			default_opts = &g_iniconf[ "rtxconf.default" ];

			for ( auto it = default_opts->begin(); it != default_opts->end(); it++ )
			{
				const char* key = it->first.c_str();
				const char* value = it->second.c_str();
				if ( map_opts && map_opts->has( key ) )
				{
					value = map_opts->operator[](key).c_str();
				}
				rmx_console_printf(PRINT_ALL, "Setting option %s = %s\n", key, value);
				remixapi_ErrorCode rercd = remixInterface.SetConfigVariable( key, value );
				if ( REMIXAPI_ERROR_CODE_SUCCESS != rercd )
				{
					rmx_console_printf( PRINT_ERROR, "RMX failed to set config var %d\n", rercd );
				}
			}
		}

		// Load Light settings
		qdx_lights_load( &g_iniconf, active_map.c_str() );
		//qdx_surface_replacements_load( g_iniconf, active_map.c_str() );
	}
}

void rmx_distant_light_radiance(float r, float g, float b, bool enabled)
{
	static char radiance[64];
	if (remixOnline)
	{
		remixapi_ErrorCode rercd;
		rercd = remixInterface.SetConfigVariable("rtx.fallbackLightMode", enabled ? "2" : "0");
		rercd = remixInterface.SetConfigVariable("rtx.fallbackLightType", "0");
		snprintf(radiance, sizeof(radiance), "%.3f, %.3f, %.3f", r, g, b);
		rercd = remixInterface.SetConfigVariable("rtx.fallbackLightRadiance", radiance);
		if (REMIXAPI_ERROR_CODE_SUCCESS != rercd)
		{
			rmx_console_printf(PRINT_ERROR, "RMX failed to set config var %d\n", rercd);
		}
	}
}

void rmx_set_game_api( game_api fn )
{
	g_game_api = fn;
}

void rmx_gamevar_set( const char *name, const char *val )
{
	if ( g_game_api )
		g_game_api( OP_SETVAR, name, val, NULL );
}

int rmx_gamevar_get( const char *name )
{
	if ( g_game_api )
	{
		gameparam_t ret = g_game_api( OP_GETVAR, name, NULL, NULL );
		return ret.intval;
	}
	return 0;
}

void rmx_deactivate_mouse()
{
	if ( g_game_api )
		g_game_api( OP_DEACTMOUSE, NULL, NULL, NULL );
}

void rmx_console_printf( int printLevel, const char *fmt, ... )
{
	if ( g_game_api )
	{
		static char msg[1024];
		va_list args;

		va_start( args, fmt );
		vsnprintf( msg, sizeof( msg ), fmt, args );
		va_end( args );
		g_game_api( OP_CONPRINT, printLevel, msg, NULL );
	}
}

void rmx_exec_cmd( const char *cmd )
{
	if ( g_game_api )
		g_game_api( OP_EXECMD, cmd, NULL, NULL );
}

static int sys_timeBase;
int rmx_milliseconds( void )
{
	static bool initialized = false;

	if ( !initialized ) {
		sys_timeBase = timeGetTime();
		initialized = true;
	}
	int sys_curtime = timeGetTime() -sys_timeBase;

	return sys_curtime;
}

void qdx_storemapconfflt( const char* base, const char* valname, float value, bool inGlobal )
{
	if ( inGlobal == false && active_map.length() == 0 )
	{
		return;
	}
	char data[32];
	snprintf( data, sizeof( data ), "%.4f", value );

	std::string section( base );
	section.append( "." );
	if ( inGlobal )
	{
		section.append( INICONF_GLOBAL );
	}
	else
	{
		section.append( active_map.c_str() );
	}
	g_iniconf[section][valname] = data;
}

float qdx_readmapconfflt( const char* base, const char* valname, float default_val )
{
	float ret = default_val;
	int tries = 0;
	std::string section( base );
	section.append( "." );
	if ( active_map.length() )
	{
		section.append( active_map.c_str() );
	}
	else
	{
		section.append( INICONF_GLOBAL );
		tries = 1;
	}

	for(; tries < 2; tries++)
	{
		if (g_iniconf.has(section) && g_iniconf[section].has(valname))
		{
			ret = strtof( g_iniconf[section][valname].c_str(), NULL );
			break;
		}
		else
		{
			section.assign( base );
			section.append( "." );
			section.append( INICONF_GLOBAL );
		}
	}
	return ret;
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
