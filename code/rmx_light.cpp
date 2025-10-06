
#include "rmx_light.h"
#include "rmx_gen.h"
#include <d3d9.h>
#include "d3dx9.h"

#include <stdint.h>
#include "fnv.h"

#include <string>
#include <vector>
#include <map>
#include <list>

#include "mINI/ini.h"

#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"

typedef float vec_t;
typedef float vec3_t[3];

#define LIGHTS_DYNAMIC_SZ 64
typedef uint64_t iterdl_t;

typedef struct light_data
{
	uint8_t updatedThisFrame;
	uint8_t isRemix;
	union
	{
		DWORD number;
		remixapi_LightHandle handle;
	};
	uint64_t hash;
	uint64_t data_hash;
	vec3_t pos;
	vec3_t color;
} light_data_t;

struct color_override_data_s
{
	float src[3];
	float dst[3];
};

/**
 * Function definitions
 */
static light_override_t* qdx_light_find_override( uint64_t hash );
static vec_t Distance( const vec3_t p1, const vec3_t p2 );

/**
 * Globals
 */
static int rmx_flashlight = 0;

static light_data_t g_lights_dynamic[LIGHTS_DYNAMIC_SZ] = { 0 };
static iterdl_t g_lights_dynamic_bitmap = 0;
static std::map<uint64_t, light_data_t> g_lights_flares;
static remixapi_LightHandle g_flashlight_handle[NUM_FLASHLIGHT_HND] = { 0 };
#define FLASHLIGHT_HASH 0xF1A581168700ULL

//no idea what to choose here
#define DEFAULT_LIGHT_RADIANCE_DYNAMIC_BASE 75.0f
#define DEFAULT_LIGHT_RADIANCE_DYNAMIC_SCALE 0.0f
#define DEFAULT_LIGHT_RADIANCE_CORONAS 150.0f
#define DEFAULT_LIGHT_RADIUS 2.0f
#define DEFAULT_CORONA_RADIUS 6.0f
#define DEFAULT_LIGHT_RADIUS_DYNAMIC_SCALE 0.0f
static float LIGHT_RADIANCE_DYNAMIC_BASE = DEFAULT_LIGHT_RADIANCE_DYNAMIC_BASE;
static float LIGHT_RADIANCE_DYNAMIC_SCALE = DEFAULT_LIGHT_RADIANCE_DYNAMIC_SCALE;
static float LIGHT_RADIANCE_CORONAS = DEFAULT_LIGHT_RADIANCE_CORONAS;
static float LIGHT_RADIUS[2] = { DEFAULT_LIGHT_RADIUS, DEFAULT_CORONA_RADIUS };
static float LIGHT_RADIUS_DYNAMIC_SCALE = DEFAULT_LIGHT_RADIUS_DYNAMIC_SCALE;
static float LIGHT_RADIANCE_FLASHLIGHT[NUM_FLASHLIGHT_HND] = { 400.0f, 960.0f, 4000.0f };
static float FLASHLIGHT_COLORS[NUM_FLASHLIGHT_HND][3] = {
	{ 0.9f, 0.98f, 1.0f },
	{ 1.0f, 0.95f, 0.9f },
	{ 0.9f, 1.0f, 0.97f }
};
static float FLASHLIGHT_CONE_ANGLES[NUM_FLASHLIGHT_HND] = { 32.0f, 21.0f, 18.0f }; //{ 40.0f, 28.0f, 24.0f };
static float FLASHLIGHT_CONE_SOFTNESS[NUM_FLASHLIGHT_HND] = { 0.05f, 0.02f, 0.02f };
static float FLASHLIGHT_VOLUMETRIC[NUM_FLASHLIGHT_HND] = { 1.0f, 1.0f, 1.0f };
static float FLASHLIGHT_POSITION_CACHE[3] = { 0 };
static float FLASHLIGHT_DIRECTION_CACHE[3] = { 0 };
static float FLASHLIGHT_POSITION_OFFSET[3] = { -15, 18, 13 };
static float FLASHLIGHT_DIRECTION_OFFSET[3] = { 0.095f, -0.08f, 0 };
static uint64_t LIGHT_PICKING_IDS[3] = { 0 };
static uint32_t LIGHT_PICKING_COUNT = 0;

static std::vector<struct color_override_data_s> g_color_overrides;
static std::map<uint64_t, light_override_t> g_light_overrides;

float* qdx_4imgui_radiance_dynamic_1f() { return &LIGHT_RADIANCE_DYNAMIC_BASE; }
float* qdx_4imgui_radiance_dynamic_scale_1f() { return &LIGHT_RADIANCE_DYNAMIC_SCALE; }
float* qdx_4imgui_radiance_coronas_1f() { return &LIGHT_RADIANCE_CORONAS; }
float* qdx_4imgui_radius_dynamic_1f() { return &LIGHT_RADIUS[0]; }
float* qdx_4imgui_radius_dynamic_scale_1f() { return &LIGHT_RADIUS_DYNAMIC_SCALE; }
float* qdx_4imgui_radius_coronas_1f() { return &LIGHT_RADIUS[1]; }
float* qdx_4imgui_flashlight_radiance_1f(int idx) { return &LIGHT_RADIANCE_FLASHLIGHT[idx]; }
float* qdx_4imgui_flashlight_colors_3f(int idx) { return &FLASHLIGHT_COLORS[idx][0]; }
float* qdx_4imgui_flashlight_coneangles_1f(int idx) { return &FLASHLIGHT_CONE_ANGLES[idx]; }
float* qdx_4imgui_flashlight_conesoft_1f(int idx) { return &FLASHLIGHT_CONE_SOFTNESS[idx]; }
float* qdx_4imgui_flashlight_volumetric_1f(int idx) { return &FLASHLIGHT_VOLUMETRIC[idx]; }
const float* qdx_4imgui_flashlight_position_3f() { return &FLASHLIGHT_POSITION_CACHE[0]; }
const float* qdx_4imgui_flashlight_direction_3f() { return &FLASHLIGHT_DIRECTION_CACHE[0]; }
float* qdx_4imgui_flashlight_position_off_3f() { return &FLASHLIGHT_POSITION_OFFSET[0]; }
float* qdx_4imgui_flashlight_direction_off_3f() { return &FLASHLIGHT_DIRECTION_OFFSET[0]; }
uint64_t qdx_4imgui_light_picking_id( uint32_t idx ) { if ( LIGHT_PICKING_COUNT == 0 ) { return 0; } else if ( idx >= LIGHT_PICKING_COUNT ) { idx = LIGHT_PICKING_COUNT - 1; } return LIGHT_PICKING_IDS[idx]; }
int qdx_4imgui_light_picking_count() { return LIGHT_PICKING_COUNT; }
void qdx_4imgui_light_picking_clear() { LIGHT_PICKING_COUNT = 0; }

void qdx_4imgui_light_clear_override( uint64_t hash )
{
	auto it = g_light_overrides.find( hash );
	if ( it != g_light_overrides.end() )
	{
		g_light_overrides.erase( it );
	}
}

light_override_t* qdx_4imgui_light_get_override( uint64_t hash, light_type_e type )
{
	if ( hash == 0 )
		return NULL;

	light_override_t* ret = qdx_light_find_override( hash );
	if ( ret == NULL )
	{
		light_override_t local;
		ZeroMemory( &local, sizeof( local ) );
		local.hash = hash;
		local.position_offset[0] = 0.0f;
		local.position_offset[1] = 0.0f;
		local.position_offset[2] = 0.0f;
		local.color[0] = 1.0f;
		local.color[1] = 1.0f;
		local.color[2] = 1.0f;
		if ( type == LIGHT_DYNAMIC )
		{
			iterdl_t keep_searching = g_lights_dynamic_bitmap;
			light_data_t* l = &g_lights_dynamic[0];
			iterdl_t bit = 1;
			for ( int i = 0; keep_searching && i < LIGHTS_DYNAMIC_SZ; i++, l++, bit <<= 1 )
			{
				if ( g_lights_dynamic_bitmap & bit )
				{
					if ( l->hash == hash )
					{
						local.color[0] = l->color[0];
						local.color[1] = l->color[1];
						local.color[2] = l->color[2];
						break;
					}
				}
			}
			local.radiance_base = LIGHT_RADIANCE_DYNAMIC_BASE;
			local.radiance_scale = LIGHT_RADIANCE_DYNAMIC_SCALE;
			local.radius_base = LIGHT_RADIUS[0];
			local.radius_scale = LIGHT_RADIUS_DYNAMIC_SCALE;
		}
		else if ( type == LIGHT_CORONA )
		{
			for ( auto it = g_lights_flares.begin(); it != g_lights_flares.end(); it++ )
			{
				if ( it->second.hash == hash )
				{
					local.color[0] = it->second.color[0];
					local.color[1] = it->second.color[1];
					local.color[2] = it->second.color[2];
					break;
				}
			}
			local.radiance_base = LIGHT_RADIANCE_CORONAS;
			local.radius_base = LIGHT_RADIUS[1];
		}

		ret = &g_light_overrides[hash];
		*ret = local;
	}
	return ret;
}

void qdx_light_scan_closest_lights(light_type_e type)
{
	int i;
	std::list<std::pair<float,uint64_t>> distvhash;

	if ( type == LIGHT_DYNAMIC )
	{
		iterdl_t keep_searching = g_lights_dynamic_bitmap;
		light_data_t* l = &g_lights_dynamic[0];
		iterdl_t bit = 1;
		for ( i = 0; keep_searching && i < LIGHTS_DYNAMIC_SZ; i++, l++, bit <<= 1 )
		{
			if ( g_lights_dynamic_bitmap & bit )
			{
				vec3_t position = { l->pos[0], l->pos[1], l->pos[2] };
				light_override_t* ovr = qdx_light_find_override( l->hash );
				if ( ovr )
				{
					position[0] += ovr->position_offset[0];
					position[1] += ovr->position_offset[1];
					position[2] += ovr->position_offset[2];
				}
				float dist = Distance( FLASHLIGHT_POSITION_CACHE, position ); //use the flashlight cache since we have it
				distvhash.push_back( std::make_pair( dist, l->hash ) );
				keep_searching &= ~bit;
			}
		}
	}
	if ( type == LIGHT_CORONA )
	{
		for ( auto it = g_lights_flares.begin(); it != g_lights_flares.end(); it++ )
		{
			float dist = Distance( FLASHLIGHT_POSITION_CACHE, it->second.pos ); //use the flashlight cache since we have it
			distvhash.push_back( std::make_pair( dist, it->second.hash ) );
		}
	}

	distvhash.sort([](const auto& a, const auto& b) {
		return a.first
			< b.first; // use > for descending order
		});

	auto it = distvhash.begin();
	for ( i = 0; i < ARRAYSIZE( LIGHT_PICKING_IDS ) && it != distvhash.end(); i++, it++ )
	{
		LIGHT_PICKING_IDS[i] = it->second;
	}
	LIGHT_PICKING_COUNT = i;
}

static light_override_t* qdx_light_find_override( uint64_t hash )
{
	auto itm = g_light_overrides.find(hash);
	if (itm != g_light_overrides.end())
	{
		return &(itm->second);
	}
	return NULL;
}

void rmx_setplayerpos( const float* origin, const float* direction )
{
	const vec3_t color = { 1.0f, 1.0f, 1.0f };
	qdx_light_add( LIGHT_FLASHLIGHT, 0, origin, direction, color, 0 );
}

void rmx_flashlight_enable( int val )
{
	if ( val == 0 || val == 1 )
	{
		rmx_flashlight = val;
	}
	else
	{
		rmx_flashlight ^= 1;
	}
}

static bool str_starts_with(const std::string& str, const std::string& prefix)
{
	return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

static bool str_starts_with(const std::string& str, const char* prefix, unsigned prefixLen)
{
	return str.size() >= prefixLen && str.compare(0, prefixLen, prefix, prefixLen) == 0;
}

static bool str_starts_with(const std::string& str, const char* prefix)
{
	return str_starts_with(str, prefix, std::string::traits_type::length(prefix));
}

static bool str_ends_with(const std::string& str, const std::string& suffix)
{
	return str.size() >= suffix.size() && str.compare(str.size()-suffix.size(), suffix.size(), suffix) == 0;
}

static bool str_ends_with(const std::string& str, const char* suffix, unsigned suffixLen)
{
	return str.size() >= suffixLen && str.compare(str.size()-suffixLen, suffixLen, suffix, suffixLen) == 0;
}

static bool str_ends_with(const std::string& str, const char* suffix)
{
	return str_ends_with(str, suffix, std::string::traits_type::length(suffix));
}

#define SECTION_LIGHTS "lights"
#define SECTION_FLASHLIGHT "flashlight"
#define SECTION_LIGHT_OVERRIDE "lightovrd"
#define SECTION_LIGHT_OVERRIDE_PRINT "lightovrd.0x%x%x"
#define SECTION_LIGHTS_COLOR_OVERRIDE "lights_color_override"

void qdx_flashlight_save()
{
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "PositionOff_0", FLASHLIGHT_POSITION_OFFSET[0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "PositionOff_1", FLASHLIGHT_POSITION_OFFSET[1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "PositionOff_2", FLASHLIGHT_POSITION_OFFSET[2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "DirectionOff_0", FLASHLIGHT_DIRECTION_OFFSET[0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "DirectionOff_1", FLASHLIGHT_DIRECTION_OFFSET[1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "DirectionOff_2", FLASHLIGHT_DIRECTION_OFFSET[2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Radiance_Spot_1", LIGHT_RADIANCE_FLASHLIGHT[0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Radiance_Spot_2", LIGHT_RADIANCE_FLASHLIGHT[1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Radiance_Spot_3", LIGHT_RADIANCE_FLASHLIGHT[2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_R_Spot_1", FLASHLIGHT_COLORS[0][0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_G_Spot_1", FLASHLIGHT_COLORS[0][1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_B_Spot_1", FLASHLIGHT_COLORS[0][2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_R_Spot_2", FLASHLIGHT_COLORS[1][0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_G_Spot_2", FLASHLIGHT_COLORS[1][1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_B_Spot_2", FLASHLIGHT_COLORS[1][2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_R_Spot_3", FLASHLIGHT_COLORS[2][0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_G_Spot_3", FLASHLIGHT_COLORS[2][1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Color_B_Spot_3", FLASHLIGHT_COLORS[2][2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Angles_Spot_1", FLASHLIGHT_CONE_ANGLES[0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Angles_Spot_2", FLASHLIGHT_CONE_ANGLES[1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Angles_Spot_3", FLASHLIGHT_CONE_ANGLES[2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Softness_Spot_1", FLASHLIGHT_CONE_SOFTNESS[0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Softness_Spot_2", FLASHLIGHT_CONE_SOFTNESS[1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Softness_Spot_3", FLASHLIGHT_CONE_SOFTNESS[2] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Volumetric_Spot_1", FLASHLIGHT_VOLUMETRIC[0] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Volumetric_Spot_2", FLASHLIGHT_VOLUMETRIC[1] );
	qdx_storemapconfflt( SECTION_FLASHLIGHT, "Volumetric_Spot_3", FLASHLIGHT_VOLUMETRIC[2] );

	qdx_save_iniconf();
}

void qdx_radiance_save( bool inGlobal )
{
	qdx_storemapconfflt( SECTION_LIGHTS, "DynamicRadiance", LIGHT_RADIANCE_DYNAMIC_BASE, inGlobal );
	qdx_storemapconfflt( SECTION_LIGHTS, "DynamicRadianceScale", LIGHT_RADIANCE_DYNAMIC_SCALE, inGlobal );
	qdx_storemapconfflt( SECTION_LIGHTS, "DynamicRadius", LIGHT_RADIUS[0], inGlobal );
	qdx_storemapconfflt( SECTION_LIGHTS, "DynamicRadiusScale", LIGHT_RADIUS_DYNAMIC_SCALE, inGlobal );
	qdx_storemapconfflt( SECTION_LIGHTS, "CoronaRadiance", LIGHT_RADIANCE_CORONAS, inGlobal );
	qdx_storemapconfflt( SECTION_LIGHTS, "CoronaRadius", LIGHT_RADIUS[1], inGlobal );

	qdx_save_iniconf();
}

void qdx_light_override_save( light_override_t* ovr, bool writeOut )
{
	char section[64];
	uint64_comp_t idval;
	idval.ll = ovr->hash;
	snprintf( section, sizeof( section ), SECTION_LIGHT_OVERRIDE_PRINT, idval.u32[1], idval.u32[0] );
	qdx_storemapconfflt( section, "PositionOffset0", ovr->position_offset[0], FALSE );
	qdx_storemapconfflt( section, "PositionOffset1", ovr->position_offset[1], FALSE );
	qdx_storemapconfflt( section, "PositionOffset2", ovr->position_offset[2], FALSE );
	qdx_storemapconfflt( section, "Color0", ovr->color[0], FALSE );
	qdx_storemapconfflt( section, "Color1", ovr->color[1], FALSE );
	qdx_storemapconfflt( section, "Color2", ovr->color[2], FALSE );
	qdx_storemapconfflt( section, "Radiance", ovr->radiance_base, FALSE );
	qdx_storemapconfflt( section, "RadianceScale", ovr->radiance_scale, FALSE );
	qdx_storemapconfflt( section, "Radius", ovr->radius_base, FALSE );
	qdx_storemapconfflt( section, "RadiusScale", ovr->radius_scale, FALSE );
	qdx_storemapconfflt( section, "Volumetric", ovr->volumetric_scale, FALSE );

	if ( writeOut )
	{
		qdx_save_iniconf();
	}
}

void qdx_light_override_save_all()
{
	for(auto it = g_light_overrides.begin(); it != g_light_overrides.end(); it++)
	{
		qdx_light_override_save(&(it->second), false);
	}

	qdx_save_iniconf();
}

void qdx_light_override_load( uint64_t hash, light_override_t* ovr )
{
	char section[64];
	uint64_comp_t idval;
	idval.ll = hash;
	snprintf( section, sizeof( section ), SECTION_LIGHT_OVERRIDE_PRINT, idval.u32[1], idval.u32[0] );
	ovr->hash = hash;
	ovr->position_offset[0] = qdx_readmapconfflt( section, "PositionOffset0", 0.0f );
	ovr->position_offset[1] = qdx_readmapconfflt( section, "PositionOffset1", 0.0f );
	ovr->position_offset[2] = qdx_readmapconfflt( section, "PositionOffset2", 0.0f );
	ovr->color[0] = qdx_readmapconfflt( section, "Color0", 1.0f );
	ovr->color[1] = qdx_readmapconfflt( section, "Color1", 1.0f );
	ovr->color[2] = qdx_readmapconfflt( section, "Color2", 1.0f );
	ovr->radiance_base = qdx_readmapconfflt( section, "Radiance", DEFAULT_LIGHT_RADIANCE_DYNAMIC_BASE );
	ovr->radiance_scale = qdx_readmapconfflt( section, "RadianceScale", DEFAULT_LIGHT_RADIANCE_DYNAMIC_SCALE );
	ovr->radius_base = qdx_readmapconfflt( section, "Radius", DEFAULT_LIGHT_RADIUS );
	ovr->radius_scale = qdx_readmapconfflt( section, "RadiusScale", DEFAULT_LIGHT_RADIUS_DYNAMIC_SCALE );
	ovr->volumetric_scale = qdx_readmapconfflt( section, "Volumetric", 0.0f );
	ovr->updated = 0;
}

void qdx_lights_load( void *pini, const char *mapname )
{
	mINI::INIStructure &ini = *(mINI::INIStructure*)pini;

	LIGHT_RADIANCE_DYNAMIC_BASE = qdx_readmapconfflt( SECTION_LIGHTS, "DynamicRadiance", DEFAULT_LIGHT_RADIANCE_DYNAMIC_BASE );
	LIGHT_RADIANCE_DYNAMIC_SCALE = qdx_readmapconfflt( SECTION_LIGHTS, "DynamicRadianceScale", DEFAULT_LIGHT_RADIANCE_DYNAMIC_SCALE );
	LIGHT_RADIUS[0] = qdx_readmapconfflt( SECTION_LIGHTS, "DynamicRadius", DEFAULT_LIGHT_RADIUS );
	LIGHT_RADIUS_DYNAMIC_SCALE = qdx_readmapconfflt( SECTION_LIGHTS, "DynamicRadiusScale", DEFAULT_LIGHT_RADIUS_DYNAMIC_SCALE );
	LIGHT_RADIANCE_CORONAS = qdx_readmapconfflt( SECTION_LIGHTS, "CoronaRadiance", DEFAULT_LIGHT_RADIANCE_CORONAS );
	LIGHT_RADIUS[1] = qdx_readmapconfflt( SECTION_LIGHTS, "CoronaRadius", DEFAULT_LIGHT_RADIUS );

	LIGHT_RADIANCE_FLASHLIGHT[0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Radiance_Spot_1", LIGHT_RADIANCE_FLASHLIGHT[0] );
	LIGHT_RADIANCE_FLASHLIGHT[1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Radiance_Spot_2", LIGHT_RADIANCE_FLASHLIGHT[1] );
	LIGHT_RADIANCE_FLASHLIGHT[2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Radiance_Spot_3", LIGHT_RADIANCE_FLASHLIGHT[2] );
	FLASHLIGHT_COLORS[0][0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_R_Spot_1", FLASHLIGHT_COLORS[0][0] );
	FLASHLIGHT_COLORS[0][1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_G_Spot_1", FLASHLIGHT_COLORS[0][1] );
	FLASHLIGHT_COLORS[0][2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_B_Spot_1", FLASHLIGHT_COLORS[0][2] );
	FLASHLIGHT_COLORS[1][0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_R_Spot_2", FLASHLIGHT_COLORS[1][0] );
	FLASHLIGHT_COLORS[1][1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_G_Spot_2", FLASHLIGHT_COLORS[1][1] );
	FLASHLIGHT_COLORS[1][2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_B_Spot_2", FLASHLIGHT_COLORS[1][2] );
	FLASHLIGHT_COLORS[2][0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_R_Spot_3", FLASHLIGHT_COLORS[2][0] );
	FLASHLIGHT_COLORS[2][1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_G_Spot_3", FLASHLIGHT_COLORS[2][1] );
	FLASHLIGHT_COLORS[2][2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Color_B_Spot_3", FLASHLIGHT_COLORS[2][2] );
	FLASHLIGHT_CONE_ANGLES[0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Angles_Spot_1", FLASHLIGHT_CONE_ANGLES[0] );
	FLASHLIGHT_CONE_ANGLES[1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Angles_Spot_2", FLASHLIGHT_CONE_ANGLES[1] );
	FLASHLIGHT_CONE_ANGLES[2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Angles_Spot_3", FLASHLIGHT_CONE_ANGLES[2] );
	FLASHLIGHT_CONE_SOFTNESS[0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Softness_Spot_1", FLASHLIGHT_CONE_SOFTNESS[0] );
	FLASHLIGHT_CONE_SOFTNESS[1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Softness_Spot_2", FLASHLIGHT_CONE_SOFTNESS[1] );
	FLASHLIGHT_CONE_SOFTNESS[2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Softness_Spot_3", FLASHLIGHT_CONE_SOFTNESS[2] );
	FLASHLIGHT_VOLUMETRIC[0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Volumetric_Spot_1", FLASHLIGHT_VOLUMETRIC[0] );
	FLASHLIGHT_VOLUMETRIC[1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Volumetric_Spot_2", FLASHLIGHT_VOLUMETRIC[1] );
	FLASHLIGHT_VOLUMETRIC[2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "Volumetric_Spot_3", FLASHLIGHT_VOLUMETRIC[2] );
	FLASHLIGHT_POSITION_OFFSET[0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "PositionOff_0", FLASHLIGHT_POSITION_OFFSET[0] );
	FLASHLIGHT_POSITION_OFFSET[1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "PositionOff_1", FLASHLIGHT_POSITION_OFFSET[1] );
	FLASHLIGHT_POSITION_OFFSET[2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "PositionOff_2", FLASHLIGHT_POSITION_OFFSET[2] );
	FLASHLIGHT_DIRECTION_OFFSET[0] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "DirectionOff_0", FLASHLIGHT_DIRECTION_OFFSET[0] );
	FLASHLIGHT_DIRECTION_OFFSET[1] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "DirectionOff_1", FLASHLIGHT_DIRECTION_OFFSET[1] );
	FLASHLIGHT_DIRECTION_OFFSET[2] = qdx_readmapconfflt( SECTION_FLASHLIGHT, "DirectionOff_2", FLASHLIGHT_DIRECTION_OFFSET[2] );

	if ( ini.has( SECTION_LIGHTS_COLOR_OVERRIDE ) )
	{
		int i = 0; int ercd;
		struct color_override_data_s data;
		mINI::INIMap<std::string> &ovr = ini[ SECTION_LIGHTS_COLOR_OVERRIDE ];

		g_color_overrides.clear();
		g_color_overrides.reserve( ovr.size() / 2 );
		for ( auto it = ovr.begin(); it != ovr.end(); it++ )
		{
			if ( it->first.find( "source" ) == std::string::npos ) continue;
			ercd = sscanf_s( it->second.c_str(), "%f, %f, %f", &data.src[0], &data.src[1], &data.src[2] );
			if ( ercd != 3 ) continue;

			it++;
			if ( it->first.find( "new" ) == std::string::npos ) continue;
			ercd = sscanf_s( it->second.c_str(), "%f, %f, %f", &data.dst[0], &data.dst[1], &data.dst[2] );
			if ( ercd != 3 ) continue;

			g_color_overrides.push_back( data );
			i++;
		}
	}

	qdx_4imgui_light_picking_clear();
	g_light_overrides.clear();
	for ( auto const& it : ini )
	{
		auto const& section = it.first;
		if ( str_starts_with( section, SECTION_LIGHT_OVERRIDE ) && str_ends_with( section, mapname ) )
		{
			const char* hashstart = section.c_str() + sizeof( SECTION_LIGHT_OVERRIDE ) + 2;
			uint64_t hash = _strtoui64( hashstart, NULL, 16 );
			if ( hash )
			{
				light_override_t ovr;
				qdx_light_override_load( hash, &ovr );
				g_light_overrides[hash] = ovr;
			}
		}
	}
}

static void qdx_light_color_to_radiance(remixapi_Float3D* rad, int light_type, const vec3_t color, float intensity)
{
	float radiance = 1.0f;
	if (light_type == LIGHT_DYNAMIC)
	{
		radiance = LIGHT_RADIANCE_DYNAMIC_BASE + LIGHT_RADIANCE_DYNAMIC_SCALE * intensity;

		rad->x = radiance * color[0];
		rad->y = radiance * color[1];
		rad->z = radiance * color[2];
	}
	else if (light_type == LIGHT_CORONA)
	{
		radiance = LIGHT_RADIANCE_CORONAS;

		for ( auto it = g_color_overrides.begin(); it != g_color_overrides.end(); it++ )
		{
			if ( (color[0] == it->src[0]) && (color[1] == it->src[1]) && (color[2] == it->src[2]) )
			{
				rad->x = radiance * it->dst[0];
				rad->y = radiance * it->dst[1];
				rad->z = radiance * it->dst[2];

				return;
			}
		}

		rad->x = radiance * color[0];
		rad->y = radiance * color[1];
		rad->z = radiance * color[2];
	}
}

static void qdx_light_color_to_radiance_flashlight(remixapi_Float3D* rad, int light_type, int idx, const vec3_t color)
{
	float radiance = 1.0f;
	if ( light_type == LIGHT_FLASHLIGHT )
	{
		if ( idx < 0 || idx >= NUM_FLASHLIGHT_HND ) idx = 0;
		radiance = LIGHT_RADIANCE_FLASHLIGHT[idx];

		rad->x = radiance * color[0];
		rad->y = radiance * color[1];
		rad->z = radiance * color[2];
	}
}

static void qdx_light_color_to_radiance_override(remixapi_Float3D* rad, int light_type, float intensity, light_override_t *ovr)
{
	float radiance = ovr->radiance_base;
	if (light_type == LIGHT_DYNAMIC)
	{
		radiance += ovr->radiance_scale * intensity;
	}

	rad->x = radiance * ovr->color[0];
	rad->y = radiance * ovr->color[1];
	rad->z = radiance * ovr->color[2];
}

static float qdx_light_radius( int light_type, float radius )
{
	float ret = 2.0f;
	
	if ( light_type == LIGHT_DYNAMIC )
	{
		ret = LIGHT_RADIUS[0] + LIGHT_RADIUS_DYNAMIC_SCALE * radius;
	}
	else if ( light_type )
	{
		ret = LIGHT_RADIUS[1];
	}

	return ret;
}

static float qdx_light_radius_override( int light_type, float radius, light_override_t *ovr )
{
	float ret = ovr->radius_base;

	if ( light_type == LIGHT_DYNAMIC )
	{
		ret += ovr->radius_scale * radius;
	}

	return ret;
}

static int g_dynamic_light_linger = 1;

void qdx_lights_dynamic_linger( int val )
{
	g_dynamic_light_linger = val;
}

light_data_t* qdx_dynamiclight_get_slot( uint64_t hash )
{
	iterdl_t keep_searching = g_lights_dynamic_bitmap;
	light_data_t* l = &g_lights_dynamic[0];
	light_data_t* sto = NULL;
	iterdl_t sto_bit = 0;
	iterdl_t bit = 1;
	for ( int i = 0; (keep_searching || sto == NULL) && i < LIGHTS_DYNAMIC_SZ; i++, l++, bit <<= 1 )
	{
		if ( g_lights_dynamic_bitmap & bit )
		{
			if ( l->hash == hash )
			{
				l->updatedThisFrame = (uint8_t)g_dynamic_light_linger;
				return l;
			}
			keep_searching &= ~bit;
		}
		else
		{
			if ( sto == NULL )
			{
				sto = l;
				sto_bit = bit;
			}
		}
	}
	if ( sto != NULL )
	{
		g_lights_dynamic_bitmap |= sto_bit;
		sto->updatedThisFrame = (uint8_t)g_dynamic_light_linger;
		sto->hash = hash;
		sto->data_hash = 0;
		return sto;
	}
	else
	{
		qassert( 0 && "Not enough Dynamic light slots!" );
	}
	return NULL;
}

void qdx_lights_clear(unsigned int light_types)
{
	//if (qdx.device)
	{
		if (light_types & LIGHT_DYNAMIC)
		{
			light_data_t* l = &g_lights_dynamic[0];
			iterdl_t keep_searching = g_lights_dynamic_bitmap;
			iterdl_t bit = 1;
			for (int i = 0; keep_searching && i < LIGHTS_DYNAMIC_SZ; i++, l++, bit <<= 1)
			{
				if ( g_lights_dynamic_bitmap & bit )
				{
					if ( l->isRemix )
					{
						if( l->handle )
							remixInterface.DestroyLight( l->handle );
					}
					g_lights_dynamic_bitmap &= ~bit;
					keep_searching &= ~bit;
				}
			}
		}
		if (light_types & LIGHT_CORONA)
		{
			for (auto it = g_lights_flares.begin(); it != g_lights_flares.end(); it++)
			{
				if (it->second.isRemix)
				{
					if( it->second.handle )
						remixInterface.DestroyLight(it->second.handle);
				}
			}
			g_lights_flares.clear();
		}
		if ( light_types & LIGHT_FLASHLIGHT )
		{
			for ( int i = 0; i < NUM_FLASHLIGHT_HND; i++ )
			{
				if ( g_flashlight_handle[i] )
				{
					remixInterface.DestroyLight( g_flashlight_handle[i] );
					g_flashlight_handle[i] = 0;
				}
			}
		}
	}
}

void qdx_lights_draw()
{	
	//draw dynamic lights, handle both DX9 case or remix
	light_data_t* l = &g_lights_dynamic[0];
	iterdl_t keep_searching = g_lights_dynamic_bitmap;
	iterdl_t bit = 1;
	for (int i = 0; keep_searching && i < LIGHTS_DYNAMIC_SZ; i++, l++, bit <<= 1)
	{
		if ( g_lights_dynamic_bitmap & bit )
		{
			if ( l->updatedThisFrame )
			{
				l->updatedThisFrame--;
				if ( l->isRemix )
				{
					if( l->handle )
						remixInterface.DrawLightInstance( l->handle );
				}
			}
			else
			{
				g_lights_dynamic_bitmap &= ~bit;
				if ( l->isRemix )
				{
					if( l->handle )
						remixInterface.DestroyLight( l->handle );
				}
			}
			keep_searching &= ~bit;
		}
	}

	//draw corona lights and flashlight
	for ( auto it = g_lights_flares.begin(); it != g_lights_flares.end(); it++ )
	{
		remixInterface.DrawLightInstance( it->second.handle );
	}
	if ( rmx_flashlight )
	{
		for ( int i = 0; i < NUM_FLASHLIGHT_HND; i++ )
		{
			if ( g_flashlight_handle[i] )
			{
				remixInterface.DrawLightInstance( g_flashlight_handle[i] );
			}
		}
	}
}

#define DYNAMIC_LIGHTS_USE_DX9 0

int qdx_light_add(int light_type, int ord, const float *position, const float *direction, const float *color, float radius)
{
	int ret = 0;
	remixapi_ErrorCode rercd;
	uint64_t hash = 0;
	uint64_t hashpos = 0;
	uint64_t hashcolor = 0;
	uint64_t hashord = 0;

	//remixOnline = false;

	hashpos = fnv_32a_buf(position, 3*sizeof(float), 0x55FF);
	hashcolor = fnv_32a_buf(color, 3*sizeof(float), 0x55FF);
	hashord = fnv_32a_buf(&ord, sizeof(ord), 0x55FF);

	if (light_type == LIGHT_DYNAMIC)
	{
#if DYNAMIC_LIGHTS_USE_DX9
		hash = hashord;
#else
		hash = hashpos;
#endif
	}
	if (light_type == LIGHT_CORONA)
	{
		hash = hashpos;
		auto itm = g_lights_flares.find(hash);
		if (itm != g_lights_flares.end())
		{
			//flare already exists
			light_override_t* ovr = qdx_light_find_override( hash );
			if ( ovr == NULL || ovr->updated == 0 )
			{
				return 0;
			}
			//clear flag and readd flare with updated overrides
			ovr->updated = 0;
		}
	}
	if ( light_type == LIGHT_FLASHLIGHT )
	{
		//stuff for imgui
		//bool changed = false;
		//if( memcmp(FLASHLIGHT_POSITION_CACHE, position, 3*sizeof(float)) || memcmp(FLASHLIGHT_DIRECTION_CACHE, direction, 3*sizeof(float)) )
		//	changed = true;
		FLASHLIGHT_POSITION_CACHE[0] = position[0];
		FLASHLIGHT_POSITION_CACHE[1] = position[1];
		FLASHLIGHT_POSITION_CACHE[2] = position[2];
		FLASHLIGHT_DIRECTION_CACHE[0] = direction[0];
		FLASHLIGHT_DIRECTION_CACHE[1] = direction[1];
		FLASHLIGHT_DIRECTION_CACHE[2] = direction[2];

		if ( !remixOnline || !rmx_flashlight )
		{
			return 0;
		}

		hash = FLASHLIGHT_HASH;

		if ( 0 && g_flashlight_handle ) //destroying a light creates artifacts; light props get updated regardless
		{
			qdx_lights_clear( LIGHT_FLASHLIGHT );
		}

		for ( int i = 0; i < NUM_FLASHLIGHT_HND; i++ )
		{

			remixapi_LightInfoSphereEXT light_sphere;
			ZeroMemory( &light_sphere, sizeof( light_sphere ) );

			D3DXVECTOR3 tpos, tdir;
			D3DXMATRIX invview, viewrot;
			const D3DXVECTOR3 updir( 0, 0, 1 );
			const D3DXVECTOR3 zero( 0, 0, 1 );
			D3DXMatrixLookAtRH( &viewrot, &zero, (D3DXVECTOR3*)FLASHLIGHT_DIRECTION_CACHE, &updir );
			//D3DGlobal_GetCamera( &viewrot );
			viewrot._41 = 0.0;
			viewrot._42 = 0.0;
			viewrot._43 = 0.0;
			D3DXMatrixInverse( &invview, NULL, (D3DXMATRIX*)&viewrot );
			D3DXVec3TransformCoord( &tpos, (D3DXVECTOR3*)FLASHLIGHT_POSITION_OFFSET, &invview );
			D3DXVec3TransformNormal( &tdir, (D3DXVECTOR3*)FLASHLIGHT_DIRECTION_OFFSET, &invview );

			light_sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
			light_sphere.position.x = tpos.x + position[0];
			light_sphere.position.y = tpos.y + position[1];
			light_sphere.position.z = tpos.z + position[2];
			light_sphere.radius = 1.0f;
			light_sphere.volumetricRadianceScale = FLASHLIGHT_VOLUMETRIC[i];

			light_sphere.shaping_hasvalue = 1;
			{
				tdir.x += direction[0];
				tdir.y += direction[1];
				tdir.z += direction[2];
				D3DXVec3Normalize( (D3DXVECTOR3*)&light_sphere.shaping_value.direction, &tdir );
				light_sphere.shaping_value.coneAngleDegrees = FLASHLIGHT_CONE_ANGLES[i];
				light_sphere.shaping_value.coneSoftness = FLASHLIGHT_CONE_SOFTNESS[i];
				light_sphere.shaping_value.focusExponent = 0.0f;
			}

			uint64_t fhash = FLASHLIGHT_HASH + i + 1;

			remixapi_LightInfo lightinfo;
			ZeroMemory( &lightinfo, sizeof( lightinfo ) );

			lightinfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
			lightinfo.pNext = &light_sphere;
			lightinfo.hash = fhash;
			qdx_light_color_to_radiance_flashlight( &lightinfo.radiance, light_type, i, FLASHLIGHT_COLORS[i] );

			rercd = remixInterface.CreateLight( &lightinfo, &g_flashlight_handle[i] );
			if ( rercd != REMIXAPI_ERROR_CODE_SUCCESS )
			{
				//ri.Printf( PRINT_ERROR, "RMX failed to create flashlight %d\n", rercd );
				return 0;
			}
			ret = 1;
		}

		return ret;
	}

	if (remixOnline)
	{
		light_override_t* ovr = qdx_light_find_override( hash );

		remixapi_LightInfoSphereEXT light_sphere;
		ZeroMemory( &light_sphere, sizeof(light_sphere) );

		light_sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
		//light_sphere.pNext = &obj_pick;
		if ( ovr == NULL )
		{
			light_sphere.position.x = position[0];
			light_sphere.position.y = position[1];
			light_sphere.position.z = position[2];
			light_sphere.radius = qdx_light_radius( light_type, radius );
			light_sphere.shaping_hasvalue = 0;
			light_sphere.volumetricRadianceScale = 0.0f;
		}
		else
		{
			light_sphere.position.x = position[0] + ovr->position_offset[0];
			light_sphere.position.y = position[1] + ovr->position_offset[1];
			light_sphere.position.z = position[2] + ovr->position_offset[2];
			light_sphere.radius = qdx_light_radius_override( light_type, radius, ovr );
			light_sphere.shaping_hasvalue = 0;
			light_sphere.volumetricRadianceScale = ovr->volumetric_scale;
		}

		remixapi_LightInfo lightinfo;
		ZeroMemory(&lightinfo, sizeof(lightinfo));

		lightinfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
		lightinfo.pNext = &light_sphere;
		lightinfo.hash = hash;
		if ( ovr == NULL )
		{
			qdx_light_color_to_radiance( &lightinfo.radiance, light_type, color, radius );
		}
		else
		{
			qdx_light_color_to_radiance_override( &lightinfo.radiance, light_type, radius, ovr );
		}

		light_data_t *light_store = NULL;
		switch (light_type)
		{
		case LIGHT_DYNAMIC:
			light_store = qdx_dynamiclight_get_slot(hash);
			break;
		case LIGHT_CORONA:
			light_store = &g_lights_flares[hash];
			break;
		default:
			qassert(FALSE && "Unknown light type");
		}

		if ( light_store )
		{
			light_store->isRemix = true;
			light_store->handle = 0;
			rercd = remixInterface.CreateLight( &lightinfo, &light_store->handle );
			if ( rercd != REMIXAPI_ERROR_CODE_SUCCESS )
			{
				//ri.Printf( PRINT_ERROR, "RMX failed to create light %d\n", rercd );
				return 0;
			}
			light_store->hash = hash;
			light_store->pos[0] = position[0];
			light_store->pos[1] = position[1];
			light_store->pos[2] = position[2];
			light_store->color[0] = color[0];
			light_store->color[1] = color[1];
			light_store->color[2] = color[2];

			ret = 1;
		}
	}

	return ret;
}

#define VectorSubtract( a,b,c )   ( ( c )[0] = ( a )[0] - ( b )[0],( c )[1] = ( a )[1] - ( b )[1],( c )[2] = ( a )[2] - ( b )[2] )

vec_t VectorLength( const vec3_t v ) {
	return sqrt( v[0] * v[0] + v[1] * v[1] + v[2] * v[2] );
}

vec_t VectorLengthSquared( const vec3_t v ) {
	return ( v[0] * v[0] + v[1] * v[1] + v[2] * v[2] );
}

vec_t Distance( const vec3_t p1, const vec3_t p2 ) {
	vec3_t v;

	VectorSubtract( p2, p1, v );
	return VectorLength( v );
}