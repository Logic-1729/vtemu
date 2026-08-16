#include "mvterm.h"

#include <stdio.h>
#include <vterm.h>

static void pututf8 (uint32_t cp) {
    if (cp < 0x80) {
        putchar ((int)cp);
    } else if (cp < 0x800) {
        putchar (0xC0 | (int)(cp >> 6));
        putchar (0x80 | (int)(cp & 0x3F));
    } else if (cp < 0x10000) {
        putchar (0xE0 | (int)(cp >> 12));
        putchar (0x80 | (int)((cp >> 6) & 0x3F));
        putchar (0x80 | (int)(cp & 0x3F));
    } else {
        putchar (0xF0 | (int)(cp >> 18));
        putchar (0x80 | (int)((cp >> 12) & 0x3F));
        putchar (0x80 | (int)((cp >> 6) & 0x3F));
        putchar (0x80 | (int)(cp & 0x3F));
    }
}

void print_vterm (VTerm* vt) {
    int rows, cols;
    vterm_get_size (vt, &rows, &cols);

    VTermColor fg, bg;
    vterm_state_get_default_colors (vterm_obtain_state (vt), &fg, &bg);

    // TODO: print attr
    // VTermScreenCellAttrs attr = {};

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols;) {
            VTermScreenCell cell;
            vterm_screen_get_cell (vterm_obtain_screen (vt), (VTermPos){r, c}, &cell);

            if (cell.width) {
                if (!cell.chars[0]) putchar ('-');

                for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; i++) {
                    uint32_t cp = cell.chars[i];
                    if (cp == '%' || cp == '-') putchar ('%');
                    pututf8 (cp);
                }
                c += cell.width;
            } else {
                fprintf (stderr, "vterm screen format error\n");
                ++c;
            }
        }

        putchar ('\n');
    }
}

int vterm_escape (RINGBUF dest, int escape) {
    if (escape == 48) {  // <L>
        ringbuf_write (dest, "<", 1);
    } else if (escape == 52) {  // <P>
        return VTERM_COMM_PRINT;
    } else if (escape == 60) {  // <X>
        return VTERM_COMM_PAUSE;
    } else if (escape == 2550) {  // <CR>
        ringbuf_write (dest, "\r", 1);
    } else if (escape == 171495) {  // <ESC>
        ringbuf_write (dest, "\x1b", 1);
    } else {
        fprintf (stderr, "unknown escape %d\n", escape);
        return -1;
    }

    return 0;
}

const int VTERM_ESCAPE_INIT_STAT = -1;
int vterm_escape_translate (RINGBUF dest, int* status, char c) {
    if (*status == -1) {
        if (c == '<')
            *status = 0;
        else if (c != '\n' && c != '\r')
            ringbuf_write (dest, &c, 1);

    } else {
        if (c == '>') {
            int ret = vterm_escape (dest, *status);
            *status = -1;
            return ret;
        }
        if (*status & (63 << 24)) return -1;

        if ('0' <= c && c <= '9') {
            *status = *status * 64 + c - '0' + 1;
        } else if ('a' <= c && c <= 'z') {
            *status = *status * 64 + c - 'a' + 11;
        } else if ('A' <= c && c <= 'Z') {
            *status = *status * 64 + c - 'A' + 37;
        } else {
            return -1;
        }
    }
    return 0;
}

int cb_damage (VTermRect rect, void* u) {
    (void)rect;
    (void)u;
    return 1;
}
int cb_moverect (VTermRect d, VTermRect s, void* u) {
    (void)d;
    (void)s;
    (void)u;
    return 1;
}
int cb_movecursor (VTermPos p, VTermPos o, int v, void* u) {
    (void)p;
    (void)o;
    (void)v;
    (void)u;
    return 1;
}
int cb_settermprop (VTermProp p, VTermValue* val, void* u) {
    (void)p;
    (void)val;
    (void)u;
    return 1;
}
int cb_bell (void* u) {
    (void)u;
    return 1;
}
int cb_pushline (int c, const VTermScreenCell* cl, void* u) {
    (void)c;
    (void)cl;
    (void)u;
    return 1;
}
int cb_popline (int c, VTermScreenCell* cl, void* u) {
    (void)c;
    (void)cl;
    (void)u;
    return 1;
}

const VTermScreenCallbacks mvtscb = {cb_damage,   cb_moverect, cb_movecursor, cb_settermprop, cb_bell, NULL,
                                     cb_pushline, cb_popline,  NULL};

static ssize_t ringbuf_read_vterm (void* ctx, void* buf, size_t n) {
    return (ssize_t)vterm_output_read ((VTerm*)ctx, (char*)buf, n);
}
static ssize_t ringbuf_write_vterm (void* ctx, const void* buf, size_t n) {
    return (ssize_t)vterm_input_write ((VTerm*)ctx, (const char*)buf, n);
}

RINGBUF_READ_CALLBACK RINGBUF_READ_VTERM = ringbuf_read_vterm;
RINGBUF_WRITE_CALLBACK RINGBUF_WRITE_VTERM = ringbuf_write_vterm;
