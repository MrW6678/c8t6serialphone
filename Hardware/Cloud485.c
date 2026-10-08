#include "Cloud485.h"
#include "HMI.h"
#include <string.h>

/* USART3 receives even while sensor polling and HMI sends block main().
 * TIM3 detects RTU silence independently of the SysTick delay routines.
 * Interrupts only collect frames/send bytes; parsing runs in main(). */
static uint8_t rx[CLOUD_FRAME_CAPACITY];
static uint8_t pending[CLOUD_FRAME_CAPACITY];
static uint8_t tx[CLOUD_REPLY_CAPACITY];
static volatile uint16_t rx_length, pending_length, tx_length, tx_index;
static volatile uint8_t rx_bad, tx_busy;
static uint16_t registers[CLOUD_REGISTER_COUNT];

void Cloud485_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;
    TIM_TimeBaseInitTypeDef timer;
    NVIC_InitTypeDef nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3 | RCC_APB1Periph_TIM3, ENABLE);
    GPIO_PinRemapConfig(GPIO_PartialRemap_USART3, DISABLE);
    GPIO_PinRemapConfig(GPIO_FullRemap_USART3, DISABLE);
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);

    USART_StructInit(&uart);
    uart.USART_BaudRate = CLOUD485_BAUDRATE;
    USART_Init(USART3, &uart); /* 8 data bits, no parity, 1 stop bit */
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);

    /* Existing system_stm32f10x.c configures 72MHz, APB1 timer clock 72MHz. */
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = 72U - 1U;
    timer.TIM_Period = (35000000U + CLOUD485_BAUDRATE - 1U) /
                       CLOUD485_BAUDRATE + 1000U - 1U;
    TIM_TimeBaseInit(TIM3, &timer);
    TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    /* HMI_Init has already selected NVIC_PriorityGroup_2. */
    nvic.NVIC_IRQChannel = USART3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    nvic.NVIC_IRQChannel = TIM3_IRQn;
    nvic.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&nvic);
    USART_Cmd(USART3, ENABLE);
}

void USART3_IRQHandler(void)
{
    uint32_t status = USART3->SR;
    uint8_t byte;
    if (status & (USART_FLAG_RXNE | USART_FLAG_ORE | USART_FLAG_FE | USART_FLAG_NE))
    {
        byte = (uint8_t)USART3->DR; /* SR then DR also clears receive errors. */
        if (!tx_busy)
        {
            if (status & (USART_FLAG_ORE | USART_FLAG_FE | USART_FLAG_NE))
            {
                rx_bad = 1;
            }
            else if (rx_length < CLOUD_FRAME_CAPACITY)
            {
                rx[rx_length++] = byte;
            }
            else
            {
                rx_bad = 1;
            }
            TIM_SetCounter(TIM3, 0);
            TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
            TIM_Cmd(TIM3, ENABLE);
        }
    }
    if (USART_GetITStatus(USART3, USART_IT_TXE) != RESET)
    {
        if (tx_index < tx_length)
        {
            USART_SendData(USART3, tx[tx_index++]);
        }
        else
        {
            USART_ITConfig(USART3, USART_IT_TXE, DISABLE);
            USART_ITConfig(USART3, USART_IT_TC, ENABLE);
        }
    }
    if (USART_GetITStatus(USART3, USART_IT_TC) != RESET)
    {
        USART_ITConfig(USART3, USART_IT_TC, DISABLE);
        USART_ClearITPendingBit(USART3, USART_IT_TC);
        tx_busy = 0;
    }
}

void TIM3_IRQHandler(void)
{
    uint16_t i;
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_Cmd(TIM3, DISABLE);
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        if (!rx_bad && rx_length && !pending_length)
        {
            for (i = 0; i < rx_length; i++) pending[i] = rx[i];
            pending_length = rx_length;
        }
        rx_length = 0;
        rx_bad = 0;
    }
}

void Cloud485_Poll(void)
{
    uint8_t request[CLOUD_FRAME_CAPACITY];
    uint16_t length;
    uint32_t mask;
    if (!pending_length || tx_busy) return;
    mask = __get_PRIMASK();
    __disable_irq();
    length = pending_length;
    memcpy(request, pending, length);
    pending_length = 0;
    __set_PRIMASK(mask);
    length = CloudModbus_Reply(request, length, CLOUD485_SLAVE_ADDRESS,
                              registers, tx);
    if (!length) return;
    mask = __get_PRIMASK();
    __disable_irq();
    /* A standard master waits for this reply before starting another request. */
    TIM_Cmd(TIM3, DISABLE);
    rx_length = 0;
    rx_bad = 0;
    tx_length = length;
    tx_index = 0;
    tx_busy = 1;
    USART_ClearFlag(USART3, USART_FLAG_TC);
    USART_ITConfig(USART3, USART_IT_TXE, ENABLE);
    __set_PRIMASK(mask);
}

void Cloud485_Update(const WSD_Data *sensor1, uint8_t error1,
                     const WSD_Data *sensor2, uint8_t error2,
                     int16_t temperature, uint16_t humidity,
                     uint8_t valid, uint8_t relay_on, uint32_t seconds)
{
    /* Failed readings are zeroed; consumers must also check validity/status. */
    registers[0] = valid ? (uint16_t)temperature : 0;
    registers[1] = valid ? humidity : 0;
    registers[2] = error1 ? 0 : (uint16_t)sensor1->Temperature;
    registers[3] = error1 ? 0 : sensor1->Humidity;
    registers[4] = error2 ? 0 : (uint16_t)sensor2->Temperature;
    registers[5] = error2 ? 0 : sensor2->Humidity;
    registers[6] = error1;
    registers[7] = error2;
    registers[8] = valid;
    registers[9] = relay_on;
    registers[10] = relay_on;
    registers[11] = Temp_High;
    registers[12] = Temp_Low;
    registers[13] = Humi_High;
    registers[14] = Humi_Low;
    registers[15] = (uint16_t)(seconds >> 16);
    registers[16] = (uint16_t)seconds;
    registers[17] = Record_Running;
    registers[18] = Record_Count;
    registers[19] = 0x0100U; /* Register-map version 1.0 */
}
