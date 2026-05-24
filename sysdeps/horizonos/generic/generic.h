#ifndef _HORIZONOS_GENERIC_H
#define _HORIZONOS_GENERIC_H

#include <abi-bits/hos_msg.h>

#ifdef __cplusplus
extern "C" {
#endif

int register_horizonos_port(long *ret, uint64_t preferredPort = 0);
int send_horizonos_message(uint64_t sendPort, uint64_t port, const struct hos_msg *hdr);
int receive_horizonos_message(uint64_t port, struct hos_msg *hdr, filter_options *options);
int is_thread_alive(int tid, bool *alive);
int mmap_phys(uint64_t physAddr, uint64_t len, uint64_t *retAddr);
int get_rsdp(uint64_t *rsdpAddr);
int install_irq_handler(uint64_t irq, uint64_t port);
int uninstall_irq_handler(uint64_t irq);
int get_irq_mode(long *mode);
int set_int_status(bool status);
int allocIntVec(uint8_t *vecOut, uint64_t port, uint64_t destCpu = 0);
int freeIntVec(uint8_t vec, uint64_t destCpu = 0);
int allocGsi(uint64_t *gsiOut, uint64_t port, uint64_t destCpu = 0);
int freeGsi(uint64_t gsi, uint64_t destCpu = 0);
int lockToCore(uint64_t cpuId);
int getCpuCount(uint64_t *cpuCountOut);
int getCpuIds(long *cpuIdOutArray, uint64_t cpuCount);

#ifdef __cplusplus
}
#endif

#endif