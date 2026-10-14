#ifndef _ABIBITS_HOS_MSG_H
#define _ABIBITS_HOS_MSG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hos_msg {
	uint64_t port;          /* target port (for send) or port that received the message */
	uint64_t type;		    /* message type used for filtering */
	void *buffer;           /* pointer to message buffer in user space */
	size_t length;          /* buffer length */
	int flags;              /* message flags (e.g. MSG_DONTWAIT) */
	long ret_length;        /* kernel-filled: number of bytes sent/received (or negative error) */
	uint64_t src_port;      /* set by kernel on receive: source port */
	void *control;          /* optional ancillary/control data pointer */
	size_t control_len;     /* ancillary data length */
	uint64_t timeout_ns;    /* optional timeout in nanoseconds (0 = wait indefinitely) */
};

struct filter_options {
	uint64_t *blackListTypes; /* pointer to array of message types to block */
	size_t blackListCount;    /* number of entries in blackListTypes */
	uint64_t *whiteListTypes; /* pointer to array of message types to allow (if blackListTypes is not used) */
	size_t whiteListCount;    /* number of entries in whiteListTypes */
};

#ifdef __cplusplus
}
#endif

#endif