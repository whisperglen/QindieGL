
#ifndef __RMX_GEN_H__
#define __RMX_GEN_H__

#include "bridge_remix_api.h"
#include "rmx_light.h"

extern remixapi_Interface remixInterface;
extern BOOL remixOnline;

void rmx_init_device( void *hwnd, void *device );
void rmx_deinit_device();

void qdx_begin_loading_map( const char* mapname );
const char* qdx_get_active_map();
void qdx_save_iniconf();
void qdx_storemapconfint(const char* base, const char* valname, int value, bool inGlobal);
void qdx_storemapconfflt( const char* base, const char* valname, float value, bool inGlobal = false );
void qdx_storemapconfstr(const char* base, const char* valname, const char* value, bool inGlobal);
int qdx_readmapconfint(const char* base, const char* valname);
float qdx_readmapconfflt( const char* base, const char* valname, float default_val );
int qdx_readmapconfstr(const char* base, const char* valname, char* out, int outsz);
void rmx_setplayerpos( const float* origin, const float* direction );
void rmx_frame_end();

/*****************
** game interop **
******************/
#define PRINT_ALL 0
#define PRINT_WARNING 2
#define PRINT_ERROR 3
void rmx_gamevar_set( const char *name, const char *val );
int rmx_gamevar_get( const char *name );
void rmx_exec_cmd( const char *cmd );
void rmx_console_printf( int printLevel, const char *fmt, ... );
int rmx_milliseconds( void );
void rmx_deactivate_mouse();
typedef enum gameops_e
{
	OP_GETVAR,
	OP_SETVAR,
	OP_EXECMD,
	OP_CONPRINT,
	OP_DEACTMOUSE,
	OP_GETNORMALSTHRESHVAL
} gameops_t;
typedef union gameparam_u
{
	int32_t intval;
	float fltval;
	const char* strval;
	const int* pintval;
	const float* pfltval;
} gameparam_t;
typedef union gameparamret_u
{
	int32_t intval;
	float fltval;
	char* strval;
	int* pintval;
	float* pfltval;
} gameparamret_t;
extern "C" {
	typedef gameparamret_t(__cdecl* game_api)(gameops_t op, gameparam_t p0, gameparam_t p1, gameparam_t p2);
}
void rmx_set_game_api( game_api fn );
void rmx_flashlight_enable( int val = -1 );
void rmx_distant_light_radiance(float r, float g, float b, bool enabled);

/**************************
** api for imgui interop **
**************************/
void qdx_imgui_init( void *hwnd, void *device );
void qdx_imgui_deinit();
void qdx_imgui_draw();

float* qdx_4imgui_radiance_dynamic_1f();
float* qdx_4imgui_radiance_dynamic_scale_1f();
float* qdx_4imgui_radiance_coronas_1f();
float* qdx_4imgui_radius_dynamic_1f();
float* qdx_4imgui_radius_dynamic_scale_1f();
float* qdx_4imgui_radius_coronas_1f();
float* qdx_4imgui_cylinder_axis_dynamic_3f();
float* qdx_4imgui_cylinder_axis_coronas_3f();
float* qdx_4imgui_cylinder_len_dynamic_1f();
float* qdx_4imgui_cylinder_len_coronas_1f();
float* qdx_4imgui_flashlight_radiance_1f(int idx);
float* qdx_4imgui_flashlight_colors_3f(int idx);
float* qdx_4imgui_flashlight_coneangles_1f(int idx);
float* qdx_4imgui_flashlight_conesoft_1f(int idx);
float* qdx_4imgui_flashlight_volumetric_1f( int idx );
const float* qdx_4imgui_flashlight_position_3f();
const float* qdx_4imgui_flashlight_direction_3f();
float* qdx_4imgui_flashlight_position_off_3f();
float* qdx_4imgui_flashlight_direction_off_3f();

void qdx_light_scan_closest_lights( light_type_e type );
uint64_t qdx_4imgui_light_picking_id( uint32_t idx );
int qdx_4imgui_light_picking_count();
void qdx_4imgui_light_picking_clear();
light_override_t* qdx_4imgui_light_get_override( uint64_t hash, light_type_e type );
void qdx_4imgui_light_clear_override( uint64_t hash );

int* qdx_4imgui_surface_aabb_selection( int *total );
const char *qdx_4imgui_shader_info( int *value );
void qdx_4imgui_surface_aabb_saveselection(const char *hint);

float* rmx_4imgui_getnormalsthresh();

//#define FRUSTUM_IMGUI

#ifdef FRUSTUM_IMGUI
float* qdx_4imgui_frustrumplane(int idx);
float* qdx_4imgui_frustumdist(int idx);
int* qdx_4imgui_frustumtype();
int* qdx_4imgui_frustumoverride();
#endif

/***************
** Other utils *
****************/
#define qassert(expression) do { \
            if((!(expression))) { qdx_assert_failed_str(_CRT_STRINGIZE(#expression), (__func__), (unsigned)(__LINE__), (__FILE__)); } \
        } while(0)

void qdx_assert_failed_str( const char* expression, const char* function, unsigned line, const char* file );

#endif