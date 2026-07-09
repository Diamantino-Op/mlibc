#ifndef _HORIZONOS_DISPLAY_H
#define _HORIZONOS_DISPLAY_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define HOS_DISPLAY_GET_INFO 0xA0000
#define HOS_DISPLAY_GET_INFO_REPLY 0xA0001
#define HOS_DISPLAY_PRESENT_RECT 0xA0002
#define HOS_DISPLAY_PRESENT_RECT_REPLY 0xA0003
#define HOS_DISPLAY_CLEAR 0xA0004
#define HOS_DISPLAY_CLEAR_REPLY 0xA0005

#define HOS_DISPLAY_MAX_TRANSFER_BYTES (256 * 1024)

struct HosDisplayInfo {
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint16_t bpp;
	uint8_t redMaskSize;
	uint8_t redMaskShift;
	uint8_t greenMaskSize;
	uint8_t greenMaskShift;
	uint8_t blueMaskSize;
	uint8_t blueMaskShift;
};

struct HosDisplayInfoReply {
	bool success;
	HosDisplayInfo info;
};

struct HosDisplayRectHeader {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	uint32_t srcPitch;
	uint32_t pixelBytes;
};

struct HosDisplayPresentReply {
	bool success;
	uint32_t clippedWidth;
	uint32_t clippedHeight;
};

struct HosDisplayClearRequest {
	uint32_t color;
};

struct HosDisplayClearReply {
	bool success;
};

#endif
