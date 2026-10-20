#include "sccb.h"
#include "camera_config.h"

#define SCCB_TIMEOUT_MS 100

extern OV5642_HandleTypeDef hov5642;

static OV5642_StatusTypeDef SCCB_status(HAL_StatusTypeDef status);

OV5642_StatusTypeDef SCCB_write_reg(uint16_t reg_addr, uint8_t value) {

  uint8_t data[3];
  data[0] = (reg_addr>>8) & 0xFF; //high byte of reg addr
  data[1] = reg_addr & 0xFF; //low byte of reg addr
  data[2] = value;

  HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(hov5642.hi2c, OV5642_WRITE_ADDR, data, 3, SCCB_TIMEOUT_MS);
  if(status != HAL_OK)
    return SCCB_status(status);
  // Inherited from the tested bring-up code; not required by the datasheet for most
  // registers, but removing it is untested.
  HAL_Delay(10);
  return OV5642_OK;
}

OV5642_StatusTypeDef SCCB_read_reg(uint16_t reg_addr, uint8_t *val){
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
    hov5642.hi2c,
    OV5642_WRITE_ADDR,
    reg_addr,
    I2C_MEMADD_SIZE_16BIT,
    val,
    1,
    SCCB_TIMEOUT_MS);
  return SCCB_status(status);
}

static OV5642_StatusTypeDef SCCB_status(HAL_StatusTypeDef status){
  if(status == HAL_OK)
    return OV5642_OK;
  if(status == HAL_TIMEOUT)
    return OV5642_I2C_TIMEOUT;
  return OV5642_ERROR;
}
