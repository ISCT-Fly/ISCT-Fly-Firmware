/* Minimal newlib system calls.  The console does its own output, so stdio is not wired up;
 * only the heap (used by the float formatter in vsnprintf) needs to work. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

extern uint8_t _end;            /* end of .bss, from the linker script */
extern uint8_t _estack;         /* top of RAM */
extern uint32_t _Min_Stack_Size;

void *_sbrk(ptrdiff_t incr)
{
    static uint8_t *brk;
    uint8_t *limit = &_estack - (uintptr_t)&_Min_Stack_Size;

    if (brk == NULL) {
        brk = &_end;
    }
    if (brk + incr > limit) {
        errno = ENOMEM;
        return (void *)-1;
    }
    uint8_t *prev = brk;
    brk += incr;
    return prev;
}

int _write(int fd, const char *buf, int len) { (void)fd; (void)buf; return len; }
int _read(int fd, char *buf, int len)        { (void)fd; (void)buf; (void)len; return 0; }
int _close(int fd)                           { (void)fd; return -1; }
int _lseek(int fd, int off, int whence)      { (void)fd; (void)off; (void)whence; return 0; }
int _fstat(int fd, struct stat *st)          { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)                          { (void)fd; return 1; }
int _getpid(void)                            { return 1; }
int _kill(int pid, int sig)                  { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int status)                       { (void)status; for (;;) {} }
