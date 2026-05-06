#ifndef _HORIZONOS_GENERIC_H
#define _HORIZONOS_GENERIC_H

#include <abi-bits/hos_msg.h>

#ifdef __cplusplus
extern "C" {
#endif

int register_horizonos_port(int port);
int send_horizonos_message(int port, const struct hos_msg *hdr);
int receive_horizonos_message(int port, struct hos_msg *hdr);
int is_thread_alive(int tid, bool *alive);
int mmap_phys(uint64_t physAddr, uint64_t len, uint64_t *retAddr);
int get_rsdp(uint64_t *rsdpAddr);
int install_irq_handler(uint32_t irq, uint32_t(*handler)(void *), void *ctx, void *handlerOut);
int uninstall_irq_handler(uint32_t(*handler)(void *), void *handle);
int get_irq_mode(long *mode);
int set_int_status(bool status);

#ifdef __cplusplus
}
#endif

#endif