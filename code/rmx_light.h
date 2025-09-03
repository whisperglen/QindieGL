
#ifndef __RMX_LIGHT_H__
#define __RMX_LIGHT_H__

#include <stdint.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

enum light_type_e
{
	LIGHT_NONE = 0,
	LIGHT_DYNAMIC = 1,
	LIGHT_CORONA = 2,
	LIGHT_FLASHLIGHT = 4,
	LIGHT_NEW = 8,

	LIGHT_ALL = (LIGHT_DYNAMIC|LIGHT_CORONA|LIGHT_FLASHLIGHT|LIGHT_NEW)
};

typedef union uint64_comp_u
{
	uint64_t ll;
	uint32_t u32[2];
} uint64_comp_t;

typedef struct light_override_s
{
	uint64_t hash;
	float position_offset[3];
	float color[3];
	float radiance_base;
	float radiance_scale;
	float radius_base;
	float radius_scale;
	float volumetric_scale;
	BOOL updated;
	light_type_e type;
} light_override_t;


#define NUM_FLASHLIGHT_HND 3

int  qdx_light_add(int light_type_e, int ord, const float *position, const float *direction, const float *color, float radius);
void qdx_lights_clear(unsigned int light_types);
void qdx_lights_draw();
void qdx_lights_dynamic_linger( int val );

void qdx_lights_load( void *ini, const char *mapname );
void qdx_flashlight_save();
void qdx_radiance_save( bool inGlobal );
void qdx_light_override_save( light_override_t* ovr, bool writeOut = true );
void qdx_light_override_save_all();

#ifdef __cplusplus
} //extern "C"
#endif

#endif //__RMX_LIGHT_H__
