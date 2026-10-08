#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include <string.h>
#include "Delay.h"
#include "OLED.h"
#include "WSD.h"
#include "LED.h"
#include "HMI.h"
#include "Cloud485.h"

// 报警逻辑（简单阈值、无回差，阈值可在串口屏"阈值设置页"用滑块实时修改）：
//   平均温度 ≥ Temp_High 或 平均湿度 ≥ Humi_High → 两个继电器(PB0/PB1)同时吸合
//   两者都低于各自上限 → 两个同时释放（每轮重判，改大阈值后最多1秒释放）
// 阈值默认值见 HMI.c（Temp_High=30, Temp_Low=0, Humi_High=70, Humi_Low=0）
// 注：Temp_Low/Humi_Low 目前不参与继电器判断（仅屏上显示/存储），以后要回差再启用
// 屏改阈值 → USART1 中断 → HMI_ParseByte 更新，并回写屏显示
// 两路都通信失败时两个继电器都断开（安全）

/******************************************************************************
 * 接线图 (Wiring Diagram)
 * ============================================================================
 *  STM32F103C8T6 (Blue Pill)            TTL转485自动收发模块（无DE/RE脚）
 *  ---------------------------------      ---------------------------------
 *  PA2   (USART2_TX) ----------------    RXD  (接模块的数据输入脚！本模块丝印为"自身视角")
 *  PA3   (USART2_RX) ----------------    TXD  (接模块的数据输出脚！丝印TXD是模块自己的输出)
 *  5V    ----------------------------    VCC  (MAX485版接5V；SP3485版接3.3V)
 *  GND   ----------------------------    GND  (必须与变送器电源共地!)
 *
 *            485模块                         温湿度变送器 ×2（同一485总线）
 *            ---------                      --------------------------------
 *            A  ──┬──────────────────────   485A (变送器1，地址 0x01 = 十进制1)
 *                 ├──────────────────────   485A (变送器2，地址 0x02 = 十进制2)
 *            B  ──┼──────────────────────   485B (变送器1)
 *                 └──────────────────────   485B (变送器2)
 *                                            VCC ---- 12~24V 直流 (必须≥10V!)
 *                                            GND ---- 电源地 (两台并联，与485模块共地)
 *
 *  STM32F103C8T6                        OLED (0.96" SSD1306 I2C, 地址0x78)
 *  ---------------------------------      --------------------------------
 *  PB8   ----------------------------     SCL
 *  PB9   ----------------------------     SDA
 *  3.3V  ----------------------------     VCC  (严禁接5V!)
 *  GND   ----------------------------     GND  (与 STM32 GND 共地)
 *
 *  STM32F103C8T6                        10寸串口屏 (USART1, TTL串口)
 *  ---------------------------------      --------------------------------
 *  PA9   (USART1_TX) ----------------    RXD  (屏的串口接收脚)
 *  PA10  (USART1_RX) ----------------    TXD  (屏的串口发送脚)
 *  GND   ----------------------------    GND  (必须与 STM32 共地!)
 *  [独立电源] 5V/12V ----------------    VCC  (10寸屏功耗大，严禁从STM32取电)
 *
 *  STM32F103C8T6                        5V继电器模块 ×2 (DC+ DC- IN NO COM NC，高电平触发)
 *  ---------------------------------      --------------------------------------------
 *  PB0   ----------------------------     IN  (继电器1/S1：平均温度 ≥ 阈值 → 吸合)
 *  PB1   ----------------------------     IN  (继电器2/S2：与继电器1同步动作)
 *  5V    ----------------------------     DC+ (两继电器线圈并联供电)
 *  GND   ----------------------------     DC- (与 STM32 GND 共地)
 *  [触点侧] 每个继电器的 COM/NO 各串一条LED回路：
 *           5V → NO，COM → 电阻(~330Ω) → LED(+) → LED(−) → GND（吸合即亮）
 *
 *  注意：
 *  - Cloud: USART3 PB10(TX)/PB11(RX) -> separate auto-direction TTL/RS485
 *    module -> TAS-KS-301 A/B. Do not join the USART2 sensor RS485 bus.
 *    See README.md for TAS cloud setup (9600 8N1, Modbus slave address 1).
 *  - 串口分配：USART2(PA2/PA3) 读485变送器；USART1(PA9/PA10) 留给10寸串口屏。
 *  - 两台变送器地址必须不同（1 和 2），改地址后需断电重启，详见 WSD.h。
 *  - 485模块为自动收发，程序按普通串口收发即可，无需方向控制引脚。
 *  - 通信不上时排查顺序：① A/B 线互换 ② 共地检查 ③ 变送器供电≥10V。
 *  - 485 长距离布线时，总线两端各并联一个 120Ω 终端电阻。
 *  - 串口屏波特率需与 USART1 初始化一致（本屏为 115200，对应 TJC 工程 Program.s 里 baud=115200）。
 *  - 屏若为 5V TTL 电平：PA10(RX) 5V耐压可直接接；PA9(TX 3.3V) 给屏RX需确认屏认3.3V高电平，否则加电平转换。
 *****************************************************************************/

// 显示平均温度 "AvgT: xx.x C"（Temperature 为平均温度×10，可为负）
static void Display_Temp(uint8_t Line, int16_t Temperature)
{
    OLED_ShowString(Line, 1, "AvgT:");
    if (Temperature < 0)
    {
        OLED_ShowChar(Line, 6, '-');
        Temperature = -Temperature;
    }
    else
    {
        OLED_ShowChar(Line, 6, ' ');
    }
    OLED_ShowNum(Line, 7, (uint32_t)(Temperature / 10), 2);  // 整数部分
    OLED_ShowChar(Line, 9, '.');
    OLED_ShowNum(Line, 10, (uint32_t)(Temperature % 10), 1); // 小数部分
    OLED_ShowString(Line, 11, " C");
}

// 显示平均湿度 "AvgH: xx.x %RH"（Humidity 为平均湿度×10）
static void Display_Humi(uint8_t Line, uint16_t Humidity)
{
    OLED_ShowString(Line, 1, "AvgH:");
    OLED_ShowNum(Line, 6, Humidity / 10, 2);                 // 整数部分
    OLED_ShowChar(Line, 8, '.');
    OLED_ShowNum(Line, 9, Humidity % 10, 1);                 // 小数部分
    OLED_ShowString(Line, 10, " %RH");
}

// 错误码转3字符缩写（用于第3行状态显示）
static char *WSD_ErrName(uint8_t ErrorCode)
{
    switch (ErrorCode)
    {
        case 0:  return "OK ";  // 成功
        case 1:  return "TMO";  // 无应答超时
        case 2:  return "CRC";  // CRC错误
        case 3:  return "ADR";  // 地址不符
        case 4:  return "FNC";  // 功能码不符
        case 5:  return "LEN";  // 长度不符
        default: return "???";
    }
}

// 第3行显示两个传感器的通信状态："S1:OK S2:TMO"
static void Display_Status(uint8_t Err1, uint8_t Err2)
{
    OLED_ShowString(3, 1,  "S1:");
    OLED_ShowString(3, 4,  WSD_ErrName(Err1));
    OLED_ShowString(3, 8,  "S2:");
    OLED_ShowString(3, 11, WSD_ErrName(Err2));
}

// 发送单个传感器数据到串口屏：正常显示温度，通信失败显示错误码
static void Send_SensorToHMI(const char *Ctrl, uint8_t Err, int16_t Temp)
{
    if (Err == 0)
    {
        HMI_SendTempTo(Ctrl, Temp);            // 如 tNow.txt="25.4"
    }
    else
    {
        HMI_SendText(Ctrl, WSD_ErrName(Err));  // 如 tNow.txt="TMO"
    }
}

/******************************************************************************
 * 上电计时：TIM2 每 1 秒中断一次，g_runSeconds 累加
 * （Delay_ms 用的是 SysTick 轮询，不能做精确时基，所以用独立的 TIM2）
 *****************************************************************************/
volatile uint32_t g_runSeconds = 0;          // 上电后经过的秒数（HMI.c 记录功能也用）

/* ---------- 实时时钟（软RTC） ----------
 * Blue Pill 未焊 32.768kHz 晶振、无备份电池，无法做掉电走时的真RTC。
 * 这里用 TIM2 同一个秒中断维护“一天内的秒数”(0~86399)，走 8MHz 主晶振，
 * 精度约 ±20ppm（每天±2秒）。每次上电从固件编译时刻开始走。 */
static volatile uint32_t g_clock = 0;       // 当天已过秒数 0~86399

/* 解析编译时刻 __TIME__（格式 "HH:MM:SS"）作为上电初始时间 */
static uint32_t Clock_FromBuildTime(void)
{
    const char *t = __TIME__;
    uint32_t h = (uint32_t)((t[0] - '0') * 10 + (t[1] - '0'));
    uint32_t m = (uint32_t)((t[3] - '0') * 10 + (t[4] - '0'));
    uint32_t s = (uint32_t)((t[6] - '0') * 10 + (t[7] - '0'));

    return h * 3600 + m * 60 + s;
}

/* ---------- 日期（与实时时钟联动，跨天自动进位） ----------
 * 上电日期 = 固件编译日期 __DATE__（格式 "Oct  4 2026"）。
 * 时钟到 23:59:59 回绕 00:00:00 时日期+1（自动处理大小月/闰年），
 * 星期几由年月日实时计算，无需人工维护。 */
static volatile uint16_t g_dateY = 2026;   // 年
static volatile uint8_t  g_dateM = 10;     // 月
static volatile uint8_t  g_dateD = 4;      // 日

/* 星期文本（GBK编码字节转义：屏按GBK显示中文，与源文件编码无关）
 * 周日/周一/周二/周三/周四/周五/周六 */
static const char *const WEEK_GB[7] = {
    "\xD6\xDC\xC8\xD5",    /* 周日 */
    "\xD6\xDC\xD2\xBB",    /* 周一 */
    "\xD6\xDC\xB6\xFE",    /* 周二 */
    "\xD6\xDC\xC8\xFD",    /* 周三 */
    "\xD6\xDC\xCB\xC4",    /* 周四 */
    "\xD6\xDC\xCE\xE5",    /* 周五 */
    "\xD6\xDC\xC1\xF9",    /* 周六 */
};

/* 某年某月的天数（含闰年2月） */
static uint8_t Date_DaysInMonth(uint16_t y, uint8_t m)
{
    static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
    {
        return 29;
    }
    return mdays[m - 1];
}

/* 日期进一天（月/年自动进位） */
static void Date_Rollover(void)
{
    if (++g_dateD > Date_DaysInMonth(g_dateY, g_dateM))
    {
        g_dateD = 1;
        if (++g_dateM > 12)
        {
            g_dateM = 1;
            g_dateY++;
        }
    }
}

/* 计算星期几，返回 0=周日 ... 6=周六（Sakamoto算法） */
static uint8_t Date_Weekday(void)
{
    static const uint8_t t[12] = {0,3,2,5,0,3,5,1,4,6,2,4};
    uint16_t y = g_dateY;
    uint8_t  m = g_dateM;
    uint8_t  d = g_dateD;

    if (m < 3)
    {
        y--;
    }
    return (uint8_t)((y + y/4 - y/100 + y/400 + t[m-1] + d) % 7);
}

/* 解析编译日期 __DATE__（格式 "Oct  4 2026"）作为上电初始日期 */
static void Date_FromBuildTime(void)
{
    static const char mon[12][4] = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};
    const char *d = __DATE__;      /* "Mmm dd yyyy" */
    uint8_t i;

    for (i = 0; i < 12; i++)
    {
        if (d[0] == mon[i][0] && d[1] == mon[i][1] && d[2] == mon[i][2])
        {
            g_dateM = i + 1;
            break;
        }
    }
    g_dateD = (uint8_t)((d[4] == ' ' ? 0 : (d[4] - '0') * 10) + (d[5] - '0'));  /* 日期空格补位，如"Oct  4" */
    g_dateY = (uint16_t)((d[7] - '0') * 1000 + (d[8] - '0') * 100 +
                         (d[9] - '0') * 10 + (d[10] - '0'));
}

static void RunTime_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef        NVIC_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* 72MHz / 7200 = 10kHz，再 / 10000 = 1Hz → 每 1 秒一次更新中断 */
    TIM_TimeBaseStructure.TIM_Prescaler     = 7200 - 1;
    TIM_TimeBaseStructure.TIM_Period        = 10000 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_ClearFlag(TIM2, TIM_FLAG_Update);    // 清掉初始化时产生的标志，避免上电多计1秒
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM2, ENABLE);
}

/* TIM2 更新中断：每 1 秒进入一次，运行秒数+1，实时时钟+1秒（24小时回绕，日期进位） */
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        g_runSeconds++;
        if (++g_clock >= 86400)
        {
            g_clock = 0;                    // 到 23:59:59 后回到 00:00:00
            Date_Rollover();                // 日期进一天（跨月/跨年自动处理）
        }
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}

/* 把运行秒数格式化成 "HH:MM"（对应屏上 tRun 默认的 00:00 样式） */
static void Format_RunTime(char *out)
{
    uint32_t s = g_runSeconds;

    sprintf(out, "%02lu:%02lu",
            (unsigned long)(s / 3600),
            (unsigned long)(s / 60 % 60));
}

int main(void)
{
    WSD_Data wsd1 = {0}, wsd2 = {0};
    uint8_t  err1, err2;
    int16_t  temp_avg = 0;
    uint16_t humi_avg = 0;
    uint8_t  data_valid = 0;    // 本轮是否有有效数据
    uint8_t  relay_on = 0;
    uint16_t success_count = 0; // 两路都成功的次数
    uint16_t error_count   = 0; // 至少一路失败的次数
    uint8_t  boot_sync = 0;     // 开机前几轮把阈值同步到屏
    int16_t  humi_max = -1000;  // 上电以来最高湿度（×10，初值 -1000 保证第一次被覆盖）
    int16_t  humi_min = 10000;  // 上电以来最低湿度（×10，初值 10000 保证第一次被覆盖）
    uint32_t last_tleft_sec = 0; // 剩余时间上次刷新的秒数（每秒倒计时用）

    // 初始化
    LED_Init();          // 继电器初始化（PB0=继电器1, PB1=继电器2，高电平触发）
    OLED_Init();         // OLED初始化（PB8=SCL, PB9=SDA）
    WSD_Init();          // USART2初始化（PA2=TX, PA3=RX, 4800 8N1）
    HMI_Init();          // USART1初始化（PA9=TX, PA10=RX, 115200 8N1，接10寸串口屏）
    Cloud485_Init();     // USART3: PB10=TX, PB11=RX, 9600 8N1, TAS-KS-301
    RunTime_Init();      // TIM2初始化：上电开始计时（1秒中断）
    g_clock = Clock_FromBuildTime();   // 实时时钟初值 = 固件编译时刻
    Date_FromBuildTime();              // 日期初值 = 固件编译日期

    // 开机画面（第2行的版本号用于确认烧录的是新版固件）
    OLED_ShowString(1, 1, "RS-WS-N01 x2");
    OLED_ShowString(2, 1, "DUAL 485 V2.2");
    OLED_ShowString(3, 1, "Init...");
    Delay_ms(1000);
    OLED_Clear();

    while (1)
    {
        Cloud485_Poll();
        // 同一485总线上轮流读取两台变送器（地址不同，互不冲突）
        err1 = WSD_Read_Data(&wsd1, WSD_ADDR1);

        // Modbus 帧间隔：两帧之间必须静默 ≥3.5 字符时间（4800bps≈7.3ms），
        // 否则后一台设备会把两帧当成一帧连续数据丢弃 → 一直 Timeout！
        Delay_ms(10);

        err2 = WSD_Read_Data(&wsd2, WSD_ADDR2);

        data_valid = 0;

        if (err1 == 0 && err2 == 0)
        {
            // 两路都成功：求和取平均
            temp_avg = (int16_t )(((int32_t )wsd1.Temperature + wsd2.Temperature) / 2);
            humi_avg = (uint16_t)(((uint32_t)wsd1.Humidity + wsd2.Humidity) / 2);
            data_valid = 1;
            success_count++;
        }
        else if (err1 == 0)
        {
            // 只有传感器1成功：以它为准（传感器2故障，状态行可见）
            temp_avg = wsd1.Temperature;
            humi_avg = wsd1.Humidity;
            data_valid = 1;
            error_count++;
        }
        else if (err2 == 0)
        {
            // 只有传感器2成功：以它为准（传感器1故障，状态行可见）
            temp_avg = wsd2.Temperature;
            humi_avg = wsd2.Humidity;
            data_valid = 1;
            error_count++;
        }
        else
        {
            // 两路都失败：保留上次显示值，后续判断会关闭继电器
            error_count++;
        }

        Display_Status(err1, err2);

        // 串口屏：RealTime 页右侧两个"当前温度"（tNow=传感器1, hNow=传感器2），每轮都发
        // （Setting 阈值页的 nTD/nHD 不再发传感器数据，避免和阈值显示错位）
        Send_SensorToHMI(HMI_RT_T1_CTRL, err1, wsd1.Temperature);
        Send_SensorToHMI(HMI_RT_T2_CTRL, err2, wsd2.Temperature);

        if (data_valid)
        {
            // 显示平均值
            Display_Temp(1, temp_avg);
            Display_Humi(2, humi_avg);

            // 同步发送到10寸串口屏（×10 转成 "xx.x"）
            // Main 主页（nTemp/nHumi）和 RealTime 实时页（tAvgT/tAvgH）都发，
            // 屏当前停在哪页就更新哪页的控件，发给不在当前页的控件会被屏忽略
            HMI_SendTempTo(HMI_TEMP_CTRL,  temp_avg);
            HMI_SendTempTo(HMI_TEMP_CTRL2, temp_avg);
            HMI_SendHumiTo(HMI_HUMI_CTRL,  humi_avg);
            HMI_SendHumiTo(HMI_HUMI_CTRL2, humi_avg);

            // Recorder 记录页 当前温度/当前湿度（nTemp2/nHumi2），同样发平均值
            HMI_SendTempTo(HMI_REC_TEMP_CTRL, temp_avg);
            HMI_SendHumiTo(HMI_REC_HUMI_CTRL, humi_avg);

            // RealTime 页波形曲线：平均温度（0~50℃ → 0~255），每轮加一个点
            HMI_SendWaveTemp(temp_avg);

            // Main 页湿度高低统计：上电以来最高/最低湿度（×10）
            // 过滤 0 读数：变送器上电瞬间可能返回湿度 0，否则最低值会被锁死在 0.0%
            if (humi_avg > 0)
            {
                if (humi_avg > humi_max) { humi_max = humi_avg; }
                if (humi_avg < humi_min) { humi_min = humi_avg; }
            }
            if (humi_max >= 0)      /* 至少有过一次有效读数才发 */
            {
                HMI_SendHumiHighLow(humi_max, humi_min);
            }
        }

        // 继电器简单阈值控制（每轮重判，阈值由串口屏实时修改）：
        // 温度 ≥ 上限 或 湿度 ≥ 上限 → 两个继电器同时吸合
        // 两者都低于上限 → 同时释放（含把阈值调大的情况）
        // 两路都通信失败 → 全部断开（安全）
        if (data_valid &&
            (temp_avg >= (int16_t)Temp_High * 10 ||
             humi_avg >= (uint16_t)Humi_High * 10))
        {
            Relay1_ON();
            Relay2_ON();
            relay_on = 1;
        }
        else
        {
            Relay1_OFF();
            Relay2_OFF();
            relay_on = 0;
        }

        // 串口屏：Main 页状态文字 tStatus + 笑脸 pEval + 报警次数 tAlarmN
        // 超限判断含上下两侧（温度超上限或低于下限都算超限）；
        // 笑脸：超限/离线→红39；离上/下限≤3℃或≤8%RH→橙41；否则绿40
        // 报警次数 = 每次从正常进入超限 +1（边沿触发，停在超限不重复计数）
        {
            static uint8_t  alarm_active = 0;
            static uint16_t alarm_cnt    = 0;
            const char *st;
            uint16_t face;

            if (!data_valid)
            {
                st = "\xB4\xAB\xB8\xD0\xC6\xF7\xC0\xEB\xCF\xDF";   /* 传感器离线 */
            }
            else if (temp_avg > (int16_t)Temp_High * 10 ||
                     temp_avg < (int16_t)Temp_Low  * 10)
            {
                st = "\xCE\xC2\xB6\xC8\xB3\xAC\xCF\xDE";           /* 温度超限 */
            }
            else if (humi_avg > (uint16_t)Humi_High * 10 ||
                     humi_avg < (uint16_t)Humi_Low  * 10)
            {
                st = "\xCA\xAA\xB6\xC8\xB3\xAC\xCF\xDE";           /* 湿度超限 */
            }
            else
            {
                st = "\xD5\xFD\xB3\xA3";                           /* 正常 */
            }

            if (!data_valid)
            {
                face = PIC_FACE_BAD;
            }
            else if (strcmp(st, "\xD5\xFD\xB3\xA3") != 0)
            {
                face = PIC_FACE_BAD;                                /* 超限→红脸 */
            }
            else if (temp_avg >= ((int16_t)Temp_High - 3) * 10 ||
                     temp_avg <= ((int16_t)Temp_Low  + 3) * 10 ||
                     humi_avg >= ((uint16_t)Humi_High - 8) * 10 ||
                     humi_avg <= ((uint16_t)Humi_Low  + 8) * 10)
            {
                face = PIC_FACE_MID;                                /* 接近阈值→橙脸 */
            }
            else
            {
                face = PIC_FACE_GOOD;                               /* 正常→绿脸 */
            }
            HMI_SendPic(HMI_EVAL_PIC_CTRL, face);
            HMI_SendText(HMI_EVAL_TXT_CTRL, st);   /* 笑脸旁文字与状态一致 */
            HMI_SendText(HMI_STATUS_CTRL, st);     /* Main 页状态文字 */

            if (!data_valid)
            {
                alarm_active = 0;      /* 离线不算报警，恢复后重新判边沿 */
            }
            else if (strcmp(st, "\xD5\xFD\xB3\xA3") == 0)
            {
                alarm_active = 0;
            }
            else if (!alarm_active)
            {
                alarm_active = 1;
                alarm_cnt++;
            }

            HMI_SendNum(HMI_ALARM_CTRL, alarm_cnt);
        }

        // 第4行显示通信成功/失败计数（OLED）
        // 注：屏的 Main/RealTime 页没有计数控件（tCn/tIv 只在 Setting/Recorder 页），不发送
        OLED_ShowString(4, 1, "OK:");
        OLED_ShowNum(4, 4, success_count, 4);
        OLED_ShowString(4, 8, "Err:");
        OLED_ShowNum(4, 12, error_count, 4);

        // 记录模式：屏上点"开始"后，每 Record_Interval 秒采一次样并更新记录页
        // （记录条数 tCnt / 进度条 j0 / 百分比 tPct），剩余时间 tLeft 每秒倒计时，采满自动停止
        if (Record_Running)
        {
            /* 剩余时间：每秒刷新一次 */
            if ((uint32_t)(g_runSeconds - last_tleft_sec) >= 1)
            {
                last_tleft_sec = g_runSeconds;
                HMI_UpdateRemainTime();
            }

            /* 采样调度 */
            if (g_runSeconds >= Record_NextTime)
            {
                Record_NextTime = g_runSeconds + Record_Interval;

                if (data_valid)
                {
                    Record_Count++;
                    if (Record_Count >= Record_Total)
                    {
                        Record_Count   = Record_Total;
                        Record_Running = 0;      /* 采满自动停止，剩余时间归零 */
                        HMI_SendText(HMI_REC_LEFT_CTRL, "00:00:00");
                    }
                }
                HMI_UpdateRecordDisplay();
            }
        }

        // 串口屏：上电运行时长（Main 页 tRun，日期下方的时间栏），每轮刷新 "HH:MM"
        {
            char rt[8];
            Format_RunTime(rt);
            HMI_SendText(HMI_RUNTIME_CTRL, rt);
        }

        // 串口屏：实时时钟（Main 页 t5，右下角），每轮刷新 "HH:MM"
        {
            char ct[8];
            uint32_t c = g_clock;

            sprintf(ct, "%02lu:%02lu",
                    (unsigned long)(c / 3600),
                    (unsigned long)(c / 60 % 60));
            // 屏幕 RTC 自行刷新 t5，暂停发送 STM32 软件时钟，避免覆盖。
            // HMI_SendText(HMI_CLOCK_CTRL, ct);
        }

        // 串口屏：日期（Main 页 tDate），"MM-DD 周X"，星期按日期自动计算
        {
            char db[16];

            sprintf(db, "%02u-%02u ", (unsigned)g_dateM, (unsigned)g_dateD);
            strcat(db, WEEK_GB[Date_Weekday()]);
            // 屏幕 RTC 自行刷新 tDate，暂停发送 STM32 软件日期，避免覆盖。
            // HMI_SendText(HMI_DATE_CTRL, db);
        }

        // 开机前几轮把阈值同步到屏（屏开机慢，前几帧可能收不到，多同步几次）；
        // 屏滑滑块改阈值后（HMI_ThresholdChanged=1）也回写确认
        if (boot_sync < 5 || HMI_ThresholdChanged)
        {
            HMI_UpdateThresholdDisplay();
            HMI_ThresholdChanged = 0;
            if (boot_sync < 5)
            {
                boot_sync++;
            }
        }

        // 变送器温湿度刷新时间为1s，轮询周期取1s
        Cloud485_Update(&wsd1, err1, &wsd2, err2, temp_avg, humi_avg,
                        data_valid, relay_on, g_runSeconds);
        {
            uint16_t wait;
            for (wait = 0; wait < 100; wait++)
            {
                Cloud485_Poll();
                Delay_ms(10);
            }
        }
    }
}
