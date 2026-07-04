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

#ifdef __cplusplus
}
#endif

#endif
