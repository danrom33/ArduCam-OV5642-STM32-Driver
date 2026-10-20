#include "dvp_cam.h"
#include "camera_config.h"
#include "sccb.h"

#include <string.h>

static OV5642_StatusTypeDef Write_SensorConfigs(const uint16_t config[][2]);
static uint32_t Find_JpegEnd(const uint8_t *buffer, uint32_t search_length);

OV5642_HandleTypeDef hov5642;

static volatile uint8_t frame_complete = 0;
static volatile uint8_t frame_error = 0;

OV5642_StatusTypeDef OV5642_TakePicture(uint8_t frame_buffer[], uint32_t buffer_size, uint32_t *image_length){
    DCMI_HandleTypeDef *hdcmi = hov5642.hdcmi;
    *image_length = 0;

    // DMA is configured for word transfers, so the destination must be word aligned
    if(((uint32_t)frame_buffer & 0x3U) != 0 || buffer_size < 4)
      return OV5642_ERROR;

    uint32_t capture_bytes = (buffer_size > OV5642_MAX_CAPTURE_BYTES) ? OV5642_MAX_CAPTURE_BYTES : (buffer_size & ~0x3U);
    uint32_t capture_words = capture_bytes / 4;

    // Clearing the buffer means the EOI search can never match a marker left over from
    // a previous, larger capture. Costs ~1-2 ms for a full buffer, well under one frame.
    memset(frame_buffer, 0, capture_bytes);

    frame_complete = 0;
    frame_error = 0;

    // HAL never clears ErrorCode, and HAL_DCMI_IRQHandler aborts the DMA on any interrupt
    // while it is non-zero, so a previous failure would otherwise kill this capture.
    hdcmi->ErrorCode = HAL_DCMI_ERROR_NONE;

    // The snapshot frame-end path in HAL_DCMI_IRQHandler disables OVR/ERR and Start_DMA
    // never re-enables them. Without OVR, a JPEG larger than the buffer is never reported.
    // LINE/VSYNC are only interrupt noise in JPEG mode.
    __HAL_DCMI_DISABLE_IT(hdcmi, DCMI_IT_LINE | DCMI_IT_VSYNC);
    __HAL_DCMI_ENABLE_IT(hdcmi, DCMI_IT_FRAME | DCMI_IT_OVR | DCMI_IT_ERR);

    if(HAL_DCMI_Start_DMA(hdcmi, DCMI_MODE_SNAPSHOT, (uint32_t)frame_buffer, capture_words) != HAL_OK)
      return OV5642_DCMI_ERROR;

    uint32_t start = HAL_GetTick();
    while(!frame_complete && !frame_error){
      if(HAL_GetTick() - start > OV5642_CAPTURE_TIMEOUT_MS)
        break;
    }

    // Latch the outcome before HAL_DCMI_Stop: aborting a DMA that has already finished
    // fires HAL_DCMI_ErrorCallback, which would otherwise look like a capture error.
    uint8_t completed = frame_complete;
    uint8_t errored = frame_error;
    uint32_t dcmi_error = hdcmi->ErrorCode;
    uint32_t words_remaining = __HAL_DMA_GET_COUNTER(hdcmi->DMA_Handle);

    HAL_DCMI_Stop(hdcmi);

    if(errored){
      // An overrun with no space left means the frame did not fit; with space left it
      // means the DMA could not keep up with PCLK (bus contention or priority).
      if((dcmi_error & HAL_DCMI_ERROR_OVR) && words_remaining == 0)
        return OV5642_BUFFER_OVERFLOW;
      return OV5642_DCMI_ERROR;
    }
    if(!completed)
      return OV5642_DCMI_TIMEOUT;

    uint32_t received_bytes = (capture_words - words_remaining) * 4;

    // Search one word past what the DMA counter reports, in case the last partial word was
    // still in the DCMI FIFO when the counter was read. Safe because the buffer was zeroed.
    uint32_t search_length = received_bytes + 4;
    if(search_length > capture_bytes)
      search_length = capture_bytes;

    uint32_t jpeg_length = Find_JpegEnd(frame_buffer, search_length);
    if(jpeg_length == 0){
      // No EOI marker: the image is truncated. Hand back what arrived so it can be inspected.
      *image_length = received_bytes;
      return OV5642_ERROR;
    }

    *image_length = jpeg_length;
    return OV5642_OK;
}

OV5642_StatusTypeDef OV5642_Init(I2C_HandleTypeDef *hi2c, DCMI_HandleTypeDef *hdcmi){
  hov5642.hi2c = hi2c;
  hov5642.hdcmi = hdcmi;
  return OV5642_OK;
}

OV5642_StatusTypeDef OV5642_SetJpeg(){
  OV5642_StatusTypeDef status = SCCB_write_reg(0x3008, 0x80); //ov5642 reset reg
  if(status != OV5642_OK)
    return status;
  HAL_Delay(100);

  status = Write_SensorConfigs(OV5642_QVGA_Preview);
  if(status != OV5642_OK)
    return status;
  status = Write_SensorConfigs(OV5642_JPEG_Capture_QSXGA);
  if(status != OV5642_OK)
    return status;

  const uint16_t jpeg_overrides[][2] = {
    {0x3818, 0xa8},
    {0x3621, 0x10},
    {0x3801, 0xb0},
    {0x4407, 0x04}, // JPEG quantisation scale: lower is higher quality, larger file
    {0xffff, 0xff},
  };
  return Write_SensorConfigs(jpeg_overrides);
}

OV5642_StatusTypeDef OV5642_SetResolution(OV5642_Resolution res){
  switch(res){
    case OV5642_320x240:
      return Write_SensorConfigs(ov5642_320x240);
    case OV5642_640x480:
      return Write_SensorConfigs(ov5642_640x480);
    case OV5642_1024x768:
      return Write_SensorConfigs(ov5642_1024x768);
    case OV5642_1280x960:
      return Write_SensorConfigs(ov5642_1280x960);
    case OV5642_1600x1200:
      return Write_SensorConfigs(ov5642_1600x1200);
    case OV5642_2048x1536:
      return Write_SensorConfigs(ov5642_2048x1536);
    case OV5642_2592x1944:
      return Write_SensorConfigs(ov5642_2592x1944);
    default:
      return OV5642_ERROR;
  }
}

OV5642_StatusTypeDef OV5642_TestSerialConnection(){
    uint8_t id_high = 0;
    uint8_t id_low = 0;
    uint8_t attempts = 0;

    while(attempts < 10){
      if(SCCB_read_reg(0x300A, &id_high) == OV5642_OK &&
         SCCB_read_reg(0x300B, &id_low) == OV5642_OK &&
         id_high == 0x56 && id_low == 0x42)
        return OV5642_OK;
      attempts++;
      HAL_Delay(100);
    }
    return OV5642_I2C_TIMEOUT;
}

const char* OV5642_StatusToString(OV5642_StatusTypeDef status){
  switch(status){
    case OV5642_OK:
      return "OV5642_OK";
    case OV5642_ERROR:
      return "OV5642_ERROR";
    case OV5642_I2C_TIMEOUT:
      return "OV5642_I2C_TIMEOUT";
    case OV5642_DCMI_TIMEOUT:
      return "OV5642_DCMI_TIMEOUT";
    case OV5642_DCMI_ERROR:
      return "OV5642_DCMI_ERROR";
    case OV5642_BUFFER_OVERFLOW:
      return "OV5642_BUFFER_OVERFLOW";
  }
  return "OV5642_UNKNOWN_STATUS";
}

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  if(hdcmi == hov5642.hdcmi)
    frame_complete = 1;
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
  if(hdcmi == hov5642.hdcmi)
    frame_error = 1;
}

// Returns the length up to and including the JPEG EOI marker (0xFF 0xD9), or 0 if absent.
static uint32_t Find_JpegEnd(const uint8_t *buffer, uint32_t search_length){
  for(uint32_t i = 0; i + 1 < search_length; i++){
    if(buffer[i] == 0xFF && buffer[i+1] == 0xD9)
      return i + 2;
  }
  return 0;
}

static OV5642_StatusTypeDef Write_SensorConfigs(const uint16_t config[][2]){
  uint16_t i = 0;

  while(!(config[i][0] == 0xffff && config[i][1] == 0xff)) {
    OV5642_StatusTypeDef status = SCCB_write_reg(config[i][0], (uint8_t)config[i][1]);
    if(status != OV5642_OK)
      return status;
    i++;
  }
  return OV5642_OK;
}
