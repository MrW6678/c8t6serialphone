#ifndef CLOUD485_H
#define CLOUD485_H

#include "stm32f10x.h"
#include "WSD.h"
#include "CloudModbus.h"

/* Configure the TAS-KS-301 to exactly the same baud rate, 8N1. */
#define CLOUD485_BAUDRATE 9600U
#define CLOUD485_SLAVE_ADDRESS 1U

void Cloud485_Init(void);
void Cloud485_Poll(void);
void Cloud485_Update(const WSD_Data *sensor1, uint8_t error1,
                     const WSD_Data *sensor2, uint8_t error2,
                     int16_t temperature, uint16_t humidity,
                     uint8_t valid, uint8_t relay_on, uint32_t seconds);

#endif
