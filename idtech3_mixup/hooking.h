
#ifndef __HOOKING_C
#define __HOOKING_C

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int hook_dll_on_load_check();

void hook_on_process_attach();

void hook_do_init(const char *exename, const char* dllname, const char *gamename);
void hook_do_deinit();
void hook_frame_ended();

int hook_check_address_within_range(void* adr);
int hook_unprotect( void* ptr, int size, unsigned long *restore );
int hook_protect( void* ptr, int size, unsigned long restore );
const void* hook_find_pattern( unsigned char* pat, int patsz );
void* hook_loadptr( const void* addr );
void* hook_offset_to_addr( void* offset );

#define HOOK_ONLINE_NOTICE() { \
  static BOOL printed_online = FALSE; \
  if( !printed_online ) \
  { \
	printed_online = TRUE; \
    logPrintf("ONLINE: %s\n", __FUNCSIG__); \
  } \
}


#ifdef __cplusplus
}
#endif

#endif
