
#ifndef __SURFACE_SORTING_H
#define __SURFACE_SORTING_H

#include "hooking.h"

#ifdef __cplusplus
extern "C" {
#endif

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
	surfaceType_t* surface;       // any of surface*_t
} drawSurf_t;

typedef float vec_t;
typedef vec_t vec3_t[3];
typedef enum { qfalse, qtrue }    qboolean;
typedef uint8_t byte;
typedef uint32_t uint;
typedef uint16_t ushort;

#define VectorCopy(a,b)			(b[0]=a[0],b[1]=a[1],b[2]=a[2])
#define DotProduct( x,y )         ( ( x )[0] * ( y )[0] + ( x )[1] * ( y )[1] + ( x )[2] * ( y )[2] )

typedef struct cvar_s {
	char* name;
	char* string;
	char* resetString;       // cvar_restart will reset to this value
	char* latchedString;     // for CVAR_LATCH vars
	int flags;
	qboolean modified;              // set each time the cvar is changed
	int modificationCount;          // incremented each time the cvar is changed
	float value;                    // atof( string )
	int integer;                    // atoi( string )
	struct cvar_s* next;
	struct cvar_s* hashNext;
} cvar_t;

typedef cvar_t* (*cvarGet)(const char* name, const char* value, int flags);
typedef void (*cvarSet)(const char*, const char*);

void hook_surface_sorting_do_init();
void hook_surface_sorting_do_deinit();

void hook_surface_sorting_frame_ended();


#ifdef __cplusplus
}
#endif

#endif