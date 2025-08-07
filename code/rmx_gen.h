
#ifndef __RMX_GEN_H__
#define __RMX_GEN_H__

#include "bridge_remix_api.h"

extern remixapi_Interface remixInterface;
extern BOOL remixOnline;

void qdx_begin_loading_map( const char* mapname );
const char* qdx_get_active_map();


#define qassert(expression) do { \
            if((!(expression))) { qdx_assert_failed_str(_CRT_STRINGIZE(#expression), (__func__), (unsigned)(__LINE__), (__FILE__)); } \
        } while(0)

void qdx_assert_failed_str( const char* expression, const char* function, unsigned line, const char* file );

#endif