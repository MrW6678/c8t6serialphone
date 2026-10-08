#ifndef __LED_H
#define __LED_H

// 继电器触发极性配置：
//   0 = 高电平触发（IN=1 → 吸合，按模块丝印默认）
//   1 = 低电平触发（IN=0 → 吸合）
// ★ 烧录后如果某颗 LED 亮灭反了，说明该模块实际是低电平触发，
//   把对应宏改成 1 重新编译即可（例：继电器1反了 → RELAY1_ACTIVE_LOW 改成 1）★
// 实测（测试固件验证）：两个模块都是高电平触发，LED 都接 NO
#define RELAY1_ACTIVE_LOW   0
#define RELAY2_ACTIVE_LOW   0

void LED_Init(void);    // 继电器控制引脚初始化（PB0/PB1）

// 继电器1 (PB0) 控制函数（极性由 RELAY1_ACTIVE_LOW 决定）
void Relay1_ON(void);
void Relay1_OFF(void);
void Relay1_Turn(void);

// 继电器2 (PB1) 控制函数（极性由 RELAY2_ACTIVE_LOW 决定）
void Relay2_ON(void);
void Relay2_OFF(void);
void Relay2_Turn(void);

#endif
