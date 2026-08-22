/*
 * syscalls.c - minimal newlib syscall stubs for GNU Arm Embedded toolchain
 *
 * Purpose:
 *   The GCC toolchain (arm-none-eabi-gcc + newlib-nano) links libc_nano
 *   reentrant functions (_read_r/_write_r/...) which resolve to the raw
 *   syscalls below. Without stubs the linker reports
 *   "XXX is not implemented and will always fail".
 *
 * Scope (M1):
 *   Boot/App do not use printf/scanf yet, so every stub just returns an
 *   error. This file is compiled ONLY by GCC; under ARMCC (AC5/MicroLIB)
 *   the whole content is excluded - the AC5-style retarget lives in
 *   board_usart.c (fputc + __use_no_semihosting).
 *
 * M4 TODO: retarget _write/_read to USART0 (board_usart0_*) and give
 *   _fstat/_isatty proper results once printf/scanf are actually used.
 */

#if !defined(__ARMCC_VERSION)

#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/unistd.h>

int _close(int file)
{
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    (void)st;
    return -1;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    return -1;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return -1;
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    /* M4: retarget to USART0 RX */
    return -1;
}

int _write(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    /* M4: retarget to USART0 TX */
    return -1;
}

void _exit(int status)
{
    (void)status;
    for (;;)
    {
    }
}

#endif /* !__ARMCC_VERSION */
