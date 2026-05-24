#ifndef _HORIZONOS_SYSCALL_H
#define _HORIZONOS_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

#define SYSCALL_PRINT 0
#define SYSCALL_MMAP 1
#define SYSCALL_MUNMAP 2
#define SYSCALL_GETTID 3
#define SYSCALL_ARCHCTL 4
#define SYSCALL_EXIT 5
#define SYSCALL_CLOCKGET 6
#define SYSCALL_SYSINFO 7
#define SYSCALL_GETCPU 8
#define SYSCALL_KILLTHREAD 9
#define SYSCALL_PAUSE 10
#define SYSCALL_THREADEXIT 11
#define SYSCALL_NEWTHREAD 12
#define SYSCALL_SENDMSG 13
#define SYSCALL_RECVMSG 14
#define SYSCALL_PORTREGISTER 15
#define SYSCALL_ISTHREADALIVE 16
#define SYSCALL_FUTEX 17
#define SYSCALL_SIGACTION 18
#define SYSCALL_SIGRETURN 19
#define SYSCALL_MPROTECT 20
#define SYSCALL_NANOSLEEP 21
#define SYSCALL_ISATTY 22
#define SYSCALL_IOPERM 23
#define SYSCALL_IOPL 24
#define SYSCALL_KILL 25
#define SYSCALL_GETPID 26
#define SYSCALL_MMAPPHYS 27
#define SYSCALL_GETRSDP 28
#define SYSCALL_INSTALLIRQHANDLER 29
#define SYSCALL_UNINSTALLIRQHANDLER 30
#define SYSCALL_GETIRQMODE 31
#define SYSCALL_SETINTSTATUS 32
#define SYSCALL_ALLOC_INT_VEC 33
#define SYSCALL_FREE_INT_VEC 34
#define SYSCALL_ALLOC_GSI 35
#define SYSCALL_FREE_GSI 36
#define SYSCALL_LOCK_TO_CORE 37
#define SYSCALL_GET_CPU_COUNT 38
#define SYSCALL_GET_CPU_IDS 39

#ifndef __MLIBC_ABI_ONLY

static long syscall(long func, long* ret, uint64_t p1 = 0, uint64_t p2 = 0, uint64_t p3 = 0, uint64_t p4 = 0, uint64_t p5 = 0, uint64_t p6 = 0) {
	volatile long err;
	long result;

	register uint64_t r4 asm("r10") = p4;
	register uint64_t r5 asm("r8") = p5;
	register uint64_t r6 asm("r9") = p6;

	asm volatile("syscall"
		: "=a"(result), "=d"(err)
		: "a"(func), "D"(p1), "S"(p2), "d"(p3), "r"(r4),
		"r"(r5), "r"(r6)
		: "memory", "rcx", "r11");

	if (ret != nullptr) {
		*ret = result;
	}

    return err;
}

#endif /* !__MLIBC_ABI_ONLY */

#endif /* _HORIZONOS_SYSCALL_H */
