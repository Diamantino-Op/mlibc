#include <bits/ensure.h>
#include <mlibc/debug.hpp>
#include <mlibc/all-sysdeps.hpp>
#include "generic.h"
#include <errno.h>
#include <horizonos/syscall.h>
#include <horizonos/archctl.h>
#include <string.h>
#include <stdlib.h>
#include <asm/ioctls.h>
#include <poll.h>
#include <sys/select.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <mlibc/tcb.hpp>

int register_horizonos_port(int port) {
	long ret;
	return syscall(SYSCALL_PORTREGISTER, &ret, port);
}

int send_horizonos_message(int port, const struct hos_msg *hdr) {
	long ret;
	return syscall(SYSCALL_SENDMSG, &ret, port, (uint64_t)hdr);
}

int receive_horizonos_message(int port, struct hos_msg *hdr) {
	long ret;
	return syscall(SYSCALL_RECVMSG, &ret, port, (uint64_t)hdr);
}

int is_thread_alive(int tid, bool *alive) {
	long ret;
	int err = syscall(SYSCALL_ISTHREADALIVE, &ret, tid);
	*alive = ret != 0;

	return err;
}

namespace mlibc {
	[[noreturn]] static void panic_unimplemented_sysdep(const char *name) {
		mlibc::panicLogger() << "mlibc: unimplemented sysdep " << name << frg::endlog;
		__builtin_trap();
	}

	// Print

	void Sysdeps<LibcLog>::operator()(const char *message) {
		long ret;
		syscall(SYSCALL_PRINT, &ret, (uint64_t)message);
	}

	[[noreturn]] void Sysdeps<LibcPanic>::operator()() {
		sysdep<LibcLog>("mlibc: panic");
		sysdep<Exit>(1);
	}

	// Map

	int Sysdeps<VmMap>::operator()(void *hint, size_t size, int prot, int flags, int fd, off_t offset, void **window) {
		long ret;
		long err = syscall(SYSCALL_MMAP, &ret, (uint64_t)hint, size, prot, flags, fd, offset);
		*window = (void *)ret;
		return err;
	}

	// Unmap

	int Sysdeps<VmUnmap>::operator()(void *pointer, size_t size) {
		long ret;
		return syscall(SYSCALL_MUNMAP, &ret, (uintptr_t)pointer, size);
	}

	// Protect

	int Sysdeps<VmProtect>::operator()(void *pointer, size_t size, int prot) {
		long ret;
		return syscall(SYSCALL_MPROTECT, &ret, (uint64_t)pointer, size, prot);
	}

	// Get TID

	int Sysdeps<FutexTid>::operator()() {
		long ret;
		syscall(SYSCALL_GETTID, &ret);
		return ret;
	}

	pid_t Sysdeps<GetTid>::operator()() {
		long ret;
		syscall(SYSCALL_GETTID, &ret);
		return ret;
	}

	// Set FSBASE

	int Sysdeps<TcbSet>::operator()(void *pointer) {
		long r;
		return syscall(SYSCALL_ARCHCTL, &r, ARCH_CTL_SET_FSBASE, (uint64_t)pointer);
	}

	// Exit

	[[noreturn]] void Sysdeps<Exit>::operator()(int status) {
		syscall(SYSCALL_EXIT, NULL, status);
		__builtin_unreachable();
	}

	// Get Clock

	int Sysdeps<ClockGet>::operator()(int clock, time_t *secs, long *nanos) {
		struct timespec ts;
		long ret;
		int err = syscall(SYSCALL_CLOCKGET, &ret, clock, (uint64_t)&ts);
		*secs = ts.tv_sec;
		*nanos = ts.tv_nsec;
		return err;
	}

	// Sysinfo

	int Sysdeps<Sysinfo>::operator()(struct sysinfo *info) {
		long r;
		return syscall(SYSCALL_SYSINFO, &r, (uint64_t)info);
	}

	// Get CPU

	int Sysdeps<Getcpu>::operator()(int *cpu) {
		long ret;
		// can never fail
		syscall(SYSCALL_GETCPU, &ret);
		*cpu = ret;
		return 0;
	}

	// Kill Thread

	int Sysdeps<Tgkill>::operator()(int pid, int tid, int sig) {
		long ret;
		return syscall(SYSCALL_KILLTHREAD, &ret, pid, tid, sig);
	}

	// Pause

	int Sysdeps<Pause>::operator()() {
		long ret;
		return syscall(SYSCALL_PAUSE, &ret);
	}

	// Messages

	int Sysdeps<Sleep>::operator()(time_t *secs, long *nanos) {
		long ret;
		long err = syscall(SYSCALL_NANOSLEEP, &ret, *secs, *nanos);

		if (err == 0) {
			*secs = 0;
			*nanos = 0;
		}

		return err;
	}

	pid_t Sysdeps<GetPid>::operator()() {
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Kill>::operator()(pid_t pid, int signal) {
		(void)pid;
		(void)signal;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	// Futex

	#define FUTEX_WAIT 0
	#define FUTEX_WAKE 1

	int Sysdeps<FutexWait>::operator()(int *pointer, int expected, const struct timespec *time) {
		long ret;
		return syscall(SYSCALL_FUTEX, &ret, (uint64_t)pointer, FUTEX_WAIT, expected, (uint64_t)time);
	}

	int Sysdeps<FutexWake>::operator()(int *pointer, bool all) {
		long ret;
		return syscall(SYSCALL_FUTEX, &ret, (uint64_t)pointer, FUTEX_WAKE, all ? INT_MAX : 1, 0);
	}

#ifndef MLIBC_BUILDING_RTLD

	[[noreturn]] void Sysdeps<ThreadExit>::operator()() {
		syscall(SYSCALL_THREADEXIT, nullptr);
		__builtin_unreachable();
	}

	extern "C" void __mlibc_thread_entry();

	int Sysdeps<Clone>::operator()(void *tcb, pid_t *pid_out, void *stack) {
		long ret;
		long err = syscall(SYSCALL_NEWTHREAD, &ret, (uintptr_t)__mlibc_thread_entry, (uintptr_t)stack);
		if (!err) {
			auto *newTcb = reinterpret_cast<Tcb *>(tcb);
			__atomic_store_n(&newTcb->tid, static_cast<int>(ret), __ATOMIC_RELAXED);
			mlibc::sysdep<FutexWake>(&newTcb->tid, true);
		}

		*pid_out = ret;
		return err;
	}

#endif

	int Sysdeps<GetRlimit>::operator()(int resource, struct rlimit *limit) {
		switch(resource) {
		case RLIMIT_NOFILE:
			limit->rlim_cur = RLIM_INFINITY;
			limit->rlim_max = RLIM_INFINITY;
			return 0;
		default:
			return EINVAL;
		}
	}

#ifndef MLIBC_BUILDING_RTLD

	typedef struct {
		ino_t d_ino;
		off_t d_off;
		unsigned short d_reclen;
		unsigned char d_type;
		char d_name[1024];
	} dent_t;

#endif

// Alloc

	int Sysdeps<AnonAllocate>::operator()(size_t size, void **pointer) {
		size += 4096 - (size % 4096);
		return sysdep<VmMap>(NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0, pointer);
	}

	int Sysdeps<AnonFree>::operator()(void *pointer, size_t size) {
		size += 4096 - (size % 4096);
		return sysdep<VmUnmap>(pointer, size);
	}

#ifndef MLIBC_BUILDING_RTLD
	extern "C" void __mlibc_restorer();

	int Sysdeps<Sigaction>::operator()(int sig, const struct sigaction *__restrict act,
		struct sigaction *__restrict oldact) {
			long ret;

			struct sigaction newAction;
			if(act)
				memcpy(&newAction, act, sizeof(struct sigaction));

			if(act && (newAction.sa_flags & SA_RESTORER) == 0) {
				newAction.sa_restorer = __mlibc_restorer;
				newAction.sa_flags |= SA_RESTORER;
			}

			return syscall(SYSCALL_SIGACTION, &ret, sig, act ? (uint64_t)&newAction : 0, (uint64_t)oldact);
	}
#endif

	// IsaTTY

	int Sysdeps<Isatty>::operator()(int fd) {
		long ret;
		return syscall(SYSCALL_ISATTY, &ret, fd);
	}

	// Stubs
	int Sysdeps<SetGroups>::operator()(size_t size, const gid_t *list) {
		(void)size;
		(void)list;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<GetGroups>::operator()(size_t size, gid_t *list, int *ret) {
		(void)size;
		(void)list;
		(void)ret;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<GetSockopt>::operator()(int fd, int layer, int number, void *__restrict buffer, socklen_t *__restrict size) {
		(void)fd;
		(void)layer;
		(void)number;
		(void)buffer;
		(void)size;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<InetConfigured>::operator()(bool *ipv4, bool *ipv6) {
		(void)ipv4;
		(void)ipv6;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Readv>::operator()(int fd, const struct iovec *iovs, int iovc, ssize_t *bytes_read) {
		(void)fd;
		(void)iovs;
		(void)iovc;
		(void)bytes_read;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Writev>::operator()(int fd, const struct iovec *iovs, int iovc, ssize_t *bytes_written) {
		(void)fd;
		(void)iovs;
		(void)iovc;
		(void)bytes_written;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Flock>::operator()(int fd, int options) {
		(void)fd;
		(void)options;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Nice>::operator()(int nice, int *ret) {
		(void)nice;
		(void)ret;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Shutdown>::operator()(int sockfd, int how) {
		(void)sockfd;
		(void)how;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Sigpending>::operator()(sigset_t *set) {
		(void)set;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Sigtimedwait>::operator()(const sigset_t *__restrict set, siginfo_t *__restrict info, const struct timespec *__restrict timeout, int *out_signal) {
		(void)set;
		(void)info;
		(void)timeout;
		(void)out_signal;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Sigsuspend>::operator()(const sigset_t *set) {
		(void)set;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<SetUid>::operator()(uid_t id) {
		(void)id;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<SetGid>::operator()(gid_t id) {
		(void)id;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<SetEuid>::operator()(uid_t id) {
		(void)id;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<SetEgid>::operator()(gid_t id) {
		(void)id;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	uid_t Sysdeps<GetUid>::operator()() {
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	uid_t Sysdeps<GetEuid>::operator()() {
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	gid_t Sysdeps<GetGid>::operator()() {
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	gid_t Sysdeps<GetEgid>::operator()() {
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<SetResuid>::operator()(uid_t _ruid, uid_t _euid, uid_t _suid) {
		(void)_ruid;
		(void)_euid;
		(void)_suid;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<SetResgid>::operator()(gid_t _rgid, gid_t _egid, gid_t _sgid) {
		(void)_rgid;
		(void)_egid;
		(void)_sgid;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<GetResuid>::operator()(uid_t *ruid, uid_t *euid, uid_t *suid) {
		(void)ruid;
		(void)euid;
		(void)suid;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<GetResgid>::operator()(gid_t *rgid, gid_t *egid, gid_t *sgid) {
		(void)rgid;
		(void)egid;
		(void)sgid;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Mknodat>::operator()(int dirfd, const char *path, int mode, int dev) {
		(void)dirfd;
		(void)path;
		(void)mode;
		(void)dev;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Mkfifoat>::operator()(int dirfd, const char *path, mode_t mode) {
		(void)dirfd;
		(void)path;
		(void)mode;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Pread>::operator()(int fd, void *buf, size_t n, off_t off, ssize_t *bytes_read) {
		(void)fd;
		(void)buf;
		(void)n;
		(void)off;
		(void)bytes_read;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Pwrite>::operator()(int fd, const void *buf, size_t n, off_t off, ssize_t *bytes_written) {
		(void)fd;
		(void)buf;
		(void)n;
		(void)off;
		(void)bytes_written;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Chroot>::operator()(const char *path) {
		(void)path;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Peername>::operator()(int fd, struct sockaddr *addr_ptr, socklen_t max_addr_length, socklen_t *actual_length) {
		(void)fd;
		(void)addr_ptr;
		(void)max_addr_length;
		(void)actual_length;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Sockname>::operator()(int fd, struct sockaddr *addr_ptr, socklen_t max_addr_length, socklen_t *actual_length) {
		(void)fd;
		(void)addr_ptr;
		(void)max_addr_length;
		(void)actual_length;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Socketpair>::operator()(int domain, int type_and_flags, int proto, int *fds) {
		(void)domain;
		(void)type_and_flags;
		(void)proto;
		(void)fds;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<GetItimer>::operator()(int which, struct itimerval *curr_value) {
		(void)which;
		(void)curr_value;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<SetItimer>::operator()(int which, const struct itimerval *new_value, struct itimerval *old_value) {
		(void)which;
		(void)new_value;
		(void)old_value;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Fsync>::operator()(int fd) {
		(void)fd;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Fdatasync>::operator()(int fd) {
		(void)fd;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	pid_t Sysdeps<GetPpid>::operator()() {
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<GetSid>::operator()(pid_t pid, pid_t *pgid) {
		(void)pid;
		(void)pgid;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<GetPgid>::operator()(pid_t pid, pid_t *pgid) {
		(void)pid;
		(void)pgid;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<GetHostname>::operator()(char *buffer, size_t bufsize) {
		(void)buffer;
		(void)bufsize;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<SetHostname>::operator()(const char *buffer, size_t bufsize) {
		(void)buffer;
		(void)bufsize;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Uname>::operator()(struct utsname *buf) {
		(void)buf;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	void Sysdeps<Sync>::operator()() {
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Sigaltstack>::operator()(const stack_t *ss, stack_t *oss) {
		(void)ss;
		(void)oss;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<SetPgid>::operator()(pid_t pid, pid_t pgid) {
		(void)pid;
		(void)pgid;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<SetSid>::operator()(pid_t *out) {
		(void)out;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Listen>::operator()(int fd, int backlog) {
		(void)fd;
		(void)backlog;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Accept>::operator()(int fd, int *newfd, struct sockaddr *addr_ptr, socklen_t *addr_length, int flags) {
		(void)fd;
		(void)newfd;
		(void)addr_ptr;
		(void)addr_length;
		(void)flags;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Connect>::operator()(int fd, const struct sockaddr *addr_ptr, socklen_t addr_length) {
		(void)fd;
		(void)addr_ptr;
		(void)addr_length;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<MsgRecv>::operator()(int fd, struct msghdr *hdr, int flags, ssize_t *length) {
		(void)fd;
		(void)hdr;
		(void)flags;
		(void)length;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<SetSockopt>::operator()(int fd, int layer, int number, const void *buffer, socklen_t size) {
		(void)fd;
		(void)layer;
		(void)number;
		(void)buffer;
		(void)size;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<MsgSend>::operator()(int fd, const struct msghdr *hdr, int flags, ssize_t *length) {
		(void)fd;
		(void)hdr;
		(void)flags;
		(void)length;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Bind>::operator()(int fd, const struct sockaddr *addr_ptr, socklen_t addr_length) {
		(void)fd;
		(void)addr_ptr;
		(void)addr_length;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Socket>::operator()(int family, int type, int protocol, int *fd) {
		(void)family;
		(void)type;
		(void)protocol;
		(void)fd;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Utimensat>::operator()(int dirfd, const char *pathname, const struct timespec times[2], int flags) {
		(void)dirfd;
		(void)pathname;
		(void)times;
		(void)flags;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Fchownat>::operator()(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags) {
		(void)dirfd;
		(void)pathname;
		(void)owner;
		(void)group;
		(void)flags;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Ftruncate>::operator()(int fd, size_t size) {
		(void)fd;
		(void)size;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Tcgetattr>::operator()(int fd, struct termios *attr) {
		(void)fd;
		(void)attr;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Tcsetattr>::operator()(int fd, int act, const struct termios *attr) {
		(void)fd;
		(void)act;
		(void)attr;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Poll>::operator()(struct pollfd *fds, nfds_t count, int timeout, int *num_events) {
		(void)fds;
		(void)count;
		(void)timeout;
		(void)num_events;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Ppoll>::operator()(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout, const sigset_t *sigmask, int *num_events) {
		(void)fds;
		(void)nfds;
		(void)timeout;
		(void)sigmask;
		(void)num_events;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Pselect>::operator()(int num_fds, fd_set *read_set, fd_set *write_set, fd_set *except_set, const struct timespec *timeout, const sigset_t *sigmask, int *num_events) {
		(void)num_fds;
		(void)read_set;
		(void)write_set;
		(void)except_set;
		(void)timeout;
		(void)sigmask;
		(void)num_events;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Umask>::operator()(mode_t mode, mode_t *old) {
		(void)mode;
		(void)old;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Fchmod>::operator()(int fd, mode_t mode) {
		(void)fd;
		(void)mode;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Fchmodat>::operator()(int fd, const char *pathname, mode_t mode, int flags) {
		(void)fd;
		(void)pathname;
		(void)mode;
		(void)flags;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Chmod>::operator()(const char *pathname, mode_t mode) {
		(void)pathname;
		(void)mode;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Readlinkat>::operator()(int dirfd, const char *path, void *buffer, size_t max_size, ssize_t *length) {
		(void)dirfd;
		(void)path;
		(void)buffer;
		(void)max_size;
		(void)length;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Readlink>::operator()(const char *path, void *buffer, size_t max_size, ssize_t *length) {
		(void)path;
		(void)buffer;
		(void)max_size;
		(void)length;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Linkat>::operator()(int olddirfd, const char *old_path, int newdirfd, const char *new_path, int flags) {
		(void)olddirfd;
		(void)old_path;
		(void)newdirfd;
		(void)new_path;
		(void)flags;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Link>::operator()(const char *old_path, const char *new_path) {
		(void)old_path;
		(void)new_path;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Symlinkat>::operator()(const char *target_path, int dirfd, const char *link_path) {
		(void)target_path;
		(void)dirfd;
		(void)link_path;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Symlink>::operator()(const char *target_path, const char *link_path) {
		(void)target_path;
		(void)link_path;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Mkdirat>::operator()(int dirfd, const char *path, mode_t mode) {
		(void)dirfd;
		(void)path;
		(void)mode;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Mkdir>::operator()(const char *path, mode_t mode) {
		(void)path;
		(void)mode;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Faccessat>::operator()(int dirfd, const char *pathname, int mode, int flags) {
		(void)dirfd;
		(void)pathname;
		(void)mode;
		(void)flags;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Access>::operator()(const char *path, int mode) {
		(void)path;
		(void)mode;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Pipe>::operator()(int *fds, int flags) {
		(void)fds;
		(void)flags;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Chdir>::operator()(const char *path) {
		(void)path;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Fchdir>::operator()(int fd) {
		(void)fd;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Dup>::operator()(int fd, int flags, int *newfd) {
		(void)fd;
		(void)flags;
		(void)newfd;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Execve>::operator()(const char *path, char *const argv[], char *const envp[]) {
		(void)path;
		(void)argv;
		(void)envp;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<OpenDir>::operator()(const char *path, int *handle) {
		return sysdep<Open>(path, O_DIRECTORY, 0, handle);
	}

	int Sysdeps<ReadEntries>::operator()(int handle, void *buffer, size_t max_size, size_t *bytes_read) {
		(void)handle;
		(void)buffer;
		(void)max_size;
		(void)bytes_read;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Sigprocmask>::operator()(int how, const sigset_t *__restrict set, sigset_t *__restrict retrieve) {
		(void)how;
		(void)set;
		(void)retrieve;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Stat>::operator()(fsfd_target fsfdt, int fd, const char *path, int flags, struct stat *statbuf) {
		(void)fsfdt;
		(void)fd;
		(void)path;
		(void)flags;
		(void)statbuf;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Rmdir>::operator()(const char *path) {
		return sysdep<Unlinkat>(AT_FDCWD, path, AT_REMOVEDIR);
	}

	int Sysdeps<Unlinkat>::operator()(int fd, const char *path, int flags) {
		(void)fd;
		(void)path;
		(void)flags;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Rename>::operator()(const char *path, const char *new_path) {
		return sysdep<Renameat>(AT_FDCWD, path, AT_FDCWD, new_path);
	}

	int Sysdeps<Renameat>::operator()(int olddirfd, const char *old_path, int newdirfd, const char *new_path) {
		(void)olddirfd;
		(void)old_path;
		(void)newdirfd;
		(void)new_path;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Fcntl>::operator()(int fd, int request, va_list args, int *result) {
		(void)fd;
		(void)request;
		(void)args;
		(void)result;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Openat>::operator()(int dirfd, const char *path, int flags, mode_t mode, int *fd) {
		(void)dirfd;
		(void)path;
		(void)flags;
		(void)mode;
		(void)fd;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Fork>::operator()(pid_t *pid) {
		(void)pid;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Waitpid>::operator()(pid_t pid, int *status, int flags, struct rusage *ru, pid_t *ret_pid) {
		(void)pid;
		(void)status;
		(void)flags;
		(void)ru;
		(void)ret_pid;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Dup2>::operator()(int fd, int flags, int newfd) {
		(void)fd;
		(void)flags;
		(void)newfd;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

#ifndef MLIBC_BUILDING_RTLD
	int Sysdeps<GetEntropy>::operator()(void *buffer, size_t length) {
		int fd;
		int error = sysdep<Open>("/dev/urandom", O_RDONLY, 0, &fd);
		if(error)
			mlibc::panicLogger() << "/dev/urandom open error " << strerror(error) << frg::endlog;

		ssize_t bytes;
		error = sysdep<Read>(fd, buffer, length, &bytes);
		if(error) {
			mlibc::infoLogger() << "/dev/urandom read error " << strerror(error) << frg::endlog;
			return error;
		}

		sysdep<Close>(fd);
		return 0;
	}
#endif

	int Sysdeps<Mount>::operator()(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data) {
		(void)source;
		(void)target;
		(void)fstype;
		(void)flags;
		(void)data;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Ioctl>::operator()(int fd, unsigned long request, void *arg, int *result) {
		(void)fd;
		(void)request;
		(void)arg;
		(void)result;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Open>::operator()(const char *pathname, int flags, mode_t mode, int *fd) {
		(void)pathname;
		(void)flags;
		(void)mode;

		if(fd)
			*fd = -1;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	};

	int Sysdeps<Read>::operator()(int fd, void *buff, size_t count, ssize_t *bytes_read) {
		(void)fd;
		(void)buff;
		(void)count;

		if(bytes_read)
			*bytes_read = 0;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	// TODO

	int Sysdeps<Write>::operator()(int fd, const void *buff, size_t count, ssize_t *bytes_written) {
		if(bytes_written)
			*bytes_written = 0;

		if((fd == 1 || fd == 2) && buff && count) {
			const char *data = static_cast<const char *>(buff);
			size_t written = 0;

			while(written < count) {
				char message[257];
				size_t chunk = count - written;
				if(chunk > 256)
					chunk = 256;

				memcpy(message, data + written, chunk);
				message[chunk] = '\0';

				long ret;
				syscall(SYSCALL_PRINT, &ret, reinterpret_cast<uint64_t>(message));
				written += chunk;
			}

			if(bytes_written)
				*bytes_written = static_cast<ssize_t>(written);
			return 0;
		}

		return 0;
	}

	int Sysdeps<Seek>::operator()(int fd, off_t offset, int whence, off_t *new_offset) {
		(void)fd;
		(void)offset;
		(void)whence;

		if(new_offset)
			*new_offset = 0;
		return ESPIPE;
	}

	int Sysdeps<Close>::operator()(int fd) {
		if(fd >= 0 && fd <= 2)
			return 0;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Ttyname>::operator()(int fd, char *buffer, size_t size) {
		(void)fd;
		(void)buffer;
		(void)size;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Ptsname>::operator()(int fd, char *buffer, size_t length) {
		(void)fd;
		(void)buffer;
		(void)length;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

/*#ifndef MLIBC_BUILDING_RTLD
	#define TTY_IOCTL_NAME 0x771101141113l
	#define TTY_NAME_MAX 32
	#define TTY_PREFIX "/dev/"


	int Sysdeps<Ttyname>::operator()(int fd, char * buffer, size_t size) {
		size_t prefixLen = strlen(TTY_PREFIX);
		if(size < TTY_NAME_MAX + prefixLen) {
			mlibc::panicLogger() << "ttyname size too small" << frg::endlog;
			__builtin_unreachable();
		}

		strcpy(buffer, TTY_PREFIX);

		int res;
		return sysdep<Ioctl>(fd, TTY_IOCTL_NAME, (void *)(buffer + prefixLen), &res);
	}
#endif

#ifndef MLIBC_BUILDING_RTLD

	int Sysdeps<GetEntropy>::operator()(void *buffer, size_t length) {
		int fd;
		int error = sysdep<Open>("/dev/urandom", O_RDONLY, 0, &fd);
		if(error)
			mlibc::panicLogger() << "/dev/urandom open error " << strerror(error) << frg::endlog;

		ssize_t bytes;
		error = sysdep<Read>(fd, buffer, length, &bytes);
		if(error) {
			mlibc::infoLogger() << "/dev/urandom read error " << strerror(error) << frg::endlog;
			return error;
		}

		sysdep<Close>(fd);
		return 0;
	}
#endif*/

/*#ifndef MLIBC_BUILDING_RTLD
	extern "C" void __mlibc_restorer();

	int Sysdeps<Sigaction>::operator()(int sig, const struct sigaction *__restrict act,
		struct sigaction *__restrict oldact) {
		long ret;

		struct sigaction newAction;
		if(act)
			memcpy(&newAction, act, sizeof(struct sigaction));

		if(act && (newAction.sa_flags & SA_RESTORER) == 0) {
			newAction.sa_restorer = __mlibc_restorer;
			newAction.sa_flags |= SA_RESTORER;
		}

		return syscall(SYSCALL_SIGACTION, &ret, sig, act ? (uint64_t)&newAction : 0, (uint64_t)oldact);
	}

	int Sysdeps<Ptsname>::operator()(int fd, char *buffer, size_t length) {
		int index;
		int tmp;
		if(int e = sysdep<Ioctl>(fd, TIOCGPTN, &index, &tmp); e)
			return e;

		if((size_t)snprintf(buffer, length, "/dev/pts/%d", index) >= length) {
			return ERANGE;
		}

		return 0;
	}
#endif*/
} // namespace mlibc