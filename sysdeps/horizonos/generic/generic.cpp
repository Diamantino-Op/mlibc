#include <bits/ensure.h>
#include <mlibc/debug.hpp>
#include <mlibc/all-sysdeps.hpp>
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

namespace mlibc {
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

	int Sysdeps<Sleep>::operator()(time_t *secs, long *nanos) {
		/*struct timespec ts;
		ts.tv_sec = *secs;
		ts.tv_nsec = *nanos;
		long ret;
		long err = syscall(SYSCALL_NANOSLEEP, &ret, (uintptr_t)&ts, (uintptr_t)&ts);
		*secs = ts.tv_sec;
		*nanos = ts.tv_nsec;
		return err;*/

		(void)secs;
		(void)nanos;
		return 0;
	}

	pid_t Sysdeps<GetPid>::operator()() {
		return 0;
	}

	int Sysdeps<Kill>::operator()(pid_t pid, int signal) {
		(void)pid;
		(void)signal;
		return 0;
	}

#ifndef MLIBC_BUILDING_RTLD

	[[noreturn]] void Sysdeps<ThreadExit>::operator()() {
		syscall(SYSCALL_THREADEXIT, nullptr);
		__builtin_unreachable();
	}

	extern "C" void __mlibc_thread_entry();

	int Sysdeps<Clone>::operator()(void *tcb, pid_t *pid_out, void *stack) {
		(void)tcb;
		long ret;
		long err = syscall(SYSCALL_NEWTHREAD, &ret, (uintptr_t)__mlibc_thread_entry, (uintptr_t)stack);
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

// Stubs
#ifndef MLIBC_BUILDING_RTLD
	extern "C" void __mlibc_restorer();

	int Sysdeps<Sigaction>::operator()(int sig, const struct sigaction *__restrict act,
		struct sigaction *__restrict oldact) {
		(void)sig;
		(void)act;
		(void)oldact;
		return 0;
	}
#endif

	int Sysdeps<SetGroups>::operator()(size_t size, const gid_t *list) {
		(void)size;
		(void)list;
		return 0;
	}

	int Sysdeps<GetGroups>::operator()(size_t size, gid_t *list, int *ret) {
		(void)size;
		(void)list;
		*ret = 0;
		return 0;
	}

	int Sysdeps<GetSockopt>::operator()(int fd, int layer, int number, void *__restrict buffer, socklen_t *__restrict size) {
		(void)fd;
		(void)layer;
		(void)number;
		(void)buffer;
		(void)size;
		return 0;
	}

	int Sysdeps<InetConfigured>::operator()(bool *ipv4, bool *ipv6) {
		(void)ipv4;
		(void)ipv6;
		return 0;
	}

	int Sysdeps<Readv>::operator()(int fd, const struct iovec *iovs, int iovc, ssize_t *bytes_read) {
		(void)fd;
		(void)iovs;
		(void)iovc;
		(void)bytes_read;
		return 0;
	}

	int Sysdeps<Writev>::operator()(int fd, const struct iovec *iovs, int iovc, ssize_t *bytes_written) {
		(void)fd;
		(void)iovs;
		(void)iovc;
		(void)bytes_written;
		return 0;
	}

	int Sysdeps<Flock>::operator()(int fd, int options) {
		(void)fd;
		(void)options;
		return 0;
	}

	int Sysdeps<Nice>::operator()(int nice, int *ret) {
		(void)nice;
		*ret = 0;
		return 0;
	}

	int Sysdeps<Shutdown>::operator()(int sockfd, int how) {
		(void)sockfd;
		(void)how;
		return 0;
	}

	int Sysdeps<Sigpending>::operator()(sigset_t *set) {
		(void)set;
		return 0;
	}

	int Sysdeps<Sigtimedwait>::operator()(const sigset_t *__restrict set, siginfo_t *__restrict info, const struct timespec *__restrict timeout, int *out_signal) {
		(void)set;
		(void)info;
		(void)timeout;
		(void)out_signal;
		return 0;
	}

	int Sysdeps<Sigsuspend>::operator()(const sigset_t *set) {
		(void)set;
		return 0;
	}

	int Sysdeps<SetUid>::operator()(uid_t id) {
		(void)id;
		return 0;
	}

	int Sysdeps<SetGid>::operator()(gid_t id) {
		(void)id;
		return 0;
	}

	int Sysdeps<SetEuid>::operator()(uid_t id) {
		(void)id;
		return 0;
	}

	int Sysdeps<SetEgid>::operator()(gid_t id) {
		(void)id;
		return 0;
	}

	uid_t Sysdeps<GetUid>::operator()() {
		return 0;
	}

	uid_t Sysdeps<GetEuid>::operator()() {
		return 0;
	}

	gid_t Sysdeps<GetGid>::operator()() {
		return 0;
	}

	gid_t Sysdeps<GetEgid>::operator()() {
		return 0;
	}

	int Sysdeps<SetResuid>::operator()(uid_t _ruid, uid_t _euid, uid_t _suid) {
		(void)_ruid;
		(void)_euid;
		(void)_suid;
		return 0;
	}

	int Sysdeps<SetResgid>::operator()(gid_t _rgid, gid_t _egid, gid_t _sgid) {
		(void)_rgid;
		(void)_egid;
		(void)_sgid;
		return 0;
	}

	int Sysdeps<GetResuid>::operator()(uid_t *ruid, uid_t *euid, uid_t *suid) {
		(void)ruid;
		(void)euid;
		(void)suid;
		return 0;
	}

	int Sysdeps<GetResgid>::operator()(gid_t *rgid, gid_t *egid, gid_t *sgid) {
		(void)rgid;
		(void)egid;
		(void)sgid;
		return 0;
	}

	int Sysdeps<Mknodat>::operator()(int dirfd, const char *path, int mode, int dev) {
		(void)dirfd;
		(void)path;
		(void)mode;
		(void)dev;
		return 0;
	}

	int Sysdeps<Mkfifoat>::operator()(int dirfd, const char *path, mode_t mode) {
		(void)dirfd;
		(void)path;
		(void)mode;
		return 0;
	}

	int Sysdeps<Pread>::operator()(int fd, void *buf, size_t n, off_t off, ssize_t *bytes_read) {
		(void)fd;
		(void)buf;
		(void)n;
		(void)off;
		(void)bytes_read;
		return 0;
	}

	int Sysdeps<Pwrite>::operator()(int fd, const void *buf, size_t n, off_t off, ssize_t *bytes_written) {
		(void)fd;
		(void)buf;
		(void)n;
		(void)off;
		(void)bytes_written;
		return 0;
	}

	int Sysdeps<Chroot>::operator()(const char *path) {
		(void)path;
		return 0;
	}

	int Sysdeps<Peername>::operator()(int fd, struct sockaddr *addr_ptr, socklen_t max_addr_length, socklen_t *actual_length) {
		(void)fd;
		(void)addr_ptr;
		(void)max_addr_length;
		(void)actual_length;
		return 0;
	}

	int Sysdeps<Sockname>::operator()(int fd, struct sockaddr *addr_ptr, socklen_t max_addr_length, socklen_t *actual_length) {
		(void)fd;
		(void)addr_ptr;
		(void)max_addr_length;
		(void)actual_length;
		return 0;
	}

	int Sysdeps<Socketpair>::operator()(int domain, int type_and_flags, int proto, int *fds) {
		(void)domain;
		(void)type_and_flags;
		(void)proto;
		(void)fds;
		return 0;
	}

	int Sysdeps<GetItimer>::operator()(int which, struct itimerval *curr_value) {
		(void)which;
		(void)curr_value;
		return 0;
	}

	int Sysdeps<SetItimer>::operator()(int which, const struct itimerval *new_value, struct itimerval *old_value) {
		(void)which;
		(void)new_value;
		(void)old_value;
		return 0;
	}

	int Sysdeps<Fsync>::operator()(int fd) {
		(void)fd;
		return 0;
	}

	int Sysdeps<Fdatasync>::operator()(int fd) {
		(void)fd;
		return 0;
	}

	pid_t Sysdeps<GetPpid>::operator()() {
		return 0;
	}

	int Sysdeps<GetSid>::operator()(pid_t pid, pid_t *pgid) {
		(void)pid;
		(void)pgid;
		return 0;
	}

	int Sysdeps<GetPgid>::operator()(pid_t pid, pid_t *pgid) {
		(void)pid;
		(void)pgid;
		return 0;
	}

	int Sysdeps<GetHostname>::operator()(char *buffer, size_t bufsize) {
		(void)buffer;
		(void)bufsize;
		return 0;
	}

	int Sysdeps<SetHostname>::operator()(const char *buffer, size_t bufsize) {
		(void)buffer;
		(void)bufsize;
		return 0;
	}

	int Sysdeps<Uname>::operator()(struct utsname *buf) {
		(void)buf;
		return 0;
	}

	void Sysdeps<Sync>::operator()() {
	}

	int Sysdeps<Sigaltstack>::operator()(const stack_t *ss, stack_t *oss) {
		(void)ss;
		(void)oss;
		return 0;
	}

	int Sysdeps<SetPgid>::operator()(pid_t pid, pid_t pgid) {
		(void)pid;
		(void)pgid;
		return 0;
	}

	int Sysdeps<SetSid>::operator()(pid_t *out) {
		(void)out;
		return 0;
	}

	int Sysdeps<Listen>::operator()(int fd, int backlog) {
		(void)fd;
		(void)backlog;
		return 0;
	}

	int Sysdeps<Accept>::operator()(int fd, int *newfd, struct sockaddr *addr_ptr, socklen_t *addr_length, int flags) {
		(void)fd;
		(void)newfd;
		(void)addr_ptr;
		(void)addr_length;
		(void)flags;
		return 0;
	}

	int Sysdeps<Connect>::operator()(int fd, const struct sockaddr *addr_ptr, socklen_t addr_length) {
		(void)fd;
		(void)addr_ptr;
		(void)addr_length;
		return 0;
	}

	int Sysdeps<MsgRecv>::operator()(int fd, struct msghdr *hdr, int flags, ssize_t *length) {
		(void)fd;
		(void)hdr;
		(void)flags;
		(void)length;
		return 0;
	}

	int Sysdeps<SetSockopt>::operator()(int fd, int layer, int number, const void *buffer, socklen_t size) {
		(void)fd;
		(void)layer;
		(void)number;
		(void)buffer;
		(void)size;
		return 0;
	}

	int Sysdeps<MsgSend>::operator()(int fd, const struct msghdr *hdr, int flags, ssize_t *length) {
		(void)fd;
		(void)hdr;
		(void)flags;
		(void)length;
		return 0;
	}

	int Sysdeps<Bind>::operator()(int fd, const struct sockaddr *addr_ptr, socklen_t addr_length) {
		(void)fd;
		(void)addr_ptr;
		(void)addr_length;
		return 0;
	}

	int Sysdeps<Socket>::operator()(int family, int type, int protocol, int *fd) {
		(void)family;
		(void)type;
		(void)protocol;
		(void)fd;
		return 0;
	}

	int Sysdeps<Utimensat>::operator()(int dirfd, const char *pathname, const struct timespec times[2], int flags) {
		(void)dirfd;
		(void)pathname;
		(void)times;
		(void)flags;
		return 0;
	}

	int Sysdeps<Fchownat>::operator()(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags) {
		(void)dirfd;
		(void)pathname;
		(void)owner;
		(void)group;
		(void)flags;
		return 0;
	}

	int Sysdeps<Ftruncate>::operator()(int fd, size_t size) {
		(void)fd;
		(void)size;
		return 0;
	}

	int Sysdeps<Tcgetattr>::operator()(int fd, struct termios *attr) {
		(void)fd;
		(void)attr;
		return 0;
	}

	int Sysdeps<Tcsetattr>::operator()(int fd, int act, const struct termios *attr) {
		(void)fd;
		(void)act;
		(void)attr;
		return 0;
	}

	int Sysdeps<Poll>::operator()(struct pollfd *fds, nfds_t count, int timeout, int *num_events) {
		(void)fds;
		(void)count;
		(void)timeout;
		(void)num_events;
		return 0;
	}

	int Sysdeps<Ppoll>::operator()(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout, const sigset_t *sigmask, int *num_events) {
		(void)fds;
		(void)nfds;
		(void)timeout;
		(void)sigmask;
		(void)num_events;
		return 0;
	}

	int Sysdeps<Pselect>::operator()(int num_fds, fd_set *read_set, fd_set *write_set, fd_set *except_set, const struct timespec *timeout, const sigset_t *sigmask, int *num_events) {
		(void)num_fds;
		(void)read_set;
		(void)write_set;
		(void)except_set;
		(void)timeout;
		(void)sigmask;
		(void)num_events;
		return 0;
	}

	int Sysdeps<Umask>::operator()(mode_t mode, mode_t *old) {
		(void)mode;
		(void)old;
		return 0;
	}

	int Sysdeps<Fchmod>::operator()(int fd, mode_t mode) {
		(void)fd;
		(void)mode;
		return 0;
	}

	int Sysdeps<Fchmodat>::operator()(int fd, const char *pathname, mode_t mode, int flags) {
		(void)fd;
		(void)pathname;
		(void)mode;
		(void)flags;
		return 0;
	}

	int Sysdeps<Chmod>::operator()(const char *pathname, mode_t mode) {
		(void)pathname;
		(void)mode;
		return 0;
	}

	int Sysdeps<Readlinkat>::operator()(int dirfd, const char *path, void *buffer, size_t max_size, ssize_t *length) {
		(void)dirfd;
		(void)path;
		(void)buffer;
		(void)max_size;
		(void)length;
		return 0;
	}

	int Sysdeps<Readlink>::operator()(const char *path, void *buffer, size_t max_size, ssize_t *length) {
		(void)path;
		(void)buffer;
		(void)max_size;
		(void)length;
		return 0;
	}

	int Sysdeps<Linkat>::operator()(int olddirfd, const char *old_path, int newdirfd, const char *new_path, int flags) {
		(void)olddirfd;
		(void)old_path;
		(void)newdirfd;
		(void)new_path;
		(void)flags;
		return 0;
	}

	int Sysdeps<Link>::operator()(const char *old_path, const char *new_path) {
		(void)old_path;
		(void)new_path;
		return 0;
	}

	int Sysdeps<Symlinkat>::operator()(const char *target_path, int dirfd, const char *link_path) {
		(void)target_path;
		(void)dirfd;
		(void)link_path;
		return 0;
	}

	int Sysdeps<Symlink>::operator()(const char *target_path, const char *link_path) {
		(void)target_path;
		(void)link_path;
		return 0;
	}

	int Sysdeps<Mkdirat>::operator()(int dirfd, const char *path, mode_t mode) {
		(void)dirfd;
		(void)path;
		(void)mode;
		return 0;
	}

	int Sysdeps<Mkdir>::operator()(const char *path, mode_t mode) {
		(void)path;
		(void)mode;
		return 0;
	}

	int Sysdeps<Faccessat>::operator()(int dirfd, const char *pathname, int mode, int flags) {
		(void)dirfd;
		(void)pathname;
		(void)mode;
		(void)flags;
		return 0;
	}

	int Sysdeps<Access>::operator()(const char *path, int mode) {
		(void)path;
		(void)mode;
		return 0;
	}

	int Sysdeps<Pipe>::operator()(int *fds, int flags) {
		(void)fds;
		(void)flags;
		return 0;
	}

	int Sysdeps<Chdir>::operator()(const char *path) {
		(void)path;
		return 0;
	}

	int Sysdeps<Fchdir>::operator()(int fd) {
		(void)fd;
		return 0;
	}

	int Sysdeps<Dup>::operator()(int fd, int flags, int *newfd) {
		(void)fd;
		(void)flags;
		(void)newfd;
		return 0;
	}

	int Sysdeps<Execve>::operator()(const char *path, char *const argv[], char *const envp[]) {
		(void)path;
		(void)argv;
		(void)envp;
		return 0;
	}

	int Sysdeps<OpenDir>::operator()(const char *path, int *handle) {
		return sysdep<Open>(path, O_DIRECTORY, 0, handle);
	}

	int Sysdeps<ReadEntries>::operator()(int handle, void *buffer, size_t max_size, size_t *bytes_read) {
		(void)handle;
		(void)buffer;
		(void)max_size;
		(void)bytes_read;
		return 0;
	}

	int Sysdeps<Sigprocmask>::operator()(int how, const sigset_t *__restrict set, sigset_t *__restrict retrieve) {
		(void)how;
		(void)set;
		(void)retrieve;
		return 0;
	}

	int Sysdeps<Stat>::operator()(fsfd_target fsfdt, int fd, const char *path, int flags, struct stat *statbuf) {
		(void)fsfdt;
		(void)fd;
		(void)path;
		(void)flags;
		(void)statbuf;
		return 0;
	}

	int Sysdeps<Rmdir>::operator()(const char *path) {
		return sysdep<Unlinkat>(AT_FDCWD, path, AT_REMOVEDIR);
	}

	int Sysdeps<Unlinkat>::operator()(int fd, const char *path, int flags) {
		(void)fd;
		(void)path;
		(void)flags;
		return 0;
	}

	int Sysdeps<Rename>::operator()(const char *path, const char *new_path) {
		return sysdep<Renameat>(AT_FDCWD, path, AT_FDCWD, new_path);
	}

	int Sysdeps<Renameat>::operator()(int olddirfd, const char *old_path, int newdirfd, const char *new_path) {
		(void)olddirfd;
		(void)old_path;
		(void)newdirfd;
		(void)new_path;
		return 0;
	}

	int Sysdeps<Isatty>::operator()(int fd) {
		(void)fd;
		return 0;
	}

	int Sysdeps<Fcntl>::operator()(int fd, int request, va_list args, int *result) {
		(void)fd;
		(void)request;
		(void)args;
		(void)result;
		return 0;
	}

	int Sysdeps<Openat>::operator()(int dirfd, const char *path, int flags, mode_t mode, int *fd) {
		(void)dirfd;
		(void)path;
		(void)flags;
		(void)mode;
		(void)fd;
		return 0;
	}

	int Sysdeps<Fork>::operator()(pid_t *pid) {
		(void)pid;
		return 0;
	}

	int Sysdeps<Waitpid>::operator()(pid_t pid, int *status, int flags, struct rusage *ru, pid_t *ret_pid) {
		(void)pid;
		(void)status;
		(void)flags;
		(void)ru;
		(void)ret_pid;
		return 0;
	}

	int Sysdeps<Dup2>::operator()(int fd, int flags, int newfd) {
		(void)fd;
		(void)flags;
		(void)newfd;
		return 0;
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
		return 0;
	}

	int Sysdeps<Ioctl>::operator()(int fd, unsigned long request, void *arg, int *result) {
		(void)fd;
		(void)request;
		(void)arg;
		(void)result;
		return 0;
	}

	int Sysdeps<VmProtect>::operator()(void *pointer, size_t size, int prot) {
		(void)pointer;
		(void)size;
		(void)prot;
		return 0;
	}

	int Sysdeps<FutexWait>::operator()(int *pointer, int expected, const struct timespec *time) {
		(void)pointer;
		(void)expected;
		(void)time;
		return 0;
	}

	int Sysdeps<FutexWake>::operator()(int *pointer, bool all) {
		(void)pointer;
		(void)all;
		return 0;
	}

	int Sysdeps<Open>::operator()(const char *pathname, int flags, mode_t mode, int *fd) {
		(void)pathname;
		(void)flags;
		(void)mode;

		if(fd)
			*fd = -1;
		return ENOSYS;
	};

	int Sysdeps<Read>::operator()(int fd, void *buff, size_t count, ssize_t *bytes_read) {
		(void)fd;
		(void)buff;
		(void)count;

		if(bytes_read)
			*bytes_read = 0;
		return ENOSYS;
	}

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

		return ENOSYS;
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
		return ENOSYS;
	}

	int Sysdeps<Ttyname>::operator()(int fd, char *buffer, size_t size) {
		(void)fd;
		(void)buffer;
		(void)size;
		return 0;
	}

	int Sysdeps<Ptsname>::operator()(int fd, char *buffer, size_t length) {
		(void)fd;
		(void)buffer;
		(void)length;
		return 0;
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

/*#ifndef MLIBC_BUILDING_RTLD
	int Sysdeps<Pselect>::operator()(int num_fds, fd_set *read_set, fd_set *write_set, fd_set *except_set, const struct timespec *timeout, const sigset_t *sigmask, int *num_events) {
		pollfd *fds = (pollfd *)malloc(num_fds * sizeof(pollfd));

		if(fds == NULL)
			return ENOMEM;

		int actual_count = 0;

		for(int fd = 0; fd < num_fds; ++fd) {
			short events = 0;
			if(read_set && FD_ISSET(fd, read_set)) {
				events |= POLLIN;
			}

			if(write_set && FD_ISSET(fd, write_set)) {
				events |= POLLOUT;
			}

			if(except_set && FD_ISSET(fd, except_set)) {
				events |= POLLPRI;
			}

			if(events) {
				fds[actual_count].fd = fd;
				fds[actual_count].events = events;
				fds[actual_count].revents = 0;
				actual_count++;
			}
		}

		int num;
		int err = sysdep<Ppoll>(fds, actual_count, timeout, sigmask, &num);

		if(err) {
			free(fds);
			return err;
		}

		#define READ_SET_POLLSTUFF (POLLIN | POLLHUP | POLLERR)
		#define WRITE_SET_POLLSTUFF (POLLOUT | POLLERR)
		#define EXCEPT_SET_POLLSTUFF (POLLPRI)

		int return_count = 0;
		for(int fd = 0; fd < actual_count; ++fd) {
			int events = fds[fd].events;
			if((events & POLLIN) && (fds[fd].revents & READ_SET_POLLSTUFF) == 0) {
				FD_CLR(fds[fd].fd, read_set);
				events &= ~POLLIN;
			}

			if((events & POLLOUT) && (fds[fd].revents & WRITE_SET_POLLSTUFF) == 0) {
				FD_CLR(fds[fd].fd, write_set);
				events &= ~POLLOUT;
			}

			if((events & POLLPRI) && (fds[fd].revents & EXCEPT_SET_POLLSTUFF) == 0) {
				FD_CLR(fds[fd].fd, except_set);
				events &= ~POLLPRI;
			}

			if(events)
				return_count++;
		}
		*num_events = return_count;
		free(fds);
		return 0;
	}
#endif*/
} // namespace mlibc
