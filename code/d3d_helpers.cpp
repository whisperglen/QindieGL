
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <Windows.h>
#include <WinUser.h>
#include <time.h>
#include "resource.h"

#include "d3d_helpers.hpp"
#include "d3d_wrapper.hpp"

static key_inputs_t keystate = { 0 };
static key_inputs_t pressed = { 0 };
static bool new_frame = true;

#define IS_PRESSED(X) (((X) & 0x8000) != 0)

#define HANDLE_KEY(KEY, var) \
	if (IS_PRESSED(GetAsyncKeyState(KEY))) \
	{ \
		if ( keystate.var == 0 ) \
		{ \
			pressed.var = 1; \
			keystate.var = 1; \
		} \
	} \
	else \
	{ \
		keystate.var = 0; \
	}

key_inputs_t keypress_get(bool immediate)
{
	//check once every frame, and return that value for all calls
	if ( new_frame )
	{
		new_frame = false;

		bool isShift = false;
		///
		if ( IS_PRESSED( GetAsyncKeyState( VK_SHIFT ) ) )
		{
			isShift = true;
		}
		///
		if ( IS_PRESSED( GetAsyncKeyState( VK_CONTROL ) ) )
		{
			pressed.ctrl = 1;
		}
		/// ALT
		if ( IS_PRESSED( GetAsyncKeyState( VK_MENU ) ) )
		{
			pressed.alt = 1;
		}
		///
		if ( IS_PRESSED( GetAsyncKeyState( VK_UP ) ) )
		{
			if ( keystate.updown == 0 || isShift )
			{
				pressed.updown = 1;
				keystate.updown = 1;
			}
		}
		else if ( IS_PRESSED( GetAsyncKeyState( VK_DOWN ) ) )
		{
			if ( keystate.updown == 0 || isShift )
			{
				pressed.updown = -1;
				keystate.updown = -1;
			}
		}
		else
		{
			keystate.updown = 0;
		}
		///
		if ( IS_PRESSED( GetAsyncKeyState( VK_LEFT ) ) )
		{
			if ( keystate.leftright == 0 || isShift )
			{
				pressed.leftright = -1;
				keystate.leftright = -1;
			}
		}
		else if ( IS_PRESSED( GetAsyncKeyState( VK_RIGHT ) ) )
		{
			if ( keystate.leftright == 0 || isShift )
			{
				pressed.leftright = 1;
				keystate.leftright = 1;
			}
		}
		else
		{
			keystate.leftright = 0;
		}
		/// PAGE UP
		HANDLE_KEY( VK_PRIOR, pgup );
		/// PAGE DOWN
		HANDLE_KEY( VK_NEXT, pgdwn );
		///
		HANDLE_KEY( 'X', x );
		///
		HANDLE_KEY( 'Y', y );
		///
		HANDLE_KEY( 'Z', z );
		///
		HANDLE_KEY( 'I', i );
		///
		HANDLE_KEY( 'O', o );
		///
		HANDLE_KEY( 'U', u );
		///
		HANDLE_KEY( 'P', p );
		///
		HANDLE_KEY( 'C', c );
		///
		HANDLE_KEY( VK_F8, imgui );
	}

	return (immediate ? keystate : pressed);
}

void keypress_frame_ended()
{
	pressed.all = 0;
	new_frame = true;
}

int ispasswd(int val)
{
	return isalnum(val) || ispunct(val);
}

void random_init()
{
	static int initialised = 0;
	if (initialised == 0)
	{
		unsigned int seed = (unsigned int)time(NULL);
		srand(seed);
		initialised = 1;
		logPrintf( "Rand seed: %d\n", seed);
	}
}

void random_bytes(byte* out, int size)
{
	random_init();

	int i;
	for (i = 0; i < size; i++)
	{
		out[i] = rand() % 0x100;
	}
}

void random_text(byte* out, int size)
{
	random_init();

	int i;
	for (i = 0; i < size; )
	{
		int val = rand();
		byte* itr = (byte*)&val;
		for (int j = 0; j < sizeof(val); j++, itr++)
		{
			if (ispasswd(*itr))
			{
				out[i] = *itr;
				i++;
				break;
			}
		}
	}
}

bool resource_load_shader(uint32_t id, LPVOID* pData, UINT* pBytes, HMODULE module)
{
	bool res = false;
	HRSRC hrs = FindResourceA(module, MAKEINTRESOURCEA(id), MAKEINTRESOURCEA(RES_TYPE_SHADER_FILE));
	if (hrs != NULL)
	{
		HGLOBAL hg = LoadResource(module, hrs);
		if (hg != NULL)
		{
			LPVOID data = LockResource(hg);
			DWORD sz = SizeofResource(module, hrs);
			if (data != NULL && sz != 0)
			{
				*pData = data;
				*pBytes = UINT(sz);
				res = true;
			}
		}
	}
	return res;
}

typedef struct {
	DWORD           dwDDSID;
	DWORD           dwSize;
	DWORD           dwFlags;
	DWORD           dwHeight;
	DWORD           dwWidth;
	DWORD           dwPitchOrLinearSize;
	DWORD           dwDepth;
	DWORD           dwMipMapCount;
} dds_header_short_t;
const uint32_t DDS_OFFSET = 0x7C + 4;

bool resource_load_pic( uint32_t id, LPVOID* pData, UINT* pSize, UINT* pWidth, UINT* pHeight, HMODULE module )
{
	bool res = false;
	HRSRC hrs = FindResourceA(module, MAKEINTRESOURCEA(id), MAKEINTRESOURCEA(RES_TYPE_PIC_DDS));
	if (hrs != NULL)
	{
		HGLOBAL hg = LoadResource(module, hrs);
		if (hg != NULL)
		{
			LPVOID data = LockResource(hg);
			DWORD sz = SizeofResource(module, hrs);
			if (data != NULL && sz > DDS_OFFSET)
			{
				dds_header_short_t* ddsh = (dds_header_short_t*)data;
				*pData = (uint8_t*)data + DDS_OFFSET;
				*pSize = UINT(sz) - DDS_OFFSET;
				*pWidth = ddsh->dwWidth;
				*pHeight = ddsh->dwHeight;
				res = true;
			}
		}
	}
	return res;
}