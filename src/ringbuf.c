#include "ringbuf.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct ringbuf_slice {
    void* buf;
    void* end;
};
static ssize_t ringbuf_write_slice (void* ctx, const void* buf, size_t n) {
    struct ringbuf_slice* ps = (struct ringbuf_slice*)ctx;
    size_t pssz = ps->end - ps->buf;
    size_t ret = n < pssz ? n : pssz;

    memcpy (ps->buf, buf, ret);
    ps->buf += ret;

    return (ssize_t)ret;
}
ssize_t ringbuf_read (RINGBUF rb, void* buf, size_t n) {
    struct ringbuf_slice sl = {buf, buf + n};
    return ringbuf_copy_to (rb, &sl, ringbuf_write_slice);
}
ssize_t ringbuf_readd (RINGBUF rb, void* buf, size_t n) {
    struct ringbuf_slice sl = {buf, buf + n};
    return ringbuf_copyd_to (rb, &sl, ringbuf_write_slice);
}

struct ringbuf_cslice {
    const void* buf;
    const void* end;
};
static ssize_t ringbuf_read_slice (void* ctx, void* buf, size_t n) {
    struct ringbuf_cslice* ps = (struct ringbuf_cslice*)ctx;
    size_t pssz = ps->end - ps->buf;
    size_t ret = n < pssz ? n : pssz;

    memcpy (buf, ps->buf, ret);
    ps->buf += ret;

    return (ssize_t)ret;
}
ssize_t ringbuf_write (RINGBUF rb, const void* buf, size_t n) {
    struct ringbuf_cslice sl = {buf, buf + n};
    return ringbuf_copy_from (rb, &sl, ringbuf_read_slice);
}
ssize_t ringbuf_writed (RINGBUF rb, const void* buf, size_t n) {
    struct ringbuf_cslice sl = {buf, buf + n};
    return ringbuf_copyd_from (rb, &sl, ringbuf_read_slice);
}

static ssize_t ringbuf_ctx_read (void* ctx, void* buf, size_t n) { return ringbuf_read ((RINGBUF)ctx, buf, n); }
static ssize_t ringbuf_ctx_write (void* ctx, const void* buf, size_t n) { return ringbuf_write ((RINGBUF)ctx, buf, n); }
static ssize_t ringbuf_ctx_readd (void* ctx, void* buf, size_t n) { return ringbuf_read ((RINGBUF)ctx, buf, n); }
static ssize_t ringbuf_ctx_writed (void* ctx, const void* buf, size_t n) {
    return ringbuf_write ((RINGBUF)ctx, buf, n);
}

RINGBUF_READ_CALLBACK RINGBUF_READ = ringbuf_ctx_read;
RINGBUF_WRITE_CALLBACK RINGBUF_WRITE = ringbuf_ctx_write;
RINGBUF_READ_CALLBACK RINGBUF_READD = ringbuf_ctx_readd;
RINGBUF_WRITE_CALLBACK RINGBUF_WRITED = ringbuf_ctx_writed;

static ssize_t ringbuf_read_fd (void* ctx, void* buf, size_t n) { return read (*(const int*)ctx, buf, n); }
static ssize_t ringbuf_write_fd (void* ctx, const void* buf, size_t n) { return write (*(const int*)ctx, buf, n); }
RINGBUF_READ_CALLBACK RINGBUF_READ_FD = ringbuf_read_fd;
RINGBUF_WRITE_CALLBACK RINGBUF_WRITE_FD = ringbuf_write_fd;

RINGBUF ringbuf_alloc (size_t n) {
    RINGBUF rb = (RINGBUF)malloc (sizeof (struct ringbuf));
    if (!rb) return NULL;

    rb->buf = malloc (n);
    if (!rb->buf) {
        free (rb);
        return NULL;
    }

    rb->start = rb->n = n;
    rb->end = 0;

    return rb;
}

void ringbuf_free (RINGBUF rb) {
    if (rb) {
        free (rb->buf);
        free (rb);
    }
}

size_t ringbuf_size (RINGBUF rb) {
    return rb->start == rb->n ? 0 : (rb->start >= rb->end ? rb->n - rb->start + rb->end : rb->end - rb->start);
}

ssize_t ringbuf_copy_from (RINGBUF rb, void* ctx, RINGBUF_READ_CALLBACK read) {
    size_t n_end = rb->start >= rb->end ? rb->start - rb->end : rb->n - rb->end;
    if (!n_end) return 0;

    ssize_t n_read = read (ctx, rb->buf + rb->end, n_end);
    if (n_read < 0) return -1;

    if (n_read) {
        if (rb->start == rb->n) rb->start = 0;
        rb->end += n_read;
    }

    if ((size_t)n_read < n_end) return n_read;

    if (rb->end == rb->n) {
        rb->end = 0;

        if (rb->start) {
            ssize_t n_read2 = read (ctx, rb->buf, rb->start);
            if (n_read2 < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) n_read2 = 0;
            if (n_read2 < 0) return -1;

            rb->end += n_read2;

            n_read += n_read2;
        }
    }

    return n_read;
}

ssize_t ringbuf_copy_to (RINGBUF rb, void* ctx, RINGBUF_WRITE_CALLBACK write) {
    size_t n_start = rb->start >= rb->end ? rb->n - rb->start : rb->end - rb->start;
    if (!n_start) return 0;

    ssize_t n_write = write (ctx, rb->buf + rb->start, n_start);
    if (n_write < 0) return -1;

    if (n_write) {
        rb->start += n_write;
        if (rb->start == rb->n && rb->end > 0) rb->start = 0;
    }

    if ((size_t)n_write < n_start) return n_write;

    if (rb->end > rb->start) {
        ssize_t n_write2 = write (ctx, rb->buf, rb->end);
        if (n_write2 < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) n_write2 = 0;
        if (n_write2 < 0) return -1;

        rb->start += n_write2;
        n_write += n_write2;
    }

    if (rb->start == rb->end) rb->start = rb->n, rb->end = 0;

    return n_write;
}

int ringbuf_resize (RINGBUF rb, size_t n) {
    size_t xsize = ringbuf_size (rb);
    if (n < xsize) {
        errno = EINVAL;
        return 0;
    }

    void* nbuf = malloc (n);
    if (!nbuf) return 0;

    if (!xsize) {
        free (rb->buf);
        rb->buf = nbuf, rb->start = rb->n = n;
        return 1;
    }

    if (rb->start >= rb->end) {
        memcpy (nbuf, rb->buf + rb->start, rb->n - rb->start);
        memcpy (nbuf + rb->n - rb->start, rb->buf, rb->end);
    } else {
        memcpy (nbuf, rb->buf + rb->start, xsize);
    }

    free (rb->buf);
    rb->buf = nbuf, rb->n = n, rb->start = 0, rb->end = xsize;

    return 1;
}

ssize_t ringbuf_copyd_from (RINGBUF rb, void* ctx, RINGBUF_READ_CALLBACK read) {
    ssize_t ret = 0;
    while (1) {
        ssize_t pret = ringbuf_copy_from (rb, ctx, read);
        if (pret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) pret = 0;
        if (pret < 0) return -1;

        ret += pret;
        if (rb->start != rb->end) return ret;

        if (!ringbuf_resize (rb, rb->n * 2)) return -1;
    }
}

ssize_t ringbuf_copyd_to (RINGBUF rb, void* ctx, RINGBUF_WRITE_CALLBACK write) {
    ssize_t ret = ringbuf_copy_to (rb, ctx, write);

    size_t xsize = ringbuf_size (rb);
    if (xsize * 4 + RINGBUF_BLOCK * 2 < rb->n) ringbuf_resize (rb, xsize * 2 + RINGBUF_BLOCK);

    return ret;
}
