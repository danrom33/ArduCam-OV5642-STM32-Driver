# OV5642 DVP Driver (STM32L4)

Driver for the OV5642 sensor over its parallel DVP interface, using the STM32 DCMI peripheral with DMA for image data and I2C (SCCB) for sensor configuration. It outputs single JPEG snapshots.

It is the DVP counterpart of [OV5642-SPI-Driver](https://github.com/danrom33/OV5642-SPI-Driver) and exposes the same `OV5642_*` API, so application code can switch between the two with minimal changes. The two libraries define the same symbols, so **link only one of them** into a firmware image.

The original bring-up program (clock tree, CubeMX MSP code, test `main.c`) is on the `main` branch.

## Layout

```
Inc/dvp_cam.h        Public API
Inc/sccb.h           SCCB (I2C) register access
Inc/camera_config.h  Register table declarations
Src/dvp_cam.c        Capture, configuration and DCMI callbacks
Src/sccb.c
Src/camera_config.c  Register tables
```

The driver depends only on the STM32L4 HAL (`stm32l4xx_hal.h`) with the DCMI, DMA and I2C modules enabled.

## Building as a library

Add the repo as a submodule, then add a cmake include alongside the other library includes, mirroring `spi_cam.cmake`:

```cmake
file(GLOB SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ArduCam-OV5642-STM32-Driver/Src/*.c")

add_library(DVP_CAM ${SRC_FILES})

target_include_directories(DVP_CAM
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ArduCam-OV5642-STM32-Driver/Inc"
)

target_link_libraries(DVP_CAM PUBLIC STM32CUBEL4_HAL)
```

## What the application must provide

The driver does not initialise any peripherals. Before calling `OV5642_Init`, the host must:

1. **XCLK**: drive the sensor's XCLK. The tested setup uses MCO1 on PA8 with MSI at 24 MHz (`HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_MSI, RCC_MCODIV_1)`). The register tables assume a 24 MHz input clock.
2. **I2C**: initialise the I2C handle connected to the sensor's SCCB bus (7-bit address 0x3C, used as 0x78/0x79 in 8-bit form).
3. **DCMI**: initialise the DCMI handle with:
   | Field | Value |
   |---|---|
   | `SynchroMode` | `DCMI_SYNCHRO_HARDWARE` |
   | `PCKPolarity` | `DCMI_PCKPOLARITY_RISING` |
   | `VSPolarity` | `DCMI_VSPOLARITY_HIGH` |
   | `HSPolarity` | `DCMI_HSPOLARITY_LOW` |
   | `CaptureRate` | `DCMI_CR_ALL_FRAME` |
   | `ExtendedDataMode` | `DCMI_EXTEND_DATA_8B` |
   | `JPEGMode` | `DCMI_JPEG_ENABLE` |
4. **DMA**: in `HAL_DCMI_MspInit`, configure a DMA channel with `DMA_REQUEST_DCMI`, peripheral-to-memory, memory increment, **word** alignment on both sides, `DMA_NORMAL` mode, and link it with `__HAL_LINKDMA(hdcmi, DMA_Handle, hdma_dcmi)`.
5. **Interrupts**: enable `DCMI_IRQn` and the DMA channel IRQ, and call `HAL_DCMI_IRQHandler(&hdcmi)` / `HAL_DMA_IRQHandler(&hdma_dcmi)` from the corresponding handlers in `stm32l4xx_it.c`.

The driver defines `HAL_DCMI_FrameEventCallback` and `HAL_DCMI_ErrorCallback`, so the application must not.

## Usage

```c
#include "dvp_cam.h"

// Must be 4-byte aligned: DCMI DMA writes 32-bit words
static uint8_t frame_buffer[OV5642_MAX_CAPTURE_BYTES] __attribute__((aligned(4)));

OV5642_Init(&hi2c1, &hdcmi);

OV5642_StatusTypeDef status = OV5642_TestSerialConnection();
if (status != OV5642_OK) {
    printf("Camera not found: %s\r\n", OV5642_StatusToString(status));
}

OV5642_SetJpeg();
OV5642_SetResolution(OV5642_1024x768);

uint32_t image_length;
status = OV5642_TakePicture(frame_buffer, sizeof(frame_buffer), &image_length);
// On OV5642_OK, frame_buffer[0 .. image_length-1] holds one complete JPEG
```

## Capture behaviour and limits

- `OV5642_TakePicture` blocks until a frame arrives or `OV5642_CAPTURE_TIMEOUT_MS` (default 2000 ms, can be overridden at compile time) elapses.
- A single capture holds at most `OV5642_MAX_CAPTURE_BYTES` (262,140 bytes, the largest single DMA transfer of 0xFFFF words). Larger buffers are accepted but only that much is used. HAL can chain beyond this limit, but only with a second memory-to-memory DMA channel, and it leaves the end of a short JPEG in a scratch area, so the driver does not use that path.
- The JPEG length comes from the position of the EOI marker (`0xFF 0xD9`).

| Status | Meaning |
|---|---|
| `OV5642_OK` | Complete JPEG captured |
| `OV5642_BUFFER_OVERFLOW` | JPEG larger than the buffer. Reduce resolution or raise the quantisation scale (`0x4407`) |
| `OV5642_DCMI_ERROR` | DCMI overrun with buffer space left (DMA starved), or a DMA/sync error |
| `OV5642_DCMI_TIMEOUT` | No frame within the timeout: check XCLK, PCLK, VSYNC wiring, or whether the sensor is configured |
| `OV5642_ERROR` | Bad arguments (unaligned buffer), or the frame ended without an EOI marker. `image_length` is then the number of bytes received |
| `OV5642_I2C_TIMEOUT` | SCCB transaction timed out, or the sensor ID did not read back as 0x5642 |
