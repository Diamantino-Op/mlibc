#ifndef _HORIZONOS_WINDOW_H
#define _HORIZONOS_WINDOW_H

#include <stddef.h>
#include <stdint.h>

#include <horizonos/display.h>

#define HOS_WINDOW_CREATE 0xB0000
#define HOS_WINDOW_CREATE_REPLY 0xB0001
#define HOS_WINDOW_DESTROY 0xB0002
#define HOS_WINDOW_DESTROY_REPLY 0xB0003
#define HOS_WINDOW_DRAW_RECT 0xB0004
#define HOS_WINDOW_DRAW_RECT_REPLY 0xB0005
#define HOS_WINDOW_PRESENT 0xB0006
#define HOS_WINDOW_PRESENT_REPLY 0xB0007
#define HOS_WINDOW_MOVE 0xB0008
#define HOS_WINDOW_MOVE_REPLY 0xB0009

#define HOS_WINDOW_MAX_TITLE 64
#define HOS_WINDOW_MAX_TRANSFER_BYTES HOS_DISPLAY_MAX_TRANSFER_BYTES

struct HosWindowCreateRequest {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	char title[HOS_WINDOW_MAX_TITLE];
	size_t titleLength;
};

struct HosWindowCreateReply {
	bool success;
	uint64_t windowId;
	uint32_t width;
	uint32_t height;
};

struct HosWindowDestroyRequest {
	uint64_t windowId;
};

struct HosWindowDestroyReply {
	bool success;
};

struct HosWindowDrawRectHeader {
	uint64_t windowId;
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	uint32_t srcPitch;
	uint32_t pixelBytes;
};

struct HosWindowDrawRectReply {
	bool success;
	uint32_t clippedWidth;
	uint32_t clippedHeight;
};

struct HosWindowPresentRequest {
	uint64_t windowId;
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
};

struct HosWindowPresentReply {
	bool success;
};

struct HosWindowMoveRequest {
	uint64_t windowId;
	uint32_t x;
	uint32_t y;
};

struct HosWindowMoveReply {
	bool success;
};

#endif
