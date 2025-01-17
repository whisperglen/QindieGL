
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <Windows.h>
#include <WinUser.h>

#include "d3d_helpers.hpp"

key_inputs_t prevstate = { 0 };

#define IS_PRESSED(X) (((X) & 0x8000) != 0)

key_inputs_t keypress_get()
{
	key_inputs_t ret = { 0 };

	///
	if (IS_PRESSED(GetAsyncKeyState(VK_CONTROL)))
	{
		ret.ctrl = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_MENU)))
	{
		ret.alt = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_UP)))
	{
		prevstate.updown = 1;
	}
	else if (IS_PRESSED(GetAsyncKeyState(VK_DOWN)))
	{
		prevstate.updown = -1;
	}
	else if (prevstate.updown != 0)
	{
		ret.updown = prevstate.updown;
		prevstate.updown = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_LEFT)))
	{
		prevstate.leftright = -1;
	}
	else if (IS_PRESSED(GetAsyncKeyState(VK_RIGHT)))
	{
		prevstate.leftright = 1;
	}
	else if (prevstate.leftright != 0)
	{
		ret.leftright = prevstate.leftright;
		prevstate.leftright = 0;
	}
	/// PAGE UP
	if (IS_PRESSED(GetAsyncKeyState(VK_PRIOR)))
	{
		prevstate.pgup = 1;
	}
	else if (prevstate.pgup == 1)
	{
		ret.pgup = 1;
		prevstate.pgup = 0;
	}
	/// PAGE DOWN
	if (IS_PRESSED(GetAsyncKeyState(VK_NEXT)))
	{
		prevstate.pgdwn = 1;
	}
	else if (prevstate.pgdwn == 1)
	{
		ret.pgdwn = 1;
		prevstate.pgdwn = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('X')))
	{
		prevstate.x = 1;
	}
	else if (prevstate.x == 1)
	{
		ret.x = 1;
		prevstate.x = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('Y')))
	{
		prevstate.y = 1;
	}
	else if (prevstate.y == 1)
	{
		ret.y = 1;
		prevstate.y = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('Z')))
	{
		prevstate.z = 1;
	}
	else if (prevstate.z == 1)
	{
		ret.z = 1;
		prevstate.z = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('I')))
	{
		prevstate.i = 1;
	}
	else if (prevstate.i == 1)
	{
		ret.i = 1;
		prevstate.i = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('O')))
	{
		prevstate.o = 1;
	}
	else if (prevstate.o == 1)
	{
		ret.o = 1;
		prevstate.o = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('U')))
	{
		prevstate.u = 1;
	}
	else if (prevstate.u == 1)
	{
		ret.u = 1;
		prevstate.u = 0;
	}

	return ret;
}
