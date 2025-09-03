
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

key_inputs_t keypress_get(boolean immediate)
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
				keystate.updown = 1;
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
				keystate.leftright = 1;
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