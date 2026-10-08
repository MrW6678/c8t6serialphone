#include "Buzzer.h"

// 蜂鸣器初始化：PA11，推挽输出，低电平触发
void Buzzer_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = BUZZER_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;  // 推挽输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BUZZER_PORT, &GPIO_InitStructure);

    Buzzer_OFF();  // 默认不鸣叫（高电平）
}

// 蜂鸣器鸣叫（低电平触发）
void Buzzer_ON(void)
{
    GPIO_ResetBits(BUZZER_PORT, BUZZER_PIN);
}

// 蜂鸣器停止（高电平）
void Buzzer_OFF(void)
{
    GPIO_SetBits(BUZZER_PORT, BUZZER_PIN);
}

// 蜂鸣器状态翻转
void Buzzer_Turn(void)
{
    if (GPIO_ReadOutputDataBit(BUZZER_PORT, BUZZER_PIN) == 0)
    {
        GPIO_SetBits(BUZZER_PORT, BUZZER_PIN);
    }
    else
    {
        GPIO_ResetBits(BUZZER_PORT, BUZZER_PIN);
    }
}
