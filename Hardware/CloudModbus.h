#ifndef CLOUD_MODBUS_H
#define CLOUD_MODBUS_H

#include <stdint.h>

#define CLOUD_REGISTER_COUNT 20U
#define CLOUD_FRAME_CAPACITY 256U
#define CLOUD_REPLY_CAPACITY (5U + 2U * CLOUD_REGISTER_COUNT)

/* Read-only register bank; wire addresses start at zero. */
uint16_t CloudModbus_CRC16(const uint8_t *data, uint16_t length);
uint16_t CloudModbus_Reply(const uint8_t *request, uint16_t length,
                          uint8_t slave, const uint16_t *registers,
                          uint8_t *reply);

#endif
