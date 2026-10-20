#ifndef __SCCB_H
#define __SCCB_H

#include "dvp_cam.h"

OV5642_StatusTypeDef SCCB_write_reg(uint16_t reg_addr, uint8_t value);
OV5642_StatusTypeDef SCCB_read_reg(uint16_t reg_addr, uint8_t *val);

#endif /*__SCCB_H*/
