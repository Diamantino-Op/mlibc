#ifndef _HORIZONOS_GENERIC_H
#define _HORIZONOS_GENERIC_H

#include <abi-bits/hos_cpu.h>
#include <abi-bits/hos_msg.h>

#ifdef __cplusplus
extern "C" {
#endif

enum MMapCacheMode {
	MAP_CACHE_WB = 0,
	MAP_CACHE_WC = 1,
	MAP_CACHE_UC = 2,
	MAP_CACHE_WT = 3
};

int register_horizonos_port(long *ret, uint64_t preferredPort = 0);
int send_horizonos_message(uint64_t sendPort, uint64_t port, const struct hos_msg *hdr);
int receive_horizonos_message(uint64_t port, struct hos_msg *hdr, filter_options *options);
int is_thread_alive(int tid, bool *alive);
int munmap_extra(void *ptr, size_t len, bool freePage);
int mmap_phys(uint64_t physAddr, uint64_t len, uint64_t *retAddr, bool isHhdm = false, MMapCacheMode cacheMode = MAP_CACHE_WB);
int get_rsdp(uint64_t *rsdpAddr);
int install_irq_handler(uint64_t irq, uint64_t port);
int uninstall_irq_handler(uint64_t irq);
int get_irq_mode(long *mode);
int set_int_status(bool status);
int allocIntVec(uint8_t *vecOut, uint64_t port, uint64_t destCpu = 0, bool isLapic = false);
int freeIntVec(uint8_t vec, uint64_t destCpu = 0, bool isLapic = false);
int allocGsi(uint64_t *gsiOut, uint64_t port, uint64_t destCpu = 0, bool isLapic = false);
int freeGsi(uint64_t gsi, uint64_t destCpu = 0, bool isLapic = false);
int lockToCore(uint64_t cpuId);
int getCpuIds(HosCpuInfo *cpuIdOutArray, uint64_t cpuCount);
int allocPhysPage(uint64_t *outAddr);
int freePhysPage(uint64_t physPage);
int registerKernelEventHandler(uint64_t port, uint64_t eventMask);
int sendVfsRequest(uint64_t requestType, const void *request, size_t requestLength, void *reply, size_t replyLength);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

namespace horizonos {

constexpr uint64_t GET_MSG_TYPE = 0x3;
constexpr uint64_t REPLY_GET_MSG_TYPE = 0x6;
constexpr uint64_t NAME_REGISTRY_PORT = 1;
constexpr uint64_t VFS_CLIENT_PORT_BASE = 0xffff000000000000ULL;

constexpr uint64_t VFS_STAT_MSG_TYPE = 0x90000;
constexpr uint64_t VFS_OPEN_MSG_TYPE = 0x9000A;
constexpr uint64_t VFS_CLOSE_MSG_TYPE = 0x9000C;
constexpr uint64_t VFS_HANDLE_READ_MSG_TYPE = 0x9000E;
constexpr uint64_t VFS_HANDLE_WRITE_MSG_TYPE = 0x90010;
constexpr uint64_t VFS_UNLINK_MSG_TYPE = 0x90012;
constexpr uint64_t VFS_RENAME_MSG_TYPE = 0x90014;
constexpr uint64_t VFS_HANDLE_SEEK_MSG_TYPE = 0x90018;
constexpr uint64_t VFS_MKDIR_MSG_TYPE = 0x9001A;
constexpr uint64_t VFS_LOCK_MSG_TYPE = 0x90020;
constexpr uint64_t VFS_UNLOCK_MSG_TYPE = 0x90022;
constexpr uint64_t VFS_SYNC_MSG_TYPE = 0x90024;
constexpr uint64_t VFS_FSYNC_MSG_TYPE = 0x90026;
constexpr uint64_t VFS_LINK_MSG_TYPE = 0x9002A;
constexpr uint64_t VFS_SYMLINK_MSG_TYPE = 0x9002C;
constexpr uint64_t VFS_IOCTL_MSG_TYPE = 0x90032;
constexpr uint64_t VFS_HANDLE_READDIR_MSG_TYPE = 0x90034;
constexpr uint64_t VFS_READLINK_MSG_TYPE = 0x90036;
constexpr uint64_t VFS_HANDLE_TRUNCATE_MSG_TYPE = 0x90038;

constexpr uint32_t VFS_MAX_PATH_LENGTH = 256;
constexpr uint32_t VFS_MAX_READ_SIZE = 2048;
constexpr uint32_t VFS_MAX_NAME_LENGTH = 64;
constexpr uint32_t VFS_MAX_DIR_ENTRIES = 32;

constexpr uint8_t VFS_NODE_FILE = 1;
constexpr uint8_t VFS_NODE_DIRECTORY = 2;
constexpr uint8_t VFS_NODE_SYMLINK = 3;
constexpr uint8_t VFS_NODE_DEVICE = 4;

constexpr uint32_t VFS_OPEN_READ = 1 << 0;
constexpr uint32_t VFS_OPEN_WRITE = 1 << 1;
constexpr uint32_t VFS_OPEN_CREATE = 1 << 2;
constexpr uint32_t VFS_OPEN_APPEND = 1 << 3;
constexpr uint32_t VFS_OPEN_TRUNCATE = 1 << 4;
constexpr uint32_t VFS_OPEN_EXCLUSIVE = 1 << 5;
constexpr uint8_t VFS_LOCK_SHARED = 1;
constexpr uint8_t VFS_LOCK_EXCLUSIVE = 2;

struct GetMsgData {
	char name[16] {};
	size_t nameLength {};
};

struct GetReplyMsgData {
	uint64_t port {};
	uint16_t tid {};
	uint16_t versionMajor {};
	uint16_t versionMinor {};
	uint16_t versionPatch {};
};

struct VfsDirEntry {
	char name[VFS_MAX_NAME_LENGTH] {};
	size_t nameLength {};
	uint8_t nodeType {};
	uint64_t size {};
	uint64_t nodeId {};
};

struct VfsStatMsgData { char path[VFS_MAX_PATH_LENGTH] {}; size_t pathLength {}; };
struct VfsStatReplyMsgData { bool success {}; uint8_t nodeType {}; uint64_t size {}; uint64_t nodeId {}; uint32_t status {}; };
struct VfsReadDirReplyMsgData { bool success {}; uint32_t entryCount {}; VfsDirEntry entries[VFS_MAX_DIR_ENTRIES] {}; uint32_t nextOffset {}; bool hasMore {}; uint32_t status {}; };
struct VfsOpenMsgData { char path[VFS_MAX_PATH_LENGTH] {}; size_t pathLength {}; uint32_t flags {}; };
struct VfsOpenReplyMsgData { bool success {}; uint64_t handle {}; uint8_t nodeType {}; uint64_t size {}; uint32_t status {}; uint64_t nodeId {}; };
struct VfsCloseMsgData { uint64_t handle {}; };
struct VfsCloseReplyMsgData { bool success {}; uint32_t status {}; };
struct VfsHandleReadMsgData { uint64_t handle {}; uint32_t length {}; };
struct VfsHandleReadReplyMsgData { bool success {}; uint32_t bytesRead {}; uint64_t position {}; uint8_t data[VFS_MAX_READ_SIZE] {}; uint32_t status {}; };
struct VfsHandleWriteMsgData { uint64_t handle {}; uint32_t length {}; uint8_t data[VFS_MAX_READ_SIZE] {}; };
struct VfsHandleWriteReplyMsgData { bool success {}; uint32_t bytesWritten {}; uint64_t position {}; uint64_t size {}; uint32_t status {}; };
struct VfsHandleSeekMsgData { uint64_t handle {}; int64_t offset {}; uint8_t whence {}; };
struct VfsHandleSeekReplyMsgData { bool success {}; uint64_t position {}; uint32_t status {}; };
struct VfsHandleReadDirMsgData { uint64_t handle {}; };
struct VfsHandleReadDirReplyMsgData { bool success {}; uint32_t entryCount {}; VfsDirEntry entries[VFS_MAX_DIR_ENTRIES] {}; uint64_t position {}; bool hasMore {}; uint32_t status {}; };
struct VfsUnlinkMsgData { char path[VFS_MAX_PATH_LENGTH] {}; size_t pathLength {}; };
struct VfsUnlinkReplyMsgData { bool success {}; uint32_t status {}; };
struct VfsRenameMsgData { char oldPath[VFS_MAX_PATH_LENGTH] {}; size_t oldPathLength {}; char newPath[VFS_MAX_PATH_LENGTH] {}; size_t newPathLength {}; };
struct VfsRenameReplyMsgData { bool success {}; uint32_t status {}; };
struct VfsTruncateMsgData { char path[VFS_MAX_PATH_LENGTH] {}; size_t pathLength {}; uint64_t size {}; };
struct VfsTruncateReplyMsgData { bool success {}; uint64_t size {}; uint32_t status {}; };
struct VfsMkdirMsgData { char path[VFS_MAX_PATH_LENGTH] {}; size_t pathLength {}; };
struct VfsMkdirReplyMsgData { bool success {}; uint32_t status {}; uint64_t nodeId {}; };
struct VfsLockMsgData { uint64_t handle {}; uint64_t offset {}; uint64_t length {}; uint8_t mode {}; };
struct VfsLockReplyMsgData { bool success {}; uint32_t status {}; };
struct VfsUnlockMsgData { uint64_t handle {}; uint64_t offset {}; uint64_t length {}; };
struct VfsUnlockReplyMsgData { bool success {}; uint32_t status {}; };
struct VfsSyncMsgData { char volume[VFS_MAX_NAME_LENGTH] {}; size_t volumeLength {}; };
struct VfsSyncReplyMsgData { bool success {}; uint32_t status {}; };
struct VfsFsyncMsgData { uint64_t handle {}; };
struct VfsFsyncReplyMsgData { bool success {}; uint32_t status {}; };
struct VfsLinkMsgData { char oldPath[VFS_MAX_PATH_LENGTH] {}; size_t oldPathLength {}; char newPath[VFS_MAX_PATH_LENGTH] {}; size_t newPathLength {}; };
struct VfsLinkReplyMsgData { bool success {}; uint32_t status {}; uint64_t nodeId {}; };
struct VfsSymlinkMsgData { char target[VFS_MAX_PATH_LENGTH] {}; size_t targetLength {}; char linkPath[VFS_MAX_PATH_LENGTH] {}; size_t linkPathLength {}; };
struct VfsSymlinkReplyMsgData { bool success {}; uint32_t status {}; uint64_t nodeId {}; };
struct VfsReadLinkMsgData { char path[VFS_MAX_PATH_LENGTH] {}; size_t pathLength {}; };
struct VfsReadLinkReplyMsgData { bool success {}; uint32_t status {}; char target[VFS_MAX_PATH_LENGTH] {}; size_t targetLength {}; };
struct VfsIoctlMsgData { uint64_t handle {}; uint32_t request {}; uint32_t inputLength {}; uint8_t input[VFS_MAX_READ_SIZE] {}; };
struct VfsIoctlReplyMsgData { bool success {}; uint32_t status {}; uint32_t outputLength {}; uint8_t output[VFS_MAX_READ_SIZE] {}; };
struct VfsHandleTruncateMsgData { uint64_t handle {}; uint64_t size {}; };
struct VfsHandleTruncateReplyMsgData { bool success {}; uint64_t size {}; uint32_t status {}; };

}

#endif

#endif
