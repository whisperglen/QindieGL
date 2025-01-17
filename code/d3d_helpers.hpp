
#ifndef QINDIEGL_D3D_HELPERS_H
#define QINDIEGL_D3D_HELPERS_H

typedef union inputs
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
	};
	unsigned int all;
} key_inputs_t;

key_inputs_t keypress_get();

#endif
