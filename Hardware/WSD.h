#ifndef __WSD_H
#define __WSD_H

#include "stm32f10x.h"

/* 工业型壁挂液晶温湿度变送器 (RS-WS-N01-2D-LCD, Modbus-RTU) 驱动 */
#define WSD_ADDR1       0x01    /* 变送器1地址（出厂默认 0x01） */
#define WSD_ADDR2       0x02    /* 变送器2地址（十进制2 = 0x02，需提前用上位机/按键改好并断电重启） */
#define WSD_BAUDRATE    4800    /* 波特率（出厂默认 4800，8位数据、无校验、1停止位） */

typedef struct
{
    int16_t  Temperature;   /* 实际温度 ×10，单位 0.1℃（负数时变送器以补码上传） */
    uint16_t Humidity;      /* 实际湿度 ×10，单位 0.1%RH */
} WSD_Data;

void    WSD_Init(void);
uint8_t WSD_Read_Data(WSD_Data *data, uint8_t addr);  /* addr: 变送器地址 (1~254) */
/* 返回值：0=成功, 1=应答超时, 2=CRC校验错误, 3=地址不符, 4=功能码不符, 5=数据长度不符 */

#endif
