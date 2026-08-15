#ifndef __MVTERM_H__
#define __MVTERM_H__

#include <vterm.h>

#include "ringbuf.h"

void print_vterm (VTerm* vt);

extern const int VTERM_ESCAPE_INIT_STAT;
int vterm_escape_translate (RINGBUF dest, int* status, char c);

extern const VTermScreenCallbacks mvtscb;

extern RINGBUF_READ_CALLBACK RINGBUF_READ_VTERM;
extern RINGBUF_WRITE_CALLBACK RINGBUF_WRITE_VTERM;

#endif
