#include <errno.h>
#include <fcntl.h>
#include <libptytty.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vterm.h>

#include "mvterm.h"
#include "ringbuf.h"

uint64_t now_ms () {
    struct timespec ts;
    timespec_get (&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
int parse_uint (const char* str, uint64_t* val) {
    char* endptr;
    *val = strtoul (str, &endptr, 0);
    return !*endptr;
}
int parse_int (const char* str, int64_t* val) {
    char* endptr;
    *val = strtol (str, &endptr, 0);
    return !*endptr;
}
static void print_usage (FILE* out, const char* prog) {
    fprintf (out,
             "Usage: %s [options] [--] [command [args...]]\n"
             "\n"
             "Run COMMAND on a pseudoterminal and mirror its output into an\n"
             "internal libvterm screen model.  Bytes read from standard input\n"
             "are sent to the child process, except for vtemu input escapes.\n"
             "\n"
             "If COMMAND is omitted, vtemu runs: sh -i\n"
             "\n"
             "The child starts with a controlling terminal, TERM=xterm-256color,\n"
             "and the terminal size selected by -l and -c.\n"
             "\n"
             "About sh -i:\n"
             "  The -i flag asks sh to run as an interactive shell.  In that mode\n"
             "  the shell behaves like one opened from a real terminal: it prints\n"
             "  prompts, enables job-control commands such as fg and bg when the\n"
             "  shell supports them, and uses terminal-oriented behavior for input.\n"
             "  This is the default because vtemu is usually used to observe and\n"
             "  test full-screen or interactive terminal behavior.\n"
             "\n"
             "  Plain sh, without -i, may treat its input as a non-interactive\n"
             "  script.  It usually prints no prompt, may skip interactive startup\n"
             "  behavior, and may handle input differently.  Use plain sh only when\n"
             "  you intentionally want script-style execution instead of a live\n"
             "  shell session.\n"
             "\n"
             "Options:\n"
             "  -c NUM    set virtual terminal columns to NUM (default: 80)\n"
             "  -l NUM    set virtual terminal lines to NUM (default: 24)\n"
             "  -s NUM    set idle polling interval in microseconds (default: 10);\n"
             "              use 0 to yield the scheduler instead of sleeping\n"
             "  -x NUM    set <X> pause duration in milliseconds (default: 100)\n"
             "  -v        turn on visual mode; printed screen snapshots are readable\n"
             "              in a console by emitting real ANSI escape sequences\n"
             "              for visual attributes and colors\n"
             "  -h        display this help and exit\n"
             "\n"
             "Mandatory arguments to options are mandatory.  NUM is parsed as an\n"
             "unsigned integer; decimal, octal, and hexadecimal forms accepted by\n"
             "strtoul(3) are allowed.\n"
             "\n"
             "Use -- before COMMAND when the command itself starts with an option.\n"
             "For a simple visual terminal-program snapshot:\n"
             "  echo \"<X><P>:q<CR>\" | bin/vtemu -v vim 2>/dev/null\n"
             "\n"
             "Input:\n"
             "  Standard input is processed byte by byte.  Literal newline and\n"
             "  carriage return bytes are ignored; use <CR> to send Enter to\n"
             "  the child process.\n"
             "\n"
             "Input escapes:\n"
             "\n"
             "  <P>       print the current virtual terminal screen to stdout\n"
             "  <X>       pause input processing for the -x duration while output\n"
             "              continues to be read and emulated\n"
             "  <ESC>     send an Escape byte to the child process\n"
             "  <CR>      send a carriage return byte to the child process\n"
             "  <L>       send a literal '<' byte to the child process\n"
             "\n"
             "Timing with <X>:\n"
             "  Use <X> when the child needs time to react before the next input\n"
             "  arrives.  Common cases are waiting for sh -i to print its prompt,\n"
             "  waiting for Vim or another full-screen program to initialize and\n"
             "  refresh its screen, waiting after <ESC> for Vim to leave insert\n"
             "  mode, or waiting before <P> so the snapshot captures the updated\n"
             "  virtual terminal.  Repeat <X> to wait longer.\n"
             "\n"
             "Screen printing:\n"
             "  vtemu writes a screen snapshot only when <P> is received.  Without\n"
             "  -v, the snapshot uses vtemu's stable escaped test format, such as\n"
             "  %%f... colors and %%10 cells.  This is not console-readable, but it\n"
             "  makes spaces, null cells, widths, and attributes easier to test.\n"
             "  With -v, visual mode makes the output readable in a console, but\n"
             "  some characters, such as spaces and null cells, are harder to\n"
             "  distinguish.\n"
             "\n"
             "Examples:\n"
             "  %s -h\n"
             "      Show this help text.\n"
             "\n"
             "  %s -l 8 -c 40 -- sh -i\n"
             "      Run an interactive shell in an 8-line by 40-column terminal.\n"
             "\n"
             "  printf '<X>echo hello<CR><X><P>exit<CR>' | %s -l 8 -c 40 -- sh -i\n"
             "      Wait for the shell prompt, run a command, print the screen,\n"
             "      then exit the shell.\n"
             "\n"
             "  echo \"<X><P>:q<CR>\" | %s -v vim 2>/dev/null\n"
             "      Print a visual Vim screen snapshot, then quit Vim.\n"
             "\n"
             "  printf '<ESC>:q<CR><P>' | %s -v -l 24 -c 80 -- vim\n"
             "      Send Escape and :q to Vim, then print a visual screen snapshot.\n"
             "\n"
             "  printf '%%s' '<X><X>vim tmp.txt<CR><X><X><X>ggdGiHello,World!<X><P><ESC><X><X><X>:wq<CR><X><X><X>exit<CR>' | %s -v\n"
             "      Drive Vim from the default shell, insert text, and print while\n"
             "      Vim is still in insert mode.  Because <P> appears before <ESC>,\n"
             "      the snapshot shows -- INSERT --.\n"
             "\n"
             "  printf '%%s' '<X><X>vim tmp.txt<CR><X><X><X>ggdGiHello,World!<ESC><X><X><X>:wq<CR><X><X><X>cat tmp.txt<CR><X><P>exit<CR>' | %s -v\n"
             "      Edit and save tmp.txt with Vim, return to the shell, cat the\n"
             "      saved file, print the shell screen, then exit.\n"
             "\n"
             "Exit status:\n"
             "  0  if help was displayed or vtemu exits normally\n"
             "  1  if an option is invalid, an argument is missing or malformed,\n"
             "     or a terminal/pty operation fails\n"
             "\n"
             "Notes:\n"
             "  vtemu currently supports short options only; use -h, not --help.\n"
             "  When vtemu exits, it closes the pseudoterminal and terminates any\n"
             "  remaining child process.\n",
             prog, prog, prog, prog, prog, prog, prog, prog);
}

int main (int argc, char* const* argv) {
    uint64_t lines = 24, columns = 80;
    int visual_args = 0;
    uint64_t us = 10;
    uint64_t xms = 100;

    opterr = 0;
    for (int opt; (opt = getopt (argc, argv, ":c:l:s:x:vh")) != -1;) {
        if (opt == ':') {
            fprintf (stderr, "-%c: requires an argument\n", optopt);
            fprintf (stderr, "Try '%s -h' for more information.\n", argv[0]);
            return EXIT_FAILURE;
        } else if (opt == '?') {
            fprintf (stderr, "-%c: unknown option\n", optopt);
            fprintf (stderr, "Try '%s -h' for more information.\n", argv[0]);
            return EXIT_FAILURE;
        } else if (opt == 'c') {
            if (!parse_uint (optarg, &columns)) {
                fprintf (stderr, "-c: needs an uinteger\n");
                fprintf (stderr, "Try '%s -h' for more information.\n", argv[0]);
                return EXIT_FAILURE;
            }
        } else if (opt == 'l') {
            if (!parse_uint (optarg, &lines)) {
                fprintf (stderr, "-l: needs an uinteger\n");
                fprintf (stderr, "Try '%s -h' for more information.\n", argv[0]);
                return EXIT_FAILURE;
            }
        } else if (opt == 's') {
            if (!parse_uint (optarg, &us)) {
                fprintf (stderr, "-s: needs an uinteger\n");
                fprintf (stderr, "Try '%s -h' for more information.\n", argv[0]);
                return EXIT_FAILURE;
            }
        } else if (opt == 'v') {
            visual_args |= MVTERM_PRINT_VISUAL;
        } else if (opt == 'x') {
            if (!parse_uint (optarg, &xms)) {
                fprintf (stderr, "-x: needs an uinteger\n");
                fprintf (stderr, "Try '%s -h' for more information.\n", argv[0]);
                return EXIT_FAILURE;
            }
        } else if (opt == 'h') {
            print_usage (stdout, argv[0]);
            return EXIT_SUCCESS;
        }
    }

    VTerm* vt = vterm_new (lines, columns);
    if (!vt) {
        fprintf (stderr, "fail to create vterm\n");
        exit (EXIT_FAILURE);
    }
    vterm_set_utf8 (vt, 1);
    VTermScreen* vts = vterm_obtain_screen (vt);
    vterm_screen_set_callbacks (vts, &mvtscb, NULL);
    vterm_screen_reset (vts, 1);

    PTYTTY pty = ptytty_create ();
    if (!ptytty_get (pty)) {
        fprintf (stderr, "fail to get ptytty\n");
        exit (EXIT_FAILURE);
    }

    int master = ptytty_pty (pty);
    int slave = ptytty_tty (pty);
    fcntl (master, F_SETFL, fcntl (master, F_GETFL) | O_NONBLOCK);

    struct winsize ws;
    ws.ws_row = lines, ws.ws_col = columns, ws.ws_xpixel = ws.ws_ypixel = 0;
    if (ioctl (master, TIOCSWINSZ, &ws) == -1) {
        perror ("fail to set pty winsize");
        exit (EXIT_FAILURE);
    }

    pid_t pid = fork ();
    if (pid == 0) {
        setsid ();
        if (ioctl (slave, TIOCSCTTY, 0) == -1) {
            perror ("fail to set controlling tty");
            _exit (EXIT_FAILURE);
        }
        close (master);
        dup2 (slave, STDIN_FILENO);
        dup2 (slave, STDOUT_FILENO);
        dup2 (slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) close (slave);
        setenv ("TERM", "xterm-256color", 1);
        argv[optind] ? execvp (argv[optind], argv + optind) : execlp ("sh", "sh", "-i", NULL);
        _exit (EXIT_FAILURE);
    }

    int pidfd = syscall (SYS_pidfd_open, pid, 0);
    if (pidfd == -1) {
        perror ("pidfd_open");
        exit (1);
    }

    close (slave);

    fcntl (STDIN_FILENO, F_SETFL, fcntl (STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    fcntl (master, F_SETFL, fcntl (master, F_GETFL) | O_NONBLOCK);

    RINGBUF in_buf = ringbuf_alloc (RINGBUF_BLOCK), out_buf = ringbuf_alloc (RINGBUF_BLOCK);
    RINGBUF back_buf = ringbuf_alloc (RINGBUF_BLOCK);
    if (!in_buf || !out_buf || !back_buf) {
        ringbuf_free (in_buf), ringbuf_free (out_buf), ringbuf_free (back_buf);
        perror ("unable to allocate memory");
        exit (EXIT_FAILURE);
    }

    int status = VTERM_ESCAPE_INIT_STAT;
    uint64_t wait = now_ms ();
    while (1) {
        if (wait < now_ms ()) {
            int n;
            for (char c; (n = read (STDIN_FILENO, &c, 1)) > 0;) {
                int comm = vterm_escape_translate (in_buf, &status, c);
                if (comm == -1) {
                    fprintf (stderr, "unable to parse escape\n");
                    exit (EXIT_FAILURE);
                } else if (comm == MVTERM_COMM_PRINT) {
                    print_vterm (vt, visual_args);
                } else if (comm == MVTERM_COMM_PAUSE) {
                    wait = now_ms () + xms;
                    errno = EWOULDBLOCK;
                    break;
                }
            }
            if (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror ("unable to read stdin");
                break;
            }
        }

        if (ringbuf_copyd_to (in_buf, &master, RINGBUF_WRITE_FD) < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror ("unable to write pty");
            break;
        }

        if (ringbuf_copyd_from (out_buf, &master, RINGBUF_READ_FD) < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            if (errno != EIO) perror ("unable to read pty");
            break;
        }

        int mov_byte = 0;
        mov_byte |= ringbuf_copyd_to (out_buf, vt, RINGBUF_WRITE_VTERM);
        mov_byte |= ringbuf_copyd_from (in_buf, vt, RINGBUF_READ_VTERM);

        if (!mov_byte) {
            if (us)
                usleep (us);
            else
                sched_yield ();
        }
    }

    close (master);
    vterm_free (vt);

    syscall (SYS_pidfd_send_signal, pidfd, SIGKILL, NULL, 0);
    waitpid (pid, NULL, 0);

    close (pidfd);
    ptytty_delete (pty);

    return 0;
}
