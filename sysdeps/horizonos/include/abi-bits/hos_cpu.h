#ifndef _ABIBITS_HOS_CPU_H
#define _ABIBITS_HOS_CPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct HosCpuInfo {
	uint64_t cpuId;
	uint64_t apicId;
};

#ifdef __cplusplus
}
#endif

#endif