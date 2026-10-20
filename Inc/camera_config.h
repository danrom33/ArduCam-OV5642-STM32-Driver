#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

#include <stdint.h>

#define OV5642_WRITE_ADDR 0x78
#define OV5642_READ_ADDR 0x79

extern const uint16_t OV5642_YUV422[][2];

extern const uint16_t OV5642_JPEG_Capture_QSXGA[][2];

extern const uint16_t ov5642_320x240[][2];

extern const uint16_t ov5642_640x480[][2];

extern const uint16_t ov5642_1280x960[][2];

extern const uint16_t ov5642_1600x1200[][2];

extern const uint16_t ov5642_1024x768[][2];

extern const uint16_t ov5642_2048x1536[][2];
	
extern const uint16_t ov5642_2592x1944[][2];

extern const uint16_t OV5642_QVGA_Preview[][2];

#endif