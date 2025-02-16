/***************************************************************************
* Copyright (C) 2011-2016, Crystice Softworks.
* 
* This file is part of QindieGL source code.
* Please note that QindieGL is not driver, it's emulator.
* 
* QindieGL source code is free software; you can redistribute it and/or 
* modify it under the terms of the GNU General Public License as 
* published by the Free Software Foundation; either version 2 of 
* the License, or (at your option) any later version.
* 
* QindieGL source code is distributed in the hope that it will be 
* useful, but WITHOUT ANY WARRANTY; without even the implied 
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
* See the GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software 
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
***************************************************************************/
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_utils.hpp"

#include <string>
#include <tchar.h>

//==================================================================================
// Wrapper Log
//----------------------------------------------------------------------------------
// Log all internal errors into text file.
// This will help to debug the wrapper and monitor unimplemented functions.
//==================================================================================
static FILE *g_fpLog = nullptr;
static const char s_szLogFileName[] = WRAPPER_GL_SHORT_NAME_STRING ".log";
static char *log_string = nullptr;
static const size_t c_LogStringSize = 8192; //8 kbytes

static void logInit()
{
	if (g_fpLog)
		return;

	if (!log_string)
	{
		log_string = reinterpret_cast<char*>( UTIL_Alloc(c_LogStringSize) );
		assert(log_string != NULL);
	}
	log_string[c_LogStringSize -1] = 0;

	if ( fopen_s( &g_fpLog, s_szLogFileName, "w" ) )
		return;

	char timeBuf[64];
	time_t t;
	memset(&t, 0, sizeof(t));
	time(&t);
	memset(timeBuf, 0, sizeof(timeBuf));
	ctime_s(timeBuf,sizeof(timeBuf),&t);

	fprintf(g_fpLog,"=======================================================================\n");
	fprintf(g_fpLog," " WRAPPER_GL_SHORT_NAME_STRING " wrapper initialized at %s",timeBuf);
	fprintf(g_fpLog,"=======================================================================\n");

	fprintf(g_fpLog, "\n");
	fflush(g_fpLog);
}

static void logShutdown()
{
	if (g_fpLog) {
		time_t t;
		memset(&t, 0, sizeof(t));
		time(&t);
		char timeBuf[64];
		memset(timeBuf, 0, sizeof(timeBuf));
		ctime_s(timeBuf,sizeof(timeBuf),&t);

		fprintf(g_fpLog,"=======================================================================\n");
		fprintf(g_fpLog," " WRAPPER_GL_SHORT_NAME_STRING " wrapper shutdown at %s",timeBuf);
		fprintf(g_fpLog,"=======================================================================\n");

		fclose(g_fpLog);
		g_fpLog = NULL;
	}

	if (log_string)
	{
		UTIL_Free(log_string);
		log_string = NULL;
	}
}

void logPrintf( const char *fmt, ... )
{
	if (!g_fpLog)
		return;

	va_list argptr;
	va_start(argptr,fmt);
	_vsnprintf_s(log_string,c_LogStringSize,c_LogStringSize-2,fmt,argptr);
	va_end(argptr);

	fprintf(g_fpLog, "%s", log_string);
	fflush(g_fpLog);
}

#define PATH_SZ 1024
//=========================================
// DLL Entry Point
//-----------------------------------------
// Init and shutdown global DLL data
//=========================================

BOOL APIENTRY DllMain( HMODULE hModule, DWORD ul_reason_for_call, LPVOID )
{
	std::string game_cfg(GLOBAL_GAMENAME);

    switch ( ul_reason_for_call )
	{
		case DLL_PROCESS_ATTACH:
			DisableThreadLibraryCalls(hModule);
			logInit();
			//logPrintf("DllMain( DLL_PROCESS_ATTACH )\n");
			//detect executable name
			{
				static TCHAR mfname[PATH_SZ];
				DWORD ercd = GetModuleFileName(NULL, mfname, PATH_SZ);
				if (ercd > 0)
				{
					TCHAR* name = _tcsrchr(mfname, _T('\\'));
					if (name) {
						name++;
						TCHAR* tmp = _tcsrchr(mfname, _T('.'));
						if (tmp) {
							tmp[0] = 0; //terminate string before .exe
						}
						game_cfg.assign("game.");
						game_cfg.append(name);
					}
				}
			}
			D3DGlobal_StoreGameName(game_cfg.c_str());
			D3DGlobal_Init( true );
			D3DGlobal.hModule = hModule;
			break;
		case DLL_PROCESS_DETACH:
			//logPrintf("DllMain( DLL_PROCESS_DETACH )\n");
			D3DGlobal_Cleanup( true );
			logShutdown();
			break;
		default:
			break;
    }
    return TRUE;
}
