#include <sys/mman.h>
#include <mlibc/debug.hpp>
#include <errno.h>
#include <mlibc/all-sysdeps.hpp>
#include <bits/ensure.h>
#include <mlibc/tcb.hpp>

extern "C" void __mlibc_thread_trampoline(void *(*fn)(void *), Tcb *tcb, void *arg) {
	if(mlibc::sysdep<TcbSet>(tcb))
		__ensure(!"failed to set tcb for new thread");

	__atomic_store_n(&tcb->tid, mlibc::sysdep<FutexTid>(), __ATOMIC_RELAXED);
	__atomic_fetch_or(&tcb->cancelBits, tcbCancelEnableBit, __ATOMIC_RELAXED);

	tcb->invokeThreadFunc(reinterpret_cast<void *>(fn), arg);

	mlibc::thread_exit(tcb->returnValue);
}

#define DEFAULT_STACK 0x400000

namespace mlibc {
	int Sysdeps<PrepareStack>::operator()(
		void **stack,
		void *entry,
		void *arg,
		void *tcb,
		size_t *stack_size,
		size_t *guard_size,
		void **stack_base
	) {
		const size_t pageSize = 4096;
		*stack_size = *stack_size ? (*stack_size + pageSize - 1) & ~(pageSize - 1) : DEFAULT_STACK;

		if(!*stack) {
			*guard_size = pageSize;

			const size_t totalSize = *stack_size + *guard_size;
			*stack_base = mmap(NULL, totalSize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			if(*stack_base == MAP_FAILED)
				return errno;

			if(mprotect(*stack_base, *guard_size, PROT_NONE)) {
				int err = errno;
				munmap(*stack_base, totalSize);
				return err;
			}

			*stack = (void *)((char *)*stack_base + totalSize);
		} else {
			*guard_size = 0;
			*stack_base = *stack;
			*stack = (void *)((char *)*stack_base + *stack_size);
		}

		void **stack_it = (void **)*stack;

		*--stack_it = arg;
		*--stack_it = tcb;
		*--stack_it = entry;

		*stack = (void *)stack_it;

		return 0;
	}
}
