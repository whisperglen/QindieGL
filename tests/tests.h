#pragma once

#ifdef __cplusplus
extern "C" {
#endif

    typedef unsigned char uc8_t;
    typedef char sc8_t;

    void xassert_str(int success, const char* expression, const char* function, unsigned line, const char* file);
    void xassert_int(int success, int printval, const char* function, unsigned line, const char* file);

#ifdef assert
#undef assert
#endif

#define assert(expression) (void)(                                                             \
            (xassert_str((!!(expression)), _CRT_STRINGIZE(#expression), (__func__), (unsigned)(__LINE__), (__FILE__)), 0) \
        )

#define assertloop(expression, iter) (void)(                                                             \
            (xassert_int((!!(expression)), (iter), (__func__), (unsigned)(__LINE__), (__FILE__)), 0) \
        )

    void random_init();
    void random_text(uc8_t* out, int size);
    void random_bytes(uc8_t* out, int size);

#ifdef __cplusplus
}
#endif