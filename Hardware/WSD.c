#include "WSD.h"
#include "Delay.h"

/* 接收超时时间：首字节 200ms，后续每字节 10ms（4800bps 单字节约 2.1ms） */
#define WSD_TIMEOUT_FIRST_US    200000
#define WSD_TIMEOUT_BYTE_US     10000

/******************************************************************************
 * USART2 初始化：PA2=TX, PA3=RX, 4800 8N1
 * 接到 TTL转485 自动收发模块（无 DE/RE 脚），软件无需控制收发方向
 *****************************************************************************/
void WSD_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    /* PA2 = USART2_TX 复用推挽输出 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA3 = USART2_RX 浮空输入 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_3;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = WSD_BAUDRATE;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    USART_Cmd(USART2, ENABLE);
}

/******************************************************************************
 * Modbus CRC16（多项式 0xA001，结果低字节在前——与手册示例一致）
 *****************************************************************************/
static uint16_t WSD_CRC16(uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i, j;

    for (i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/******************************************************************************
 * 等待接收一个字节：timeout_us 内收到返回 1，超时返回 0
 *****************************************************************************/
static uint8_t WSD_WaitReceiveByte(uint32_t timeout_us)
{
    while (timeout_us--)
    {
        if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET)
        {
            return 1;
        }
        Delay_us(1);
    }
    return 0;
}

/******************************************************************************
 * 读取指定地址的变送器温湿度（功能码 0x03，读寄存器 0x0000 湿度、0x0001 温度，各占 2 字节）
 * 问询帧：addr 03 00 00 00 02 CRC低 CRC高
 * 应答帧：addr 03 04 湿度高 湿度低 温度高 温度低 CRC低 CRC高
 *****************************************************************************/
uint8_t WSD_Read_Data(WSD_Data *data, uint8_t addr)
{
    uint8_t  txbuf[8], rxbuf[9];
    uint16_t crc;
    uint8_t  i;

    /* 1. 组帧：地址 + 功能码 0x03 + 起始寄存器 0x0000 + 寄存器数量 0x0002 */
    txbuf[0] = addr;
    txbuf[1] = 0x03;
    txbuf[2] = 0x00;
    txbuf[3] = 0x00;
    txbuf[4] = 0x00;
    txbuf[5] = 0x02;
    crc = WSD_CRC16(txbuf, 6);
    txbuf[6] = crc & 0xFF;      /* CRC 低字节在前 */
    txbuf[7] = crc >> 8;

    /* 2. 清空接收缓冲，丢弃总线残留数据 */
    while (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET)
    {
        (void)USART_ReceiveData(USART2);
    }
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) == SET)
    {
        (void)USART2->SR;
        (void)USART2->DR;
    }

    /* 3. 发送问询帧（自动收发模块在停止位后自动切回接收，无需软件控制方向） */
    for (i = 0; i < 8; i++)
    {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
        USART_SendData(USART2, txbuf[i]);
    }

    /* 4. 接收 9 字节应答帧 */
    if (WSD_WaitReceiveByte(WSD_TIMEOUT_FIRST_US) == 0)
    {
        return 1;               /* 应答超时 */
    }
    rxbuf[0] = USART_ReceiveData(USART2);
    for (i = 1; i < 9; i++)
    {
        if (WSD_WaitReceiveByte(WSD_TIMEOUT_BYTE_US) == 0)
        {
            return 1;           /* 应答超时 */
        }
        rxbuf[i] = USART_ReceiveData(USART2);
    }

    /* 5. 校验应答帧 */
    if (rxbuf[0] != addr)
    {
        return 3;               /* 地址不符 */
    }
    if (rxbuf[1] != 0x03)
    {
        return 4;               /* 功能码不符 */
    }
    if (rxbuf[2] != 0x04)
    {
        return 5;               /* 数据长度不符 */
    }
    if (WSD_CRC16(rxbuf, 7) != ((uint16_t)(rxbuf[8] << 8) | rxbuf[7]))
    {
        return 2;               /* CRC 错误 */
    }

    /* 6. 解析数据（手册：16 位数据高字节在前；温度低于 0℃ 以补码上传） */
    data->Humidity    = (uint16_t)((rxbuf[3] << 8) | rxbuf[4]);  /* 湿度 ×10 */
    data->Temperature = (int16_t )((rxbuf[5] << 8) | rxbuf[6]);  /* 温度 ×10 */

    return 0;
}
