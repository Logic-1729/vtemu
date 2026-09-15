#ifndef __MVTERM_H__
#define __MVTERM_H__

#include <vterm.h>

#include "ringbuf.h"

#define MVTERM_ESCAPE_MAXLEN 128

typedef struct {
    char buf[MVTERM_ESCAPE_MAXLEN];
    size_t buflen;
    int state;
    char* clipboard;
    size_t cliplen, clipsz;
} MVTERM_STATE;

void mvterm_state_start_copy (MVTERM_STATE* state);
void mvterm_state_copy (MVTERM_STATE* state, const char* buf, size_t len);

#define MVTERM_STATE_ISCOPYING 1
#define MVTERM_STATE_ISPASTING 2
#define MVTERM_STATE_ISVTCOPYING 4
#define MVTERM_STATE_ISVTUSING 8

int mvterm_escape_translate (MVTERM_STATE* state, char c, VTerm* vt);

#define MVTERM_COMM_RESIZE 1
#define MVTERM_COMM_PAUSE 2
#define MVTERM_COMM_END 3

extern RINGBUF_READ_CALLBACK RINGBUF_READ_VTERM;
extern RINGBUF_WRITE_CALLBACK RINGBUF_WRITE_VTERM;

#endif
