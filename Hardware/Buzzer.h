#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32f10x.h"

// 蜂鸣器引脚：PA11，低电平触发（有源蜂鸣器）
#define BUZZER_PIN     GPIO_Pin_11
#define BUZZER_PORT    GPIOA

void Buzzer_Init(void);
void Buzzer_ON(void);    // 低电平 → 鸣叫
void Buzzer_OFF(void);   // 高电平 → 停止
void Buzzer_Turn(void);  // 翻转

#endif
