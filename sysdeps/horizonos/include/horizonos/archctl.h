#ifndef _HORIZONOS_ARCHCTL_H
#define _HORIZONOS_ARCHCTL_H

#define ARCH_CTL_SET_GSBASE 0
#define ARCH_CTL_SET_FSBASE 1
#define ARCH_CTL_GET_GSBASE 2
#define ARCH_CTL_GET_FSBASE 3

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __MLIBC_ABI_ONLY

int arch_ctl(int, void *);

#endif /* !__MLIBC_ABI_ONLY */

#ifdef __cplusplus
}
#endif

#endif /* _HORIZONOS_ARCH_CTLH */
