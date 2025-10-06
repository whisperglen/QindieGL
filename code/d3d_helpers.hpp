
#ifndef QINDIEGL_D3D_HELPERS_H
#define QINDIEGL_D3D_HELPERS_H

#include <stdint.h>

#pragma warning( push )
#pragma warning( disable : 4201)

typedef union key_inputs_u
{
	struct {
		int updown : 2;
		int leftright : 2;
		unsigned int ctrl : 1;
		unsigned int alt : 1;
		unsigned int pgdwn : 1;
		unsigned int pgup : 1;
		unsigned int x : 1;
		unsigned int y : 1;
		unsigned int z : 1;
		unsigned int i : 1;
		unsigned int o : 1;
		unsigned int u : 1;
		unsigned int p : 1;
		unsigned int c : 1;
		unsigned int imgui : 1;
	};
	unsigned int all;
} key_inputs_t;

#pragma warning( pop )

key_inputs_t keypress_get(bool immediate = false);
void keypress_frame_ended();

void random_bytes( byte* out, int size );
void random_text( byte* out, int size );

bool resource_load_shader( uint32_t id, LPVOID* pData, UINT* pBytes, HMODULE module );
bool resource_load_pic( uint32_t id, LPVOID* pData, UINT* pSize, UINT* pWidth, UINT* pHeight, HMODULE module );

#endif
