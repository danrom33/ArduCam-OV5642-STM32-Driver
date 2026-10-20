#ifndef __DVP_CAM_H
#define __DVP_CAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32l4xx_hal.h"

// Upper bound on how long TakePicture waits for a frame. Snapshot mode waits for
// the next VSYNC, so this must cover at least two frame periods at the slowest
// configured frame rate.
#ifndef OV5642_CAPTURE_TIMEOUT_MS
#define OV5642_CAPTURE_TIMEOUT_MS 2000
#endif

// Largest capture a single DMA transfer can hold (0xFFFF words). HAL_DCMI_Start_DMA
// can go beyond this, but only with a second M2M DMA channel, and it leaves the tail
// of a short JPEG in a scratch area. So captures are capped here instead.
#define OV5642_MAX_CAPTURE_BYTES (0xFFFFU * 4U)

typedef struct {
    I2C_HandleTypeDef *hi2c;
    DCMI_HandleTypeDef *hdcmi;
} OV5642_HandleTypeDef;

typedef enum {
    OV5642_320x240 = 0,
    OV5642_640x480,
    OV5642_1024x768,
    OV5642_1280x960,
    OV5642_1600x1200,
    OV5642_2048x1536,
    OV5642_2592x1944
} OV5642_Resolution;

typedef enum {
    OV5642_OK = 0,
    OV5642_ERROR,
    OV5642_I2C_TIMEOUT,
    OV5642_DCMI_TIMEOUT,
    OV5642_DCMI_ERROR,
    OV5642_BUFFER_OVERFLOW,
} OV5642_StatusTypeDef;

OV5642_StatusTypeDef OV5642_Init(I2C_HandleTypeDef *hi2c, DCMI_HandleTypeDef *hdcmi);
OV5642_StatusTypeDef OV5642_TakePicture(uint8_t frame_buffer[], uint32_t buffer_size, uint32_t *image_length);
OV5642_StatusTypeDef OV5642_SetJpeg();
OV5642_StatusTypeDef OV5642_SetResolution(OV5642_Resolution res);
OV5642_StatusTypeDef OV5642_TestSerialConnection();
const char* OV5642_StatusToString(OV5642_StatusTypeDef status);

#ifdef __cplusplus
}
#endif

#endif /* __DVP_CAM_H */
