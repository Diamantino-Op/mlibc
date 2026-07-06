#include <bits/ensure.h>
#include <mlibc/debug.hpp>
#include <mlibc/all-sysdeps.hpp>
#include "generic.h"

#include <asm/ioctls.h>
#include <dirent.h>
#include <errno.h>
#include <horizonos/archctl.h>
#include <horizonos/syscall.h>
#include <mlibc/tcb.hpp>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>

using namespace horizonos;

int register_horizonos_port(long *ret, uint64_t preferredPort) {
	return syscall(SYSCALL_PORTREGISTER, ret, preferredPort);
}

int send_horizonos_message(uint64_t sendPort, uint64_t port, const struct hos_msg *hdr) {
	return syscall(SYSCALL_SENDMSG, nullptr, sendPort, port, reinterpret_cast<uint64_t>(hdr));
}

int receive_horizonos_message(uint64_t port, struct hos_msg *hdr, filter_options *options) {
	return syscall(SYSCALL_RECVMSG, nullptr, port, reinterpret_cast<uint64_t>(hdr), reinterpret_cast<uint64_t>(options));
}

int is_thread_alive(int tid, bool *alive) {
	long ret;
	int err = syscall(SYSCALL_ISTHREADALIVE, &ret, tid);
	*alive = ret != 0;

	return err;
}

int munmap_extra(void *ptr, size_t len, bool freePage) {
	return syscall(SYSCALL_MUNMAP, nullptr, reinterpret_cast<uintptr_t>(ptr), len, freePage);
}

int mmap_phys(uint64_t physAddr, uint64_t len, uint64_t *retAddr, bool isHhdm, MMapCacheMode cacheMode) {
	return syscall(SYSCALL_MMAPPHYS, reinterpret_cast<long *>(retAddr), physAddr, len, static_cast<uint64_t>(isHhdm), static_cast<uint64_t>(cacheMode));
}

int get_rsdp(uint64_t *rsdpAddr) {
	return syscall(SYSCALL_GETRSDP, reinterpret_cast<long *>(rsdpAddr));
}

int install_irq_handler(uint64_t irq, uint64_t port) {
	return syscall(SYSCALL_INSTALLIRQHANDLER, nullptr, irq, port);
}

int uninstall_irq_handler(uint64_t irq) {
	return syscall(SYSCALL_UNINSTALLIRQHANDLER, nullptr, irq);
}

int get_irq_mode(long *mode) {
	return syscall(SYSCALL_GETIRQMODE, mode);
}

int set_int_status(bool status) {
	return syscall(SYSCALL_SETINTSTATUS, nullptr, status);
}

int allocIntVec(uint8_t *vecOut, uint64_t port, uint64_t destCpu, bool isLapic) {
	return syscall(SYSCALL_ALLOC_INT_VEC, reinterpret_cast<long *>(vecOut), port, destCpu, static_cast<uint64_t>(isLapic));
}

int freeIntVec(uint8_t vec, uint64_t destCpu, bool isLapic) {
	return syscall(SYSCALL_FREE_INT_VEC, nullptr, vec, destCpu, static_cast<uint64_t>(isLapic));
}

int allocGsi(uint64_t *gsiOut, uint64_t port, uint64_t destCpu, bool isLapic) {
	return syscall(SYSCALL_ALLOC_GSI, reinterpret_cast<long *>(gsiOut), port, destCpu, static_cast<uint64_t>(isLapic));
}

int freeGsi(uint64_t gsi, uint64_t destCpu, bool isLapic) {
	return syscall(SYSCALL_FREE_GSI, nullptr, gsi, destCpu, static_cast<uint64_t>(isLapic));
}

int getCpuIds(HosCpuInfo *cpuIdOutArray, uint64_t cpuCount) {
	return syscall(SYSCALL_GET_CPU_IDS, nullptr, reinterpret_cast<uint64_t>(cpuIdOutArray), cpuCount);
}

int allocPhysPage(uint64_t *outAddr) {
	return syscall(SYSCALL_ALLOC_PHYS_PAGE, reinterpret_cast<long *>(outAddr));
}

int freePhysPage(uint64_t physPage) {
	return syscall(SYSCALL_FREE_PHYS_PAGE, nullptr, physPage);
}

int registerKernelEventHandler(uint64_t port, uint64_t eventMask) {
	return syscall(SYSCALL_REGISTER_EVENT_HANDLER, nullptr, port, eventMask);
}

namespace {
	constexpr size_t VFS_CLIENT_PORT_CACHE_SIZE = 128;

	struct HorizonFd {
		bool used {};
		uint64_t handle {};
		uint8_t nodeType {};
		int flags {};
		char path[VFS_MAX_PATH_LENGTH] {};
	};

	constexpr int maxHorizonFds = 256;
	HorizonFd fdTable[maxHorizonFds] {};
	char currentDirectory[VFS_MAX_PATH_LENGTH] = "HorizonOS:/";
	mode_t currentUmask = 022;

	size_t boundedStringLength(const char *str, size_t maxLength) {
		size_t length = 0;

		while (length < maxLength and str[length] != '\0') {
			++length;
		}

		return length;
	}

	bool stringStartsWith(const char *str, const char *prefix) {
		for (size_t i = 0; prefix[i] != '\0'; ++i) {
			if (str[i] != prefix[i]) {
				return false;
			}
		}

		return true;
	}

	bool stringEquals(const char *a, const char *b) {
		size_t i = 0;

		for (;; ++i) {
			if (a[i] != b[i]) {
				return false;
			}

			if (a[i] == '\0') {
				return true;
			}
		}
	}

	bool stringContainsColon(const char *str) {
		for (size_t i = 0; str[i] != '\0'; ++i) {
			if (str[i] == ':') {
				return true;
			}
		}

		return false;
	}

	bool copyString(char *out, size_t outSize, const char *a, const char *b = nullptr, const char *c = nullptr) {
		size_t written = 0;

		const auto append = [&](const char *str) -> bool {
			for (size_t i = 0; str != nullptr and str[i] != '\0'; ++i) {
				if (written + 1 >= outSize) {
					return false;
				}

				out[written++] = str[i];
			}

			return true;
		};

		if (!append(a) or !append(b) or !append(c)) {
			return false;
		}

		out[written] = '\0';

		return true;
	}

	void fillVfsName(char *dst, const size_t dstSize, size_t &length, const char *name) {
		const size_t copyLen = boundedStringLength(name, dstSize - 1);

		memcpy(dst, name, copyLen);
		dst[copyLen] = '\0';
		length = copyLen + 1;
	}

	bool resolvePath(const char *path, char *out, size_t outSize) {
		if (path == nullptr or out == nullptr or outSize == 0) {
			return false;
		}

		if (stringContainsColon(path)) {
			return copyString(out, outSize, path);
		} else if (stringStartsWith(path, "/Devices") and (path[8] == '\0' or path[8] == '/')) {
			return copyString(out, outSize, "Devices:", path + 8);
		} else if (stringEquals(path, "/")) {
			return copyString(out, outSize, "HorizonOS:/");
		} else if (path[0] == '/') {
			return copyString(out, outSize, "HorizonOS:", path);
		} else {
			const size_t cwdLength = boundedStringLength(currentDirectory, VFS_MAX_PATH_LENGTH);

			if (cwdLength == 0) {
				return copyString(out, outSize, "HorizonOS:/", path);
			}

			if (currentDirectory[cwdLength - 1] == '/') {
				return copyString(out, outSize, currentDirectory, path);
			}

			return copyString(out, outSize, currentDirectory, "/", path);
		}
	}

	int allocFd(uint64_t handle, uint8_t nodeType, int flags, const char *path) {
		for (int fd = 3; fd < maxHorizonFds; ++fd) {
			if (!fdTable[fd].used) {
				fdTable[fd].used = true;
				fdTable[fd].handle = handle;
				fdTable[fd].nodeType = nodeType;
				fdTable[fd].flags = flags;
				copyString(fdTable[fd].path, sizeof(fdTable[fd].path), path);

				return fd;
			}
		}

		return -1;
	}

	bool fdValid(int fd) {
		return fd >= 0 and fd < maxHorizonFds and fdTable[fd].used;
	}

	bool backendHandleHasOtherFd(int fd) {
		const uint64_t handle = fdTable[fd].handle;

		for (int i = 3; i < maxHorizonFds; ++i) {
			if (i != fd and fdTable[i].used and fdTable[i].handle == handle) {
				return true;
			}
		}

		return false;
	}

	bool localFdIsSet(int fd, fd_set *set) {
		return fd >= 0 and fd < FD_SETSIZE and (set->fds_bits[fd / 8] & (1 << (fd % 8))) != 0;
	}

	void localFdClear(int fd, fd_set *set) {
		if (fd >= 0 and fd < FD_SETSIZE) {
			set->fds_bits[fd / 8] &= ~(1 << (fd % 8));
		}
	}

	uint64_t currentThreadVfsPort(uint64_t *ownerKey) {
		long tid = 0;
		long pid = 0;

		syscall(SYSCALL_GETTID, &tid);
		syscall(SYSCALL_GETPID, &pid);

		const uint64_t key = ((static_cast<uint64_t>(pid) & 0xffff) << 32) | (static_cast<uint64_t>(tid) & 0xffffffff);

		if (ownerKey != nullptr) {
			*ownerKey = key;
		}

		return VFS_CLIENT_PORT_BASE | key;
	}

	int ensureCurrentThreadVfsPort(uint64_t *port) {
		if (port == nullptr) {
			return EINVAL;
		}

		static uint64_t cachedClientKeys[VFS_CLIENT_PORT_CACHE_SIZE] {};
		static uint64_t cachedClientPorts[VFS_CLIENT_PORT_CACHE_SIZE] {};

		uint64_t ownerKey = 0;
		const uint64_t preferredPort = currentThreadVfsPort(&ownerKey);

		for (size_t i = 0; i < VFS_CLIENT_PORT_CACHE_SIZE; ++i) {
			const uint64_t cachedKey = __atomic_load_n(&cachedClientKeys[i], __ATOMIC_ACQUIRE);

			if (cachedKey != ownerKey) {
				continue;
			}

			const uint64_t cachedPort = __atomic_load_n(&cachedClientPorts[i], __ATOMIC_ACQUIRE);

			if (cachedPort != 0) {
				*port = cachedPort;
				return 0;
			}
		}

		long registeredPort = 0;
		const int err = register_horizonos_port(&registeredPort, preferredPort);

		if (err != 0 and err != EEXIST) {
			return err;
		}

		const size_t slot = ownerKey % VFS_CLIENT_PORT_CACHE_SIZE;

		__atomic_store_n(&cachedClientPorts[slot], preferredPort, __ATOMIC_RELEASE);
		__atomic_store_n(&cachedClientKeys[slot], ownerKey, __ATOMIC_RELEASE);

		*port = preferredPort;

		return 0;
	}

	int resolveVfsPort(uint64_t clientPort, uint64_t *port) {
		static uint64_t cachedVfsPort = 0;

		if (port == nullptr) {
			return EINVAL;
		}

		if (cachedVfsPort != 0) {
			*port = cachedVfsPort;
			return 0;
		}

		for (;;) {
			GetMsgData req {};
			GetReplyMsgData reply {};
			fillVfsName(req.name, sizeof(req.name), req.nameLength, "Vfs");

			hos_msg msg {};
			msg.type = GET_MSG_TYPE;
			msg.port = NAME_REGISTRY_PORT;
			msg.buffer = &req;
			msg.length = sizeof(req);

			const int sendErr = send_horizonos_message(clientPort, NAME_REGISTRY_PORT, &msg);

			if (sendErr != 0) {
				return sendErr;
			}

			hos_msg recv {};
			recv.buffer = &reply;
			recv.length = sizeof(reply);

			uint64_t whiteListType = REPLY_GET_MSG_TYPE;
			filter_options filter {};
			filter.whiteListTypes = &whiteListType;
			filter.whiteListCount = 1;

			const int recvErr = receive_horizonos_message(clientPort, &recv, &filter);

			if (recvErr != 0) {
				return recvErr;
			}

			if (reply.port != 0) {
				cachedVfsPort = reply.port;
				*port = reply.port;
				return 0;
			}

			long secs = 0;
			long nanos = 10'000'000;
			syscall(SYSCALL_NANOSLEEP, nullptr, reinterpret_cast<uint64_t>(&secs), reinterpret_cast<uint64_t>(&nanos));
		}
	}

	int vfsStatusToErr(uint32_t status) {
		return status == 0 ? EIO : static_cast<int>(status);
	}

	uint32_t posixOpenFlagsToVfs(int flags) {
		uint32_t out = 0;

		if ((flags & O_ACCMODE) == O_WRONLY) {
			out |= VFS_OPEN_WRITE;
		} else if ((flags & O_ACCMODE) == O_RDWR) {
			out |= VFS_OPEN_READ | VFS_OPEN_WRITE;
		} else {
			out |= VFS_OPEN_READ;
		}

		if ((flags & O_CREAT) != 0) out |= VFS_OPEN_CREATE;
		if ((flags & O_APPEND) != 0) out |= VFS_OPEN_APPEND;
		if ((flags & O_TRUNC) != 0) out |= VFS_OPEN_TRUNCATE;
		if ((flags & O_EXCL) != 0) out |= VFS_OPEN_EXCLUSIVE;

		return out;
	}

	mode_t nodeTypeToMode(uint8_t nodeType) {
		switch (nodeType) {
			case VFS_NODE_DIRECTORY:
				return S_IFDIR | 0755;
			case VFS_NODE_SYMLINK:
				return S_IFLNK | 0777;
			case VFS_NODE_DEVICE:
				return S_IFCHR | 0666;
			case VFS_NODE_FILE:
			default:
				return S_IFREG | 0644;
		}
	}

	unsigned char nodeTypeToDirentType(uint8_t nodeType) {
		switch (nodeType) {
			case VFS_NODE_DIRECTORY:
				return DT_DIR;
			case VFS_NODE_SYMLINK:
				return DT_LNK;
			case VFS_NODE_FILE:
				return DT_REG;
			default:
				return DT_UNKNOWN;
		}
	}
}

int sendVfsRequest(uint64_t requestType, const void *request, size_t requestLength, void *reply, size_t replyLength) {
	if (requestType == 0 or reply == nullptr or replyLength == 0 or (requestLength != 0 and request == nullptr)) {
		return EINVAL;
	}

	uint64_t clientPort = 0;
	int err = ensureCurrentThreadVfsPort(&clientPort);

	if (err != 0) {
		return err;
	}

	uint64_t vfsPort = 0;
	err = resolveVfsPort(clientPort, &vfsPort);

	if (err != 0) {
		return err;
	}

	hos_msg send {};
	send.type = requestType;
	send.port = vfsPort;
	send.buffer = const_cast<void *>(request);
	send.length = requestLength;

	err = send_horizonos_message(clientPort, vfsPort, &send);

	if (err != 0) {
		return err;
	}

	hos_msg recv {};
	recv.buffer = reply;
	recv.length = replyLength;

	uint64_t whiteListType = requestType + 1;
	filter_options filter {};
	filter.whiteListTypes = &whiteListType;
	filter.whiteListCount = 1;

	return receive_horizonos_message(clientPort, &recv, &filter);
}

namespace mlibc {
	[[noreturn]] static void panic_unimplemented_sysdep(const char *name) {
		mlibc::panicLogger() << "mlibc: unimplemented sysdep " << name << frg::endlog;
		__builtin_trap();
	}

	// Print

	void Sysdeps<LibcLog>::operator()(const char *message) {
		syscall(SYSCALL_PRINT, nullptr, reinterpret_cast<uint64_t>(message));
	}

	[[noreturn]] void Sysdeps<LibcPanic>::operator()() {
		sysdep<LibcLog>("mlibc: panic");
		sysdep<Exit>(1);
	}

	// Map

	int Sysdeps<VmMap>::operator()(void *hint, size_t size, int prot, int flags, int fd, off_t offset, void **window) {
		long ret;
		long err = syscall(SYSCALL_MMAP, &ret, reinterpret_cast<uint64_t>(hint), size, prot, flags, fd, offset);

		*window = (void *)ret;

		return err;
	}

	// Unmap

	int Sysdeps<VmUnmap>::operator()(void *pointer, size_t size) {
		return syscall(SYSCALL_MUNMAP, nullptr, reinterpret_cast<uintptr_t>(pointer), size, true);
	}

	// Protect

	int Sysdeps<VmProtect>::operator()(void *pointer, size_t size, int prot) {
		return syscall(SYSCALL_MPROTECT, nullptr, reinterpret_cast<uint64_t>(pointer), size, prot);
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
		return syscall(SYSCALL_ARCHCTL, nullptr, ARCH_CTL_SET_FSBASE, reinterpret_cast<uint64_t>(pointer));
	}

	// Exit

	[[noreturn]] void Sysdeps<Exit>::operator()(int status) {
		syscall(SYSCALL_EXIT, nullptr, status);
		__builtin_unreachable();
	}

	// Get Clock

	int Sysdeps<ClockGet>::operator()(const int clock, time_t *secs, long *nanos) {
		if (secs == nullptr || nanos == nullptr) {
			return EFAULT;
		}

		int err = syscall(SYSCALL_CLOCKGET, nullptr, clock, reinterpret_cast<uint64_t>(secs), reinterpret_cast<uint64_t>(nanos));

		if (err != 0) {
			*secs = -1;
			*nanos = -1;

			return err;
		}

		return 0;
	}

	// Sysinfo

	int Sysdeps<Sysinfo>::operator()(struct sysinfo *info) {
		return syscall(SYSCALL_SYSINFO, nullptr, reinterpret_cast<uint64_t>(info));
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
		return syscall(SYSCALL_KILLTHREAD, nullptr, pid, tid, sig);
	}

	// Pause

	int Sysdeps<Pause>::operator()() {
		return syscall(SYSCALL_PAUSE, nullptr);
	}

	// Messages

	int Sysdeps<Sleep>::operator()(time_t *secs, long *nanos) {
		long err = syscall(SYSCALL_NANOSLEEP, nullptr, *secs, *nanos);

		if (err == 0) {
			*secs = 0;
			*nanos = 0;
		}

		return err;
	}

	pid_t Sysdeps<GetPid>::operator()() {
		long ret;
		syscall(SYSCALL_GETPID, &ret);

		return ret;
	}

	int Sysdeps<Kill>::operator()(pid_t pid, int signal) {
		return syscall(SYSCALL_KILL, nullptr, pid, signal);
	}

	// Futex

	#define FUTEX_WAIT 0
	#define FUTEX_WAKE 1

	int Sysdeps<FutexWait>::operator()(int *pointer, int expected, const struct timespec *time) {
		return syscall(SYSCALL_FUTEX, nullptr, (uint64_t)pointer, FUTEX_WAIT, expected, (uint64_t)time);
	}

	int Sysdeps<FutexWake>::operator()(int *pointer, bool all) {
		return syscall(SYSCALL_FUTEX, nullptr, (uint64_t)pointer, FUTEX_WAKE, all ? INT_MAX : 1, 0);
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
		return sysdep<VmMap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0, pointer);
	}

	int Sysdeps<AnonFree>::operator()(void *pointer, size_t size) {
		size += 4096 - (size % 4096);
		return sysdep<VmUnmap>(pointer, size);
	}

#ifndef MLIBC_BUILDING_RTLD
	extern "C" void __mlibc_restorer();

	int Sysdeps<Sigaction>::operator()(int sig, const struct sigaction *__restrict act, struct sigaction *__restrict oldact) {
			struct sigaction newAction;
			if(act)
				memcpy(&newAction, act, sizeof(struct sigaction));

			if(act && (newAction.sa_flags & SA_RESTORER) == 0) {
				newAction.sa_restorer = __mlibc_restorer;
				newAction.sa_flags |= SA_RESTORER;
			}

			return syscall(SYSCALL_SIGACTION, nullptr, sig, act ? (uint64_t)&newAction : 0, (uint64_t)oldact);
	}
#endif

	// IsaTTY

	int Sysdeps<Isatty>::operator()(int fd) {
		if (fd == 1 or fd == 2) {
			return 0;
		}

		return syscall(SYSCALL_ISATTY, nullptr, fd);
	}

	// IO

	int Sysdeps<Ioperm>::operator()(unsigned long int from, unsigned long int num, int state) {
		return syscall(SYSCALL_IOPERM, nullptr, from, num, state);
	}

	int Sysdeps<Iopl>::operator()(int level) {
		return syscall(SYSCALL_IOPL, nullptr, level);
	}

	// Affinity
	int Sysdeps<GetAffinity>::operator()(pid_t pid, size_t cpusetsize, cpu_set_t *mask) {
		return syscall(SYSCALL_GETAFFINITY, nullptr, pid, cpusetsize, reinterpret_cast<uint64_t>(mask));
	}

	int Sysdeps<GetThreadaffinity>::operator()(pid_t tid, size_t cpusetsize, cpu_set_t *mask) {
		return syscall(SYSCALL_GETAFFINITY, nullptr, tid, cpusetsize, reinterpret_cast<uint64_t>(mask));
	}

	int Sysdeps<SetAffinity>::operator()(pid_t pid, size_t cpusetsize, const cpu_set_t *mask) {
		return syscall(SYSCALL_SETAFFINITY, nullptr, pid, cpusetsize, reinterpret_cast<uint64_t>(mask));
	}

	int Sysdeps<SetThreadaffinity>::operator()(pid_t tid, size_t cpusetsize, const cpu_set_t *mask) {
		return syscall(SYSCALL_SETAFFINITY, nullptr, tid, cpusetsize, reinterpret_cast<uint64_t>(mask));
	}

	int Sysdeps<Sysconf>::operator()(int num, long *ret) {
		switch(num) {
			case _SC_OPEN_MAX: {
				struct rlimit ru;

				if (int e = sysdep<GetRlimit>(RLIMIT_NOFILE, &ru); e) {
					return e;
				}

				*ret = (ru.rlim_cur == RLIM_INFINITY) ? -1 : ru.rlim_cur;

				break;
			}

			case _SC_NPROCESSORS_CONF:
			case _SC_NPROCESSORS_ONLN: {
#ifndef MLIBC_BUILDING_RTLD
				cpu_set_t set;
				CPU_ZERO(&set);

				if (int e = sysdep<GetAffinity>(0, sizeof(set), &set); e) {
					return e;
				}

				*ret = CPU_COUNT(&set);
#endif

				//syscall(SYSCALL_GET_CPU_COUNT, ret);

				//mlibc::infoLogger() << "mlibc: ret val: " << *ret << frg::endlog;

				break;
			}

			case _SC_PHYS_PAGES: {
				struct sysinfo info;

				if (int e = sysdep<Sysinfo>(&info); e) {
					return
					 e;
				}

				unsigned unit = (info.mem_unit) ? info.mem_unit : 1;
				*ret = std::min(long((info.totalram * unit) / 0x1000), LONG_MAX); // 0x1000 = PAGE_SIZE

				break;
			}

			case _SC_CHILD_MAX: {
				struct rlimit ru;

				if (int e = sysdep<GetRlimit>(RLIMIT_NPROC, &ru); e) {
					return e;
				}

				*ret = (ru.rlim_cur == RLIM_INFINITY) ? -1 : ru.rlim_cur;

				break;
			}

			case _SC_LINE_MAX: {
				*ret = -1;

				break;
			}

			default: {
				return EINVAL;
			}
		}

		return 0;
	}

	// OS

	int Sysdeps<Shutdown>::operator()(int sockfd, int how) {
		(void)sockfd;
		(void)how;
		return ENOTSOCK;
	}

	// FileSystem

	int Sysdeps<Readv>::operator()(int fd, const struct iovec *iovs, int iovc, ssize_t *bytes_read) {
		if (iovs == nullptr or iovc < 0) {
			return EINVAL;
		}

		ssize_t total = 0;

		for (int i = 0; i < iovc; ++i) {
			ssize_t chunk = 0;
			const int err = sysdep<Read>(fd, iovs[i].iov_base, iovs[i].iov_len, &chunk);

			if (err != 0) {
				if (total != 0) {
					break;
				}

				return err;
			}

			total += chunk;

			if (static_cast<size_t>(chunk) != iovs[i].iov_len) {
				break;
			}
		}

		if (bytes_read != nullptr) {
			*bytes_read = total;
		}

		return 0;
	}

	int Sysdeps<Writev>::operator()(int fd, const struct iovec *iovs, int iovc, ssize_t *bytes_written) {
		if (iovs == nullptr or iovc < 0) {
			return EINVAL;
		}

		ssize_t total = 0;

		for (int i = 0; i < iovc; ++i) {
			ssize_t chunk = 0;
			const int err = sysdep<Write>(fd, iovs[i].iov_base, iovs[i].iov_len, &chunk);

			if (err != 0) {
				if (total != 0) {
					break;
				}

				return err;
			}

			total += chunk;

			if (static_cast<size_t>(chunk) != iovs[i].iov_len) {
				break;
			}
		}

		if (bytes_written != nullptr) {
			*bytes_written = total;
		}

		return 0;
	}

	int Sysdeps<Mknodat>::operator()(int dirfd, const char *path, int mode, int dev) {
		(void)dirfd;
		(void)path;
		(void)mode;
		(void)dev;
		return ENOSYS;
	}

	int Sysdeps<Mkfifoat>::operator()(int dirfd, const char *path, mode_t mode) {
		(void)dirfd;
		(void)path;
		(void)mode;
		return ENOSYS;
	}

	int Sysdeps<Pread>::operator()(int fd, void *buf, size_t n, off_t off, ssize_t *bytes_read) {
		off_t oldOffset = 0;
		off_t ignored = 0;
		int err = sysdep<Seek>(fd, 0, SEEK_CUR, &oldOffset);

		if (err != 0) {
			return err;
		}

		if ((err = sysdep<Seek>(fd, off, SEEK_SET, &ignored)) != 0) {
			return err;
		}

		err = sysdep<Read>(fd, buf, n, bytes_read);
		sysdep<Seek>(fd, oldOffset, SEEK_SET, &ignored);

		return err;
	}

	int Sysdeps<Pwrite>::operator()(int fd, const void *buf, size_t n, off_t off, ssize_t *bytes_written) {
		off_t oldOffset = 0;
		off_t ignored = 0;
		int err = sysdep<Seek>(fd, 0, SEEK_CUR, &oldOffset);

		if (err != 0) {
			return err;
		}

		if ((err = sysdep<Seek>(fd, off, SEEK_SET, &ignored)) != 0) {
			return err;
		}

		err = sysdep<Write>(fd, buf, n, bytes_written);
		sysdep<Seek>(fd, oldOffset, SEEK_SET, &ignored);

		return err;
	}

	int Sysdeps<Fsync>::operator()(int fd) {
		if (fd < 0 or fd >= maxHorizonFds or !fdTable[fd].used) {
			return EBADF;
		}

		VfsFsyncMsgData req {};
		VfsFsyncReplyMsgData reply {};
		req.handle = fdTable[fd].handle;

		const int err = sendVfsRequest(VFS_FSYNC_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		return reply.success ? 0 : vfsStatusToErr(reply.status);
	}

	int Sysdeps<Fdatasync>::operator()(int fd) {
		return sysdep<Fsync>(fd);
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
		if (fd < 0 or fd >= maxHorizonFds or !fdTable[fd].used) {
			return EBADF;
		}

		VfsHandleTruncateMsgData req {};
		VfsHandleTruncateReplyMsgData reply {};
		req.handle = fdTable[fd].handle;
		req.size = size;

		const int err = sendVfsRequest(VFS_HANDLE_TRUNCATE_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		return reply.success ? 0 : vfsStatusToErr(reply.status);
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
		(void)timeout;

		if (fds == nullptr or num_events == nullptr) {
			return EFAULT;
		}

		int ready = 0;

		for (nfds_t i = 0; i < count; ++i) {
			fds[i].revents = 0;

			if (fds[i].fd < 0) {
				continue;
			}

			if (fds[i].fd <= 2 or fdValid(fds[i].fd)) {
				fds[i].revents = fds[i].events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM);

				if (fds[i].revents == 0 and fds[i].events != 0) {
					fds[i].revents = POLLIN;
				}
			} else {
				fds[i].revents = POLLNVAL;
			}

			if (fds[i].revents != 0) {
				++ready;
			}
		}

		*num_events = ready;

		return 0;
	}

	int Sysdeps<Ppoll>::operator()(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout, const sigset_t *sigmask, int *num_events) {
		(void)timeout;
		(void)sigmask;
		return sysdep<Poll>(fds, nfds, 0, num_events);
	}

	int Sysdeps<Pselect>::operator()(int num_fds, fd_set *read_set, fd_set *write_set, fd_set *except_set, const struct timespec *timeout, const sigset_t *sigmask, int *num_events) {
		(void)timeout;
		(void)sigmask;

		if (num_events == nullptr) {
			return EFAULT;
		}

		int ready = 0;

		for (int fd = 0; fd < num_fds; ++fd) {
			const bool valid = fd <= 2 or fdValid(fd);

			if (read_set != nullptr and localFdIsSet(fd, read_set)) {
				if (valid) {
					++ready;
				} else {
					localFdClear(fd, read_set);
				}
			}

			if (write_set != nullptr and localFdIsSet(fd, write_set)) {
				if (valid) {
					++ready;
				} else {
					localFdClear(fd, write_set);
				}
			}
		}

		(void)except_set;
		*num_events = ready;

		return 0;
	}

	int Sysdeps<Umask>::operator()(mode_t mode, mode_t *old) {
		if (old != nullptr) {
			*old = currentUmask;
		}

		currentUmask = mode & 0777;

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

		if (buffer == nullptr or length == nullptr) {
			return EFAULT;
		}

		char resolved[VFS_MAX_PATH_LENGTH] {};

		if (!resolvePath(path, resolved, sizeof(resolved))) {
			return EINVAL;
		}

		VfsReadLinkMsgData req {};
		VfsReadLinkReplyMsgData reply {};
		fillVfsName(req.path, sizeof(req.path), req.pathLength, resolved);

		const int err = sendVfsRequest(VFS_READLINK_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		if (!reply.success) {
			return vfsStatusToErr(reply.status);
		}

		const size_t targetLength = reply.targetLength == 0 ? boundedStringLength(reply.target, sizeof(reply.target)) : reply.targetLength - 1;
		const size_t copyLength = std::min(max_size, targetLength);

		memcpy(buffer, reply.target, copyLength);
		*length = static_cast<ssize_t>(copyLength);

		return 0;
	}

	int Sysdeps<Readlink>::operator()(const char *path, void *buffer, size_t max_size, ssize_t *length) {
		return sysdep<Readlinkat>(AT_FDCWD, path, buffer, max_size, length);
	}

	int Sysdeps<Linkat>::operator()(int olddirfd, const char *old_path, int newdirfd, const char *new_path, int flags) {
		(void)olddirfd;
		(void)newdirfd;
		(void)flags;
		char resolvedOld[VFS_MAX_PATH_LENGTH] {};
		char resolvedNew[VFS_MAX_PATH_LENGTH] {};

		if (!resolvePath(old_path, resolvedOld, sizeof(resolvedOld)) or !resolvePath(new_path, resolvedNew, sizeof(resolvedNew))) {
			return EINVAL;
		}

		VfsLinkMsgData req {};
		VfsLinkReplyMsgData reply {};
		fillVfsName(req.oldPath, sizeof(req.oldPath), req.oldPathLength, resolvedOld);
		fillVfsName(req.newPath, sizeof(req.newPath), req.newPathLength, resolvedNew);

		const int err = sendVfsRequest(VFS_LINK_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		return reply.success ? 0 : vfsStatusToErr(reply.status);
	}

	int Sysdeps<Link>::operator()(const char *old_path, const char *new_path) {
		return sysdep<Linkat>(AT_FDCWD, old_path, AT_FDCWD, new_path, 0);
	}

	int Sysdeps<Symlinkat>::operator()(const char *target_path, int dirfd, const char *link_path) {
		(void)dirfd;
		char resolvedLink[VFS_MAX_PATH_LENGTH] {};

		if (!resolvePath(link_path, resolvedLink, sizeof(resolvedLink)) or target_path == nullptr) {
			return EINVAL;
		}

		VfsSymlinkMsgData req {};
		VfsSymlinkReplyMsgData reply {};
		fillVfsName(req.target, sizeof(req.target), req.targetLength, target_path);
		fillVfsName(req.linkPath, sizeof(req.linkPath), req.linkPathLength, resolvedLink);

		const int err = sendVfsRequest(VFS_SYMLINK_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		return reply.success ? 0 : vfsStatusToErr(reply.status);
	}

	int Sysdeps<Symlink>::operator()(const char *target_path, const char *link_path) {
		return sysdep<Symlinkat>(target_path, AT_FDCWD, link_path);
	}

	int Sysdeps<Mkdirat>::operator()(int dirfd, const char *path, mode_t mode) {
		(void)dirfd;
		(void)mode;
		char resolved[VFS_MAX_PATH_LENGTH] {};

		if (!resolvePath(path, resolved, sizeof(resolved))) {
			return EINVAL;
		}

		VfsMkdirMsgData req {};
		VfsMkdirReplyMsgData reply {};
		fillVfsName(req.path, sizeof(req.path), req.pathLength, resolved);

		const int err = sendVfsRequest(VFS_MKDIR_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		return reply.success ? 0 : vfsStatusToErr(reply.status);
	}

	int Sysdeps<Mkdir>::operator()(const char *path, mode_t mode) {
		return sysdep<Mkdirat>(AT_FDCWD, path, mode);
	}

	int Sysdeps<Faccessat>::operator()(int dirfd, const char *pathname, int mode, int flags) {
		(void)dirfd;
		(void)mode;
		(void)flags;
		struct stat st {};

		return sysdep<Stat>(fsfd_target::path, AT_FDCWD, pathname, 0, &st);
	}

	int Sysdeps<Access>::operator()(const char *path, int mode) {
		return sysdep<Faccessat>(AT_FDCWD, path, mode, 0);
	}

	int Sysdeps<Dup>::operator()(int fd, int flags, int *newfd) {
		(void)flags;
		if (newfd == nullptr) {
			return EFAULT;
		}

		if (fd < 0 or fd >= maxHorizonFds or fd <= 2 or !fdTable[fd].used) {
			return EBADF;
		}

		for (int i = 3; i < maxHorizonFds; ++i) {
			if (!fdTable[i].used) {
				fdTable[i] = fdTable[fd];
				*newfd = i;

				return 0;
			}
		}

		return EMFILE;
	}

	int Sysdeps<Dup2>::operator()(int fd, int flags, int newfd) {
		(void)flags;
		if (fd < 0 or fd >= maxHorizonFds or newfd < 0 or newfd >= maxHorizonFds or fd <= 2 or !fdTable[fd].used) {
			return EBADF;
		}

		if (newfd > 2 and fdTable[newfd].used) {
			sysdep<Close>(newfd);
		}

		fdTable[newfd] = fdTable[fd];

		return 0;
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
		if (handle < 0 or handle >= maxHorizonFds or !fdTable[handle].used) {
			return EBADF;
		}

		if (buffer == nullptr or bytes_read == nullptr) {
			return EFAULT;
		}

		if (fdTable[handle].nodeType != VFS_NODE_DIRECTORY) {
			return ENOTDIR;
		}

		VfsHandleReadDirMsgData req {};
		VfsHandleReadDirReplyMsgData reply {};
		req.handle = fdTable[handle].handle;

		const int err = sendVfsRequest(VFS_HANDLE_READDIR_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		if (!reply.success) {
			return vfsStatusToErr(reply.status);
		}

		size_t written = 0;
		auto *out = static_cast<char *>(buffer);

		for (uint32_t i = 0; i < reply.entryCount; ++i) {
			const char *name = reply.entries[i].name;
			const size_t nameLen = strlen(name);
			size_t reclen = offsetof(struct dirent, d_name) + nameLen + 1;
			reclen = (reclen + alignof(struct dirent) - 1) & ~(alignof(struct dirent) - 1);

			if (written + reclen > max_size) {
				break;
			}

			auto *entry = reinterpret_cast<struct dirent *>(out + written);
			memset(entry, 0, reclen);
			entry->d_ino = reply.entries[i].nodeId;
			entry->d_off = reply.position;
			entry->d_reclen = reclen;
			entry->d_type = nodeTypeToDirentType(reply.entries[i].nodeType);
			memcpy(entry->d_name, name, nameLen + 1);
			written += reclen;
		}

		*bytes_read = written;

		return 0;
	}

	int Sysdeps<Stat>::operator()(fsfd_target fsfdt, int fd, const char *path, int flags, struct stat *statbuf) {
		(void)flags;

		if (statbuf == nullptr) {
			return EFAULT;
		}

		VfsStatReplyMsgData reply {};

		if (fsfdt == fsfd_target::fd) {
			if (fd < 0 or fd >= maxHorizonFds or !fdTable[fd].used) {
				return EBADF;
			}

			memset(statbuf, 0, sizeof(*statbuf));
			statbuf->st_mode = nodeTypeToMode(fdTable[fd].nodeType);
			statbuf->st_nlink = 1;
			statbuf->st_blksize = 4096;

			return 0;
		}

		char resolved[VFS_MAX_PATH_LENGTH] {};

		if (!resolvePath(path, resolved, sizeof(resolved))) {
			return EINVAL;
		}

		VfsStatMsgData req {};
		fillVfsName(req.path, sizeof(req.path), req.pathLength, resolved);

		const int err = sendVfsRequest(VFS_STAT_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		if (!reply.success) {
			return vfsStatusToErr(reply.status);
		}

		memset(statbuf, 0, sizeof(*statbuf));
		statbuf->st_ino = reply.nodeId;
		statbuf->st_mode = nodeTypeToMode(reply.nodeType);
		statbuf->st_nlink = 1;
		statbuf->st_size = reply.size;
		statbuf->st_blksize = 4096;
		statbuf->st_blocks = (reply.size + 511) / 512;

		return 0;
	}

	int Sysdeps<Rmdir>::operator()(const char *path) {
		return sysdep<Unlinkat>(AT_FDCWD, path, AT_REMOVEDIR);
	}

	int Sysdeps<Unlinkat>::operator()(int fd, const char *path, int flags) {
		(void)fd;
		(void)flags;
		char resolved[VFS_MAX_PATH_LENGTH] {};

		if (!resolvePath(path, resolved, sizeof(resolved))) {
			return EINVAL;
		}

		VfsUnlinkMsgData req {};
		VfsUnlinkReplyMsgData reply {};
		fillVfsName(req.path, sizeof(req.path), req.pathLength, resolved);

		const int err = sendVfsRequest(VFS_UNLINK_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		return reply.success ? 0 : vfsStatusToErr(reply.status);
	}

	int Sysdeps<Rename>::operator()(const char *path, const char *new_path) {
		return sysdep<Renameat>(AT_FDCWD, path, AT_FDCWD, new_path);
	}

	int Sysdeps<Renameat>::operator()(int olddirfd, const char *old_path, int newdirfd, const char *new_path) {
		(void)olddirfd;
		(void)newdirfd;
		char resolvedOld[VFS_MAX_PATH_LENGTH] {};
		char resolvedNew[VFS_MAX_PATH_LENGTH] {};

		if (!resolvePath(old_path, resolvedOld, sizeof(resolvedOld)) or !resolvePath(new_path, resolvedNew, sizeof(resolvedNew))) {
			return EINVAL;
		}

		VfsRenameMsgData req {};
		VfsRenameReplyMsgData reply {};
		fillVfsName(req.oldPath, sizeof(req.oldPath), req.oldPathLength, resolvedOld);
		fillVfsName(req.newPath, sizeof(req.newPath), req.newPathLength, resolvedNew);

		const int err = sendVfsRequest(VFS_RENAME_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		return reply.success ? 0 : vfsStatusToErr(reply.status);
	}

	int Sysdeps<Fcntl>::operator()(int fd, int request, va_list args, int *result) {
		if (fd < 0 or fd >= maxHorizonFds or (!fdTable[fd].used and fd > 2)) {
			return EBADF;
		}

		switch (request) {
			case F_GETFD:
				*result = 0;
				return 0;
			case F_SETFD:
				(void)va_arg(args, int);
				*result = 0;
				return 0;
			case F_GETFL:
				*result = fd <= 2 ? O_WRONLY : fdTable[fd].flags;
				return 0;
			case F_DUPFD:
			case F_DUPFD_CLOEXEC: {
				int minFd = va_arg(args, int);

				if (minFd < 0 or minFd >= maxHorizonFds or fd <= 2) {
					return EINVAL;
				}

				for (int i = minFd; i < maxHorizonFds; ++i) {
					if (!fdTable[i].used) {
						fdTable[i] = fdTable[fd];
						*result = i;

						return 0;
					}
				}

				return EMFILE;
			}
			default:
				return EINVAL;
		}
	}

	int Sysdeps<Openat>::operator()(int dirfd, const char *path, int flags, mode_t mode, int *fd) {
		(void)dirfd;
		(void)mode;
		return sysdep<Open>(path, flags, mode, fd);
	}

	int Sysdeps<Mount>::operator()(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data) {
		(void)source;
		(void)target;
		(void)fstype;
		(void)flags;
		(void)data;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Ioctl>::operator()(int fd, unsigned long request, void *arg, int *result) {
		if (fd < 0 or fd >= maxHorizonFds or !fdTable[fd].used) {
			return EBADF;
		}

		VfsIoctlMsgData req {};
		VfsIoctlReplyMsgData reply {};
		req.handle = fdTable[fd].handle;
		req.request = request;

		if (arg != nullptr) {
			req.inputLength = sizeof(uint64_t);
			memcpy(req.input, &arg, sizeof(arg));
		}

		const int err = sendVfsRequest(VFS_IOCTL_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		if (result != nullptr) {
			*result = 0;
		}

		return reply.success ? 0 : vfsStatusToErr(reply.status);
	}

	int Sysdeps<Open>::operator()(const char *pathname, int flags, mode_t mode, int *fd) {
		(void)mode;

		if(fd) {
			*fd = -1;
		}

		char resolved[VFS_MAX_PATH_LENGTH] {};

		if (!resolvePath(pathname, resolved, sizeof(resolved))) {
			return EINVAL;
		}

		VfsOpenMsgData req {};
		VfsOpenReplyMsgData reply {};
		fillVfsName(req.path, sizeof(req.path), req.pathLength, resolved);
		req.flags = posixOpenFlagsToVfs(flags);

		const int err = sendVfsRequest(VFS_OPEN_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		if (!reply.success) {
			return vfsStatusToErr(reply.status);
		}

		const int localFd = allocFd(reply.handle, reply.nodeType, flags, resolved);

		if (localFd < 0) {
			VfsCloseMsgData closeReq {};
			VfsCloseReplyMsgData closeReply {};
			closeReq.handle = reply.handle;
			sendVfsRequest(VFS_CLOSE_MSG_TYPE, &closeReq, sizeof(closeReq), &closeReply, sizeof(closeReply));

			return EMFILE;
		}

		if (fd != nullptr) {
			*fd = localFd;
		}

		return 0;
	};

	int Sysdeps<Read>::operator()(int fd, void *buff, size_t count, ssize_t *bytes_read) {
		if(bytes_read) {
			*bytes_read = 0;
		}

		if (fd < 0 or fd >= maxHorizonFds or !fdTable[fd].used) {
			return EBADF;
		}

		if (buff == nullptr and count != 0) {
			return EFAULT;
		}

		size_t done = 0;

		while (done < count) {
			VfsHandleReadMsgData req {};
			VfsHandleReadReplyMsgData reply {};
			req.handle = fdTable[fd].handle;
			req.length = static_cast<uint32_t>(std::min<size_t>(VFS_MAX_READ_SIZE, count - done));

			const int err = sendVfsRequest(VFS_HANDLE_READ_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

			if (err != 0) {
				return err;
			}

			if (!reply.success) {
				return done != 0 ? 0 : vfsStatusToErr(reply.status);
			}

			memcpy(static_cast<char *>(buff) + done, reply.data, reply.bytesRead);
			done += reply.bytesRead;

			if (reply.bytesRead == 0 or reply.bytesRead < req.length) {
				break;
			}
		}

		if (bytes_read != nullptr) {
			*bytes_read = done;
		}

		return 0;
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

		if (fd < 0 or fd >= maxHorizonFds or !fdTable[fd].used) {
			return EBADF;
		}

		if (buff == nullptr and count != 0) {
			return EFAULT;
		}

		size_t done = 0;

		while (done < count) {
			VfsHandleWriteMsgData req {};
			VfsHandleWriteReplyMsgData reply {};
			req.handle = fdTable[fd].handle;
			req.length = static_cast<uint32_t>(std::min<size_t>(VFS_MAX_READ_SIZE, count - done));
			memcpy(req.data, static_cast<const char *>(buff) + done, req.length);

			const int err = sendVfsRequest(VFS_HANDLE_WRITE_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

			if (err != 0) {
				return err;
			}

			if (!reply.success) {
				return done != 0 ? 0 : vfsStatusToErr(reply.status);
			}

			done += reply.bytesWritten;

			if (reply.bytesWritten == 0 or reply.bytesWritten < req.length) {
				break;
			}
		}

		if (bytes_written != nullptr) {
			*bytes_written = done;
		}

		return 0;
	}

	int Sysdeps<Seek>::operator()(int fd, off_t offset, int whence, off_t *new_offset) {
		if (fd == 1 or fd == 2) {
			return ESPIPE;
		}

		if (fd < 0 or fd >= maxHorizonFds or !fdTable[fd].used) {
			return EBADF;
		}

		VfsHandleSeekMsgData req {};
		VfsHandleSeekReplyMsgData reply {};
		req.handle = fdTable[fd].handle;
		req.offset = offset;
		req.whence = whence;

		const int err = sendVfsRequest(VFS_HANDLE_SEEK_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		if (!reply.success) {
			return vfsStatusToErr(reply.status);
		}

		if(new_offset) {
			*new_offset = reply.position;
		}

		return 0;
	}

	int Sysdeps<Close>::operator()(int fd) {
		if(fd >= 0 && fd <= 2) {
			return 0;
		}

		if (fd < 0 or fd >= maxHorizonFds or !fdTable[fd].used) {
			return EBADF;
		}

		VfsCloseMsgData req {};
		VfsCloseReplyMsgData reply {};
		req.handle = fdTable[fd].handle;
		const bool closeBackend = !backendHandleHasOtherFd(fd);
		fdTable[fd] = HorizonFd();

		if (!closeBackend) {
			return 0;
		}

		const int err = sendVfsRequest(VFS_CLOSE_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		return reply.success ? 0 : vfsStatusToErr(reply.status);
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

	int Sysdeps<Flock>::operator()(int fd, int options) {
		constexpr int HOS_LOCK_SH = 1;
		constexpr int HOS_LOCK_EX = 2;
		constexpr int HOS_LOCK_UN = 8;

		if (fd < 0 or fd >= maxHorizonFds or !fdTable[fd].used) {
			return EBADF;
		}

		if ((options & HOS_LOCK_UN) != 0) {
			VfsUnlockMsgData req {};
			VfsUnlockReplyMsgData reply {};
			req.handle = fdTable[fd].handle;

			const int err = sendVfsRequest(VFS_UNLOCK_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

			if (err != 0) {
				return err;
			}

			return reply.success ? 0 : vfsStatusToErr(reply.status);
		}

		VfsLockMsgData req {};
		VfsLockReplyMsgData reply {};
		req.handle = fdTable[fd].handle;
		req.mode = (options & HOS_LOCK_EX) != 0 ? VFS_LOCK_EXCLUSIVE : VFS_LOCK_SHARED;

		if ((options & (HOS_LOCK_SH | HOS_LOCK_EX)) == 0) {
			return EINVAL;
		}

		const int err = sendVfsRequest(VFS_LOCK_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));

		if (err != 0) {
			return err;
		}

		return reply.success ? 0 : vfsStatusToErr(reply.status);
	}

	int Sysdeps<Nice>::operator()(int nice, int *ret) {
		(void)nice;
		(void)ret;
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
		return id == 0 ? 0 : EPERM;
	}

	int Sysdeps<SetGid>::operator()(gid_t id) {
		return id == 0 ? 0 : EPERM;
	}

	int Sysdeps<SetEuid>::operator()(uid_t id) {
		return id == 0 ? 0 : EPERM;
	}

	int Sysdeps<SetEgid>::operator()(gid_t id) {
		return id == 0 ? 0 : EPERM;
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
		return _ruid == 0 and _euid == 0 and _suid == 0 ? 0 : EPERM;
	}

	int Sysdeps<SetResgid>::operator()(gid_t _rgid, gid_t _egid, gid_t _sgid) {
		return _rgid == 0 and _egid == 0 and _sgid == 0 ? 0 : EPERM;
	}

	int Sysdeps<GetResuid>::operator()(uid_t *ruid, uid_t *euid, uid_t *suid) {
		if (ruid != nullptr) {
			*ruid = 0;
		}

		if (euid != nullptr) {
			*euid = 0;
		}

		if (suid != nullptr) {
			*suid = 0;
		}

		return 0;
	}

	int Sysdeps<GetResgid>::operator()(gid_t *rgid, gid_t *egid, gid_t *sgid) {
		if (rgid != nullptr) {
			*rgid = 0;
		}

		if (egid != nullptr) {
			*egid = 0;
		}

		if (sgid != nullptr) {
			*sgid = 0;
		}

		return 0;
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

	pid_t Sysdeps<GetPpid>::operator()() {
		return 0;
	}

	int Sysdeps<GetSid>::operator()(pid_t pid, pid_t *pgid) {
		(void)pid;
		if (pgid != nullptr) {
			*pgid = 0;
		}

		return 0;
	}

	int Sysdeps<GetPgid>::operator()(pid_t pid, pid_t *pgid) {
		(void)pid;
		if (pgid != nullptr) {
			*pgid = 0;
		}

		return 0;
	}

	int Sysdeps<GetHostname>::operator()(char *buffer, size_t bufsize) {
		if (buffer == nullptr or bufsize == 0) {
			return EINVAL;
		}

		return copyString(buffer, bufsize, "horizonos") ? 0 : ENAMETOOLONG;
	}

	int Sysdeps<SetHostname>::operator()(const char *buffer, size_t bufsize) {
		(void)buffer;
		(void)bufsize;
		return EPERM;
	}

	int Sysdeps<Uname>::operator()(struct utsname *buf) {
		if (buf == nullptr) {
			return EFAULT;
		}

		memset(buf, 0, sizeof(*buf));
		copyString(buf->sysname, sizeof(buf->sysname), "HorizonOS");
		copyString(buf->nodename, sizeof(buf->nodename), "horizonos");
		copyString(buf->release, sizeof(buf->release), "0.0.0");
		copyString(buf->version, sizeof(buf->version), "0.0.0");
		copyString(buf->machine, sizeof(buf->machine), "x86_64");

		return 0;
	}

	void Sysdeps<Sync>::operator()() {
		VfsSyncMsgData req {};
		VfsSyncReplyMsgData reply {};
		fillVfsName(req.volume, sizeof(req.volume), req.volumeLength, "HorizonOS");
		sendVfsRequest(VFS_SYNC_MSG_TYPE, &req, sizeof(req), &reply, sizeof(reply));
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

	int Sysdeps<Pipe>::operator()(int *fds, int flags) {
		(void)fds;
		(void)flags;
		panic_unimplemented_sysdep(__PRETTY_FUNCTION__);
	}

	int Sysdeps<Chdir>::operator()(const char *path) {
		char resolved[VFS_MAX_PATH_LENGTH] {};

		if (!resolvePath(path, resolved, sizeof(resolved))) {
			return EINVAL;
		}

		struct stat st {};
		const int err = sysdep<Stat>(fsfd_target::path, AT_FDCWD, resolved, 0, &st);

		if (err != 0) {
			return err;
		}

		if (!S_ISDIR(st.st_mode)) {
			return ENOTDIR;
		}

		copyString(currentDirectory, sizeof(currentDirectory), resolved);

		return 0;
	}

	int Sysdeps<Fchdir>::operator()(int fd) {
		if (fd < 0 or fd >= maxHorizonFds or !fdTable[fd].used) {
			return EBADF;
		}

		if (fdTable[fd].nodeType != VFS_NODE_DIRECTORY) {
			return ENOTDIR;
		}

		copyString(currentDirectory, sizeof(currentDirectory), fdTable[fd].path);

		return 0;
	}

	int Sysdeps<Sigprocmask>::operator()(int how, const sigset_t *__restrict set, sigset_t *__restrict retrieve) {
		(void)how;
		(void)set;
		(void)retrieve;
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
