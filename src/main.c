#include <errno.h>
#include <fcntl.h>
#include <libptytty.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vterm.h>

#include "mvterm.h"
#include "ringbuf.h"

int main (void) {
    VTerm* vt = vterm_new (120, 40);
    if (!vt) {
        fprintf (stderr, "fail to create vterm\n");
        exit (EXIT_FAILURE);
    }
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

    pid_t pid = fork ();
    if (pid == 0) {
        setsid ();
        dup2 (slave, STDIN_FILENO);
        dup2 (slave, STDOUT_FILENO);
        dup2 (slave, STDERR_FILENO);
        close (slave);
        setenv ("TERM", "xterm-256color", 1);
        execlp ("sh", "sh", "-i", NULL);
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

    while (1) {
        int stdin_fd = STDIN_FILENO;
        if (ringbuf_copyd_from (in_buf, &stdin_fd, RINGBUF_READ_FD) < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror ("unable to read stdin");
            exit (EXIT_FAILURE);
        }

        if (ringbuf_copyd_to (in_buf, &master, RINGBUF_WRITE_FD) < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror ("unable to write pty");
            exit (EXIT_FAILURE);
        }

        if (ringbuf_copyd_from (out_buf, &master, RINGBUF_READ_FD) < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror ("unable to read pty");
            exit (EXIT_FAILURE);
        }

        int mov_byte = 0;
        mov_byte |= ringbuf_copyd_to (out_buf, vt, RINGBUF_WRITE_VTERM);
        mov_byte |= ringbuf_copyd_from (in_buf, vt, RINGBUF_READ_VTERM);

        if (!mov_byte) usleep (10);
    }

    close (master);
    vterm_free (vt);

    syscall (SYS_pidfd_send_signal, pidfd, SIGKILL, NULL, 0);
    waitpid (pid, NULL, 0);

    close (pidfd);
    ptytty_delete (pty);

    return 0;
}
