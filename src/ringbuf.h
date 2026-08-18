#ifndef __RINGBUF_H__
#define __RINGBUF_H__

#include <stdio.h>

struct ringbuf {
    void* buf;
    size_t start, end, n;
};

typedef struct ringbuf* RINGBUF;
#define RINGBUF_BLOCK 1024

typedef ssize_t (*RINGBUF_READ_CALLBACK) (void* ctx, void* buf, size_t n);
typedef ssize_t (*RINGBUF_WRITE_CALLBACK) (void* ctx, const void* buf, size_t n);

ssize_t ringbuf_read (RINGBUF rb, void* buf, size_t n);
ssize_t ringbuf_write (RINGBUF rb, const void* buf, size_t n);
ssize_t ringbuf_readd (RINGBUF rb, void* buf, size_t n);
ssize_t ringbuf_writed (RINGBUF rb, const void* buf, size_t n);

extern RINGBUF_READ_CALLBACK RINGBUF_READ;
extern RINGBUF_WRITE_CALLBACK RINGBUF_WRITE;
extern RINGBUF_READ_CALLBACK RINGBUF_READD;
extern RINGBUF_WRITE_CALLBACK RINGBUF_WRITED;

extern RINGBUF_READ_CALLBACK RINGBUF_READ_FD;
extern RINGBUF_WRITE_CALLBACK RINGBUF_WRITE_FD;

RINGBUF ringbuf_alloc (size_t n);
void ringbuf_free (RINGBUF rb);
size_t ringbuf_size (RINGBUF rb);

ssize_t ringbuf_copy_from (RINGBUF rb, void* ctx, RINGBUF_READ_CALLBACK read);
ssize_t ringbuf_copy_to (RINGBUF rb, void* ctx, RINGBUF_WRITE_CALLBACK write);

int ringbuf_resize (RINGBUF rb, size_t n);
ssize_t ringbuf_copyd_from (RINGBUF rb, void* ctx, RINGBUF_READ_CALLBACK read);
ssize_t ringbuf_copyd_to (RINGBUF rb, void* ctx, RINGBUF_WRITE_CALLBACK write);

#endif
