
#include "hooking.h"
#include <Windows.h>
#include <psapi.h>
#include "detours.h"

#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "surface_sorting.h"
#include "h2_refgl.h"

static BOOL h2_initialised = FALSE;

void* fp_exit = exit; //ExitProcess;

DECLSPEC_NORETURN VOID WINAPI hk_ExitProcess( UINT code )
{
	logPrintf("Calling TerminateProcess\n");
	logShutdown();
	TerminateProcess(GetCurrentProcess(), code);
}
DECLSPEC_NORETURN void __cdecl hk_exit(int code)
{
	logPrintf("Calling TerminateProcess\n");
	logShutdown();
	TerminateProcess(GetCurrentProcess(), code);
}

int hook_dll_on_load_check()
{
	if (DetourIsHelperProcess()) {
		return TRUE;
	}
	return FALSE;
}

void hook_on_process_attach()
{
	DetourRestoreAfterWith();
}

static MODULEINFO exedata;

int hook_check_address_within_range(void* adr)
{
	if ((adr >= exedata.lpBaseOfDll) && (adr < ((LPBYTE)exedata.lpBaseOfDll + exedata.SizeOfImage)))
	{
		return TRUE;
	}

	return FALSE;
}

int hook_unprotect( void* ptr, int size, unsigned long *restore )
{
	DWORD error;
	DWORD dwOld = 0;
	if (!VirtualProtect(ptr, size, PAGE_EXECUTE_READWRITE, &dwOld)) {
		error = GetLastError();
		logPrintf("VirtualProtect failed RW for %p with 0x%x\n", ptr, error);
		return FALSE;
	}
	if ( restore )
		*restore = dwOld;

	return TRUE;
}

int hook_protect( void* ptr, int size, unsigned long restore )
{
	DWORD error;
	DWORD dwOld = 0;
	if (!VirtualProtect(ptr, size, restore, &dwOld)) {
		error = GetLastError();
		logPrintf("VirtualProtect failed op:%x for %p with 0x%x\n", restore, ptr, error);
		return FALSE;
	}
	return TRUE;
}

const void* hook_find_pattern( unsigned char* pat, int patsz )
{
	const void* ret = 0;
	const void* haystack = exedata.lpBaseOfDll;
	int haystacksz = exedata.SizeOfImage;
	const byte* ptr;
	
	ptr = (const byte*)memchr( haystack, pat[0], haystacksz );
	while ( ptr )
	{
		haystacksz -= (ptrdiff_t)ptr - (ptrdiff_t)haystack;
		if ( haystacksz < patsz )
		{
			//nothing found
			break;
		}

		int i;
		for ( i = 1; i < patsz; i++ )
		{
			if ( ptr[i] != pat[i] )
			{
				break;
			}
		}
		if ( i == patsz )
		{
			ret = ptr;
			break;
		}
		haystack = (void*)((ptrdiff_t)ptr + i);
		haystacksz -= i;
		ptr = (const byte*)memchr( haystack, pat[0], haystacksz );
	}

	return ret;
}

void* hook_loadptr( const void* addr )
{
	void* ret = 0;
	memcpy( &ret, addr, sizeof( ret ) );
	return ret;
}

void* hook_offset_to_addr( void* offset )
{
	void* ret = 0;
	if ( exedata.SizeOfImage > (DWORD)offset )
	{
		ret = (uint8_t*)exedata.lpBaseOfDll + (intptr_t)offset;
	}

	return ret;
}

void hook_do_init(const char *exename, const char* dllname, const char *gamename)
{
	LONG error;

    logPrintf("Hookinit: gamecfg %s\n", gamename);

	ZeroMemory(&exedata, sizeof(exedata));
	if (GetModuleInformation(GetCurrentProcess(), GetModuleHandle(NULL), &exedata, sizeof(exedata)))
	{
		logPrintf("Hookinit: moduleinfo base:%p size:%d ep:%p \n", exedata.lpBaseOfDll, exedata.SizeOfImage, exedata.EntryPoint);
	}
	else
	{
		logPrintf("Hookinit: cannot get executable info %s\n", DXGetErrorString(GetLastError()));
	}

	if ( 0 != D3DGlobal_GetRegistryValue("TerminateProcess", "Settings", 0 ) )
	{
		bool abort = false;
		do {
			DetourTransactionBegin();
			DetourUpdateThread( GetCurrentThread() );
			fp_exit = DetourFindFunction( exename, "exit" );
			if ( fp_exit )
			{
				error = DetourAttach( &(PVOID&)fp_exit, hk_exit );
				if ( error != NO_ERROR ) logPrintf( "Hookinit: failed to intercept %s: %d \n", "exit", error );
				break;
			}
			fp_exit = DetourFindFunction( exename, "ExitProcess" );
			if ( !fp_exit )
			{
				fp_exit = ExitProcess;
			}
			error = DetourAttach( &(PVOID&)fp_exit, hk_ExitProcess );
			if ( error != NO_ERROR ) logPrintf( "Hookinit: failed to intercept %s: %d \n", "ExitProcess", error );
		} while ( 0 );

		if ( abort )
		{
			error = DetourTransactionAbort();
			if ( error != NO_ERROR ) logPrintf( "Hookinit: DetourTransactionAbort failed: %d \n", error );
		}
		else
		{
			error = DetourTransactionCommit();
			if ( error != NO_ERROR ) logPrintf( "Hookinit: DetourTransactionCommit failed: %d \n", error );

			//pin our handle so that we do not get unloaded on FreeLibrary
			HMODULE hm = 0;
			GetModuleHandleEx( GET_MODULE_HANDLE_EX_FLAG_PIN, dllname, &hm );
		}
	}

    hook_surface_sorting_do_init();

	if ( D3DGlobal_ReadGameConfPtr( "patch_h2_refgl" ) )
	{
		h2_initialised = TRUE;
		h2_refgl_init();
	}
}

void hook_do_deinit()
{
	//LONG error;

	logPrintf("Hookdeinit\n");

	//DetourTransactionBegin();
	//DetourUpdateThread(GetCurrentThread());
	//DetourDetach(&(PVOID&)fp_exit, hk_exit);
	//error = DetourTransactionCommit();
	//if(error != NO_ERROR) logPrintf("hook_do_deinit: DetourTransactionCommit failed: %d \n", error);

	hook_surface_sorting_do_deinit();

	if ( h2_initialised )
	{
		h2_refgl_deinit();
	}
}

void hook_frame_ended()
{
	hook_surface_sorting_frame_ended();
	if ( h2_initialised )
		h2_refgl_frame_ended();
}