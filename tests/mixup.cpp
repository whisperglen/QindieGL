
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "tests.h"

#define id386 1

static int ptr_is_on_stack(void* ptr)
{
	intptr_t stackptr;
	int ret = 0;
#if id386
	_asm {	mov stackptr, esp }
#else
	stackptr = (intptr_t)&ret;
#endif

	if (((intptr_t)ptr > stackptr) && ((intptr_t)ptr < (stackptr + 0x400))) //within 1K of stack
	{
		ret = 1;
	}

	return ret;
}

static int glob[16];

void do_mixup_tests()
{
	float mat[16];

	assert(ptr_is_on_stack(mat));

	assert(ptr_is_on_stack(glob) == 0);
}