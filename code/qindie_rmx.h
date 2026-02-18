
#ifndef __QINDIE_RMX_H__
#define __QINDIE_RMX_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

typedef gameparamret_t(__cdecl* game_api)(gameops_t op, gameparam_t p0, gameparam_t p1, gameparam_t p2);

typedef void (*fn_rmx_begin_loading_map)(const char* mapname);
typedef void (*fn_rmx_setplayerpos)(const float* origin, const float* direction);
typedef void (*fn_rmx_set_game_api)(game_api fn);
typedef void (*fn_rmx_flashlight_enable)(int val);

enum light_type_e
{
	LIGHT_NONE = 0,
	LIGHT_DYNAMIC = 1,
	LIGHT_CORONA = 2,
	LIGHT_FLASHLIGHT = 4,
	LIGHT_NEW = 8,
	LIGHT_DISTANT = 16,

	LIGHT_ALL = (LIGHT_DYNAMIC | LIGHT_CORONA | LIGHT_FLASHLIGHT | LIGHT_NEW | LIGHT_DISTANT)
};
typedef int  (*fn_rmx_light_add)(int light_type_e, int ord, const float* position, const float* direction, const float* color, float radius);
typedef void (*fn_rmx_lights_clear)(unsigned int light_types);

extern fn_rmx_begin_loading_map rmx_begin_loading_map;
extern fn_rmx_setplayerpos rmx_setplayerpos;
extern fn_rmx_set_game_api rmx_set_game_api;
extern fn_rmx_flashlight_enable rmx_flashlight_enable;
extern fn_rmx_light_add rmx_light_add;
extern fn_rmx_lights_clear rmx_lights_clear;

void rmx_interface_init(void *opengl32_hnd);

#ifdef RMX_ADD_IMPL
static int rmx_do_nothing_func(void) { return 0; }

#ifndef RMX_GET_PROC_ADDRESS
#include <libloaderapi.h>
#define RMX_GET_PROC_ADDRESS(MOD,NAME) GetProcAddress((MOD),(NAME))
#endif

static void *rmx_assign_func(HMODULE opengl32dll, const char* name)
{
	void* ret = RMX_GET_PROC_ADDRESS(opengl32dll, name);
	if (ret == NULL)
		ret = rmx_do_nothing_func;
	return ret;
}

fn_rmx_begin_loading_map rmx_begin_loading_map;
fn_rmx_setplayerpos rmx_setplayerpos;
fn_rmx_set_game_api rmx_set_game_api;
fn_rmx_flashlight_enable rmx_flashlight_enable;
fn_rmx_light_add rmx_light_add;
fn_rmx_lights_clear rmx_lights_clear;

void rmx_interface_init(void *opengl32_hnd)
{
	HMODULE opengl32_ = (HMODULE)opengl32_hnd;
	rmx_begin_loading_map = (fn_rmx_begin_loading_map)rmx_assign_func(opengl32_, "rmx_begin_loading_map");
	rmx_setplayerpos = (fn_rmx_setplayerpos)rmx_assign_func(opengl32_, "rmx_setplayerpos");
	rmx_set_game_api = (fn_rmx_set_game_api)rmx_assign_func(opengl32_, "rmx_set_game_api");
	rmx_flashlight_enable = (fn_rmx_flashlight_enable)rmx_assign_func(opengl32_, "rmx_flashlight_enable");
	rmx_light_add = (fn_rmx_light_add)rmx_assign_func(opengl32_, "rmx_light_add");
	rmx_lights_clear = (fn_rmx_lights_clear)rmx_assign_func(opengl32_, "rmx_lights_clear");
}
#endif

#ifdef __cplusplus
}
#endif
#endif

