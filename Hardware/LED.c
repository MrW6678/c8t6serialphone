#include "stm32f10x.h"                  // Device header
#include "LED.h"

void LED_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;

	// 初始化GPIOB的继电器控制引脚 (PB0, PB1)
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	// 上电默认：两个继电器都断开（输出无效电平，极性由宏决定）
#if RELAY1_ACTIVE_LOW
	GPIO_SetBits(GPIOB, GPIO_Pin_0);    // 低电平触发：置高 = 断开
#else
	GPIO_ResetBits(GPIOB, GPIO_Pin_0);  // 高电平触发：置低 = 断开
#endif
#if RELAY2_ACTIVE_LOW
	GPIO_SetBits(GPIOB, GPIO_Pin_1);
#else
	GPIO_ResetBits(GPIOB, GPIO_Pin_1);
#endif
}

// 继电器1 (PB0)：吸合（输出有效电平，由 RELAY1_ACTIVE_LOW 决定）
void Relay1_ON(void)
{
#if RELAY1_ACTIVE_LOW
	GPIO_ResetBits(GPIOB, GPIO_Pin_0);
#else
	GPIO_SetBits(GPIOB, GPIO_Pin_0);
#endif
}

void Relay1_OFF(void)
{
#if RELAY1_ACTIVE_LOW
	GPIO_SetBits(GPIOB, GPIO_Pin_0);
#else
	GPIO_ResetBits(GPIOB, GPIO_Pin_0);
#endif
}

void Relay1_Turn(void)
{
	if (GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_0) == 0)
	{
		GPIO_SetBits(GPIOB, GPIO_Pin_0);
	}
	else
	{
		GPIO_ResetBits(GPIOB, GPIO_Pin_0);
	}
}

// 继电器2 (PB1)：吸合（输出有效电平，由 RELAY2_ACTIVE_LOW 决定）
void Relay2_ON(void)
{
#if RELAY2_ACTIVE_LOW
	GPIO_ResetBits(GPIOB, GPIO_Pin_1);
#else
	GPIO_SetBits(GPIOB, GPIO_Pin_1);
#endif
}

void Relay2_OFF(void)
{
#if RELAY2_ACTIVE_LOW
	GPIO_SetBits(GPIOB, GPIO_Pin_1);
#else
	GPIO_ResetBits(GPIOB, GPIO_Pin_1);
#endif
}

void Relay2_Turn(void)
{
	if (GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_1) == 0)
	{
		GPIO_SetBits(GPIOB, GPIO_Pin_1);
	}
	else
	{
		GPIO_ResetBits(GPIOB, GPIO_Pin_1);
	}
}
