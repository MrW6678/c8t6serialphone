#include "CloudModbus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void seal(uint8_t *frame, uint16_t length)
{
    uint16_t crc = CloudModbus_CRC16(frame, (uint16_t)(length - 2));
    frame[length - 2] = (uint8_t)crc;
    frame[length - 1] = (uint8_t)(crc >> 8);
}

int main(void)
{
    uint16_t registers[CLOUD_REGISTER_COUNT] = {253, 654, (uint16_t)-125};
    uint8_t request[8] = {1, 3, 0, 0, 0, 2, 0xC4, 0x0B};
    uint8_t reply[CLOUD_REPLY_CAPACITY + 1];
    uint8_t expected[9] = {1, 3, 4, 0, 253, 2, 142, 0, 0};
    uint16_t length;
    seal(expected, 9);
    assert(CloudModbus_CRC16(request, 6) == 0x0BC4);
    length = CloudModbus_Reply(request, 8, 1, registers, reply);
    assert(length == 9 && memcmp(reply, expected, 9) == 0);
    request[1] = 4;
    request[3] = 2;
    request[5] = 1;
    seal(request, 8);
    assert(CloudModbus_Reply(request, 8, 1, registers, reply) == 7);
    assert(reply[1] == 4 && reply[3] == 0xFF && reply[4] == 0x83);
    request[7] ^= 1;
    assert(CloudModbus_Reply(request, 8, 1, registers, reply) == 0);
    request[0] = 0; seal(request, 8);
    assert(CloudModbus_Reply(request, 8, 1, registers, reply) == 0);
    request[0] = 2; seal(request, 8);
    assert(CloudModbus_Reply(request, 8, 1, registers, reply) == 0);
    request[0] = 1; request[3] = 19; request[5] = 2; seal(request, 8);
    assert(CloudModbus_Reply(request, 8, 1, registers, reply) == 5);
    assert(reply[1] == 0x84 && reply[2] == 2);
    request[3] = 0; request[5] = 0; seal(request, 8);
    assert(CloudModbus_Reply(request, 8, 1, registers, reply) == 5);
    assert(reply[2] == 3);
    request[5] = 126; seal(request, 8);
    assert(CloudModbus_Reply(request, 8, 1, registers, reply) == 5);
    assert(reply[2] == 3);
    request[1] = 6; request[5] = 1; seal(request, 8);
    assert(CloudModbus_Reply(request, 8, 1, registers, reply) == 5);
    assert(reply[1] == 0x86 && reply[2] == 1 && registers[0] == 253);
    request[1] = 3; request[5] = CLOUD_REGISTER_COUNT; seal(request, 8);
    reply[CLOUD_REPLY_CAPACITY] = 0xA5;
    assert(CloudModbus_Reply(request, 8, 1, registers, reply) == CLOUD_REPLY_CAPACITY);
    assert(reply[CLOUD_REPLY_CAPACITY] == 0xA5);
    assert(CloudModbus_Reply(request, 3, 1, registers, reply) == 0);
    puts("Cloud Modbus tests passed");
    return 0;
}
