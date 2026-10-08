#include "HMI.h"
#include <stdio.h>

/*==========================================================
 * 全局阈值（℃/%RH 整数，可被串口屏实时修改）
 * 默认值：温度上限30/下限0，湿度上限70/下限0
 *==========================================================*/
volatile uint8_t Temp_High = 30;     /* 温度上限（继电器1吸合点） */
volatile uint8_t Temp_Low  = 0;      /* 温度下限（仅供屏显示/预留） */
volatile uint8_t Humi_High = 70;     /* 湿度上限（继电器2吸合点） */
volatile uint8_t Humi_Low  = 0;      /* 湿度下限（仅供屏显示/预留） */
volatile uint8_t HMI_ThresholdChanged = 0; /* 屏改过阈值→1，主循环回写屏后清0 */

/*==========================================================
 * 记录功能状态（屏"记录测量页"的开始/停止按钮控制）
 *==========================================================*/
volatile uint16_t Record_Interval = 30;   /* 记录间隔（秒），默认30 */
volatile uint16_t Record_Total    = 200;  /* 总记录点数，默认200 */
volatile uint16_t Record_Count    = 0;    /* 已记录点数 */
volatile uint8_t  Record_Running  = 0;    /* 0=停止 1=记录中 */
volatile uint32_t Record_NextTime = 0;    /* 下一次采样时间（g_runSeconds 时间轴） */
volatile uint32_t Record_StartTime = 0;   /* 开始记录时刻（g_runSeconds 时间轴） */
volatile uint32_t Record_TotalTime = 0;   /* 本次记录总时长（秒）= (总点数-1)×间隔 */

/******************************************************************************
 * USART1 初始化：PA9=TX, PA10=RX, 115200 8N1
 * 接到 10寸淘晶驰(TJC) X5 串口屏（USART HMI 协议，TTL 电平，需共地）
 * 开启接收中断：屏滑滑块发出的阈值帧由 USART1_IRQHandler → HMI_ParseByte 解析
 *****************************************************************************/
void HMI_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1 | RCC_APB2Periph_AFIO, ENABLE);

    /* PA9 = USART1_TX 复用推挽输出 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA10 = USART1_RX 浮空输入 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = HMI_BAUDRATE;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    /* 接收中断：屏 → 单片机 阈值帧 */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    /* NVIC（分组须在所有 NVIC_Init 之前调用；main 里 TIM2 也用 NVIC，此处在先） */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART1, ENABLE);
}

/******************************************************************************
 * 发送一个字节（阻塞等待发送寄存器空）
 *****************************************************************************/
static void HMI_SendByte(uint8_t ch)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, ch);
}

/******************************************************************************
 * 发送一条指令字符串，末尾自动追加 0xFF 0xFF 0xFF（TJC 帧结束符）
 *****************************************************************************/
void HMI_SendString(const char *str)
{
    while (*str)
    {
        HMI_SendByte((uint8_t)(*str++));
    }
    HMI_SendByte(0xFF);
    HMI_SendByte(0xFF);
    HMI_SendByte(0xFF);
}

/******************************************************************************
 * 往任意文本控件写一个字符串
 * 拼成指令： ctrl.txt="str" + 0xFF 0xFF 0xFF
 *****************************************************************************/
void HMI_SendText(const char *ctrl, const char *str)
{
    char buf[48];

    sprintf(buf, "%s.txt=\"%s\"", ctrl, str);
    HMI_SendString(buf);
}

/******************************************************************************
 * 往任意文本控件写一个整数
 * 拼成指令： ctrl.txt="1234" + 0xFF 0xFF 0xFF
 *****************************************************************************/
void HMI_SendNum(const char *ctrl, uint16_t num)
{
    char buf[48];

    sprintf(buf, "%s.txt=\"%d\"", ctrl, num);
    HMI_SendString(buf);
}

/******************************************************************************
 * 切换图片控件的图片：ctrl.pic=pic + 0xFF 0xFF 0xFF
 * （如 pEval.pic=39 → Main 页笑脸变红脸）
 *****************************************************************************/
void HMI_SendPic(const char *ctrl, uint16_t pic)
{
    char buf[32];

    sprintf(buf, "%s.pic=%d", ctrl, pic);
    HMI_SendString(buf);
}

/******************************************************************************
 * 把 ×10 温度格式化成 "±xx.x" 字符串（如 255→"25.5"，-55→"-5.5"）
 *****************************************************************************/
static void HMI_FmtTemp(char *out, int16_t temp_x10)
{
    int32_t v = temp_x10;

    if (v < 0)
    {
        *out++ = '-';
        v = -v;
    }
    sprintf(out, "%d.%d", (int)(v / 10), (int)(v % 10));
}

/******************************************************************************
 * 发送平均温度（temp_x10 = 温度×10，可为负）
 *****************************************************************************/
void HMI_SendTemp(int16_t temp_x10)
{
    HMI_SendTempTo(HMI_TEMP_CTRL, temp_x10);
}

/******************************************************************************
 * 发送温度到指定控件（temp_x10 = 温度×10，可为负）
 *****************************************************************************/
void HMI_SendTempTo(const char *ctrl, int16_t temp_x10)
{
    char num[8];

    HMI_FmtTemp(num, temp_x10);
    HMI_SendText(ctrl, num);
}

/******************************************************************************
 * 发送平均湿度（humi_x10 = 湿度×10，非负）
 *****************************************************************************/
void HMI_SendHumi(uint16_t humi_x10)
{
    HMI_SendHumiTo(HMI_HUMI_CTRL, humi_x10);
}

/******************************************************************************
 * 发送湿度到指定控件（humi_x10 = 湿度×10，非负）
 *****************************************************************************/
void HMI_SendHumiTo(const char *ctrl, uint16_t humi_x10)
{
    char num[8];

    sprintf(num, "%d.%d", humi_x10 / 10, humi_x10 % 10);
    HMI_SendText(ctrl, num);
}

/******************************************************************************
 * 温度 → 波形曲线数据点
 * 把 0~50℃ 线性映射到波形控件的 0~255（TJC 波形 add 指令的数据范围），
 * 越界钳位（<0℃ → 0，>50℃ → 255）。
 * 拼成指令： add s1.id,0,<0~255> + 0xFF 0xFF 0xFF
 *****************************************************************************/
void HMI_SendWaveTemp(int16_t temp_x10)
{
    int32_t v = (int32_t)temp_x10 * 255 / 500;  /* ×10 → 0~255（50.0℃ = 500 → 255） */
    char buf[40];

    if (v < 0)   v = 0;
    if (v > 255) v = 255;

    sprintf(buf, "add %s.id,0,%ld", HMI_SCOPE_CTRL, (long)v);
    HMI_SendString(buf);
}

/******************************************************************************
 * 湿度高低统计：发 "高：xx.x% 低：xx.x%" 到 Main 页 tHStat
 * （max_x10/min_x10 为 ×10 值，如 450 → "45.0"；中文按屏的 GBK 编码发送）
 *****************************************************************************/
void HMI_SendHumiHighLow(int16_t max_x10, int16_t min_x10)
{
    char buf[64];

    sprintf(buf, "%s.txt=\"\xB8\xDF\xA3\xBA%d.%d%% \xB5\xCD\xA3\xBA%d.%d%%\"",
            HMI_HUMI_HL_CTRL,
            (int)(max_x10 / 10), (int)(max_x10 % 10),
            (int)(min_x10 / 10), (int)(min_x10 % 10));
    HMI_SendString(buf);
}

/******************************************************************************
 * 串口屏 → 单片机 帧解析（USART1_IRQHandler 逐字节调用）
 *
 * 屏侧"弹起事件"脚本发的实际字节流：
 *   阈值滑块:  printh AA 01 + prints h0.val,1 + printh FF FF FF
 *              → AA 01 "30"(ASCII) FF FF FF [多余 FF 忽略]
 *   记录滑块:  AA 05 "30" / AA 06 "200"
 *   开始/停止: AA 10 00 FF FF FF / AA 11 00 FF FF FF（00 为占位字节）
 * 注意：prints 带小数位参数时会把值除 10^N 发出（72→"7.2"、200→"2.00"），
 *       本解析器忽略小数点、数字拼接还原（7,2→72、2,0,0→200），两种发法都兼容
 *
 * CMD: 01=温度上限 02=温度下限 03=湿度上限 04=湿度下限
 *      05=记录间隔 06=记录点数 10=开始记录 11=停止记录
 * 帧外的一切字节（屏上电握手、错误响应帧）都不以 AA 开头，自动被忽略
 *****************************************************************************/
void HMI_ParseByte(uint8_t data)
{
    static uint8_t  state  = 0;     /* 0=等AA 1=等CMD 2=收数据 3=等结束符 */
    static uint8_t  cmd    = 0;
    static uint16_t value  = 0;
    static uint8_t  digits = 0;
    static uint8_t  ff     = 0;

    switch (state)
    {
        case 0:                     /* 等帧头 AA */
            if (data == 0xAA)
            {
                state = 1;
            }
            break;

        case 1:                     /* 命令号 01~06 / 10 / 11 */
            if ((data >= 0x01 && data <= 0x06) || data == 0x10 || data == 0x11)
            {
                cmd    = data;
                value  = 0;
                digits = 0;
                ff     = 0;
                state  = 2;
            }
            else
            {
                state = 0;          /* 非法命令号，重头等 AA */
            }
            break;

        case 2:                     /* 数据区 */
            if (cmd == 0x10 || cmd == 0x11)
            {
                /* 开始/停止：AA 10 00 FF FF FF，00 是占位字节，吞掉 */
                if (data == 0xFF)   /* 兼容没带占位字节的写法 */
                {
                    ff = 1;
                }
                state = 3;
            }
            else if (data >= '0' && data <= '9' && digits < 4)
            {
                /* 数字拼接（含小数点后的数字）：
                   prints 带小数位时会把值除 10^N 再发（72 → "7.2"），
                   忽略小数点继续拼数字即可还原：'7','2' → 72 ✓
                   屏脚本改 ,0 后发纯整数 "72" 也同样正确 */
                value = value * 10 + (uint16_t)(data - '0');
                digits++;
            }
            else if (data == '.')
            {
                /* 忽略小数点，数字继续按上式拼接还原 */
            }
            else if (data == 0xFF)  /* 数据结束，开始收帧尾 */
            {
                ff    = 1;
                state = 3;
            }
            else
            {
                state = 0;          /* 非法数据 → 放弃本帧 */
            }
            break;

        case 3:                     /* FF FF FF 帧尾 */
            if (data == 0xFF)
            {
                if (++ff >= 3)
                {
                    if (cmd >= 0x01 && cmd <= 0x04)
                    {
                        HMI_HandleCommand(cmd, (uint8_t)value);       /* 阈值 */
                    }
                    else
                    {
                        HMI_HandleRecordCommand(cmd, value);           /* 记录 */
                    }
                    state = 0;
                }
            }
            else
            {
                state = 0;
            }
            break;

        default:
            state = 0;
            break;
    }
}

/******************************************************************************
 * 处理屏发来的阈值命令（范围钳制 + 上下限互锁）
 *****************************************************************************/
void HMI_HandleCommand(uint8_t cmd, uint8_t value)
{
    switch (cmd)
    {
        /* 温度上限：1~99℃，且必须 > 温度下限 */
        case 0x01:
            if (value < 1)  value = 1;
            if (value > 99) value = 99;
            if (value <= Temp_Low)
            {
                if (Temp_Low < 99) value = Temp_Low + 1;
                else               { Temp_Low = 98; value = 99; }
            }
            Temp_High = value;
            break;

        /* 温度下限：0~98℃，且必须 < 温度上限 */
        case 0x02:
            if (value > 98) value = 98;
            if (value >= Temp_High)
            {
                if (Temp_High > 0) value = Temp_High - 1;
                else               { Temp_High = 1; value = 0; }
            }
            Temp_Low = value;
            break;

        /* 湿度上限：1~99%，且必须 > 湿度下限 */
        case 0x03:
            if (value < 1)  value = 1;
            if (value > 99) value = 99;
            if (value <= Humi_Low)
            {
                if (Humi_Low < 99) value = Humi_Low + 1;
                else               { Humi_Low = 98; value = 99; }
            }
            Humi_High = value;
            break;

        /* 湿度下限：0~98%，且必须 < 湿度上限 */
        case 0x04:
            if (value > 98) value = 98;
            if (value >= Humi_High)
            {
                if (Humi_High > 0) value = Humi_High - 1;
                else               { Humi_High = 1; value = 0; }
            }
            Humi_Low = value;
            break;

        default:
            return;                 /* 未知命令：不动，也不回写 */
    }

    HMI_ThresholdChanged = 1;       /* 通知主循环把新阈值回写屏（滑块位置+显示框） */
}

/******************************************************************************
 * 把当前阈值同步到串口屏：滑块位置（h0~h3）+ 显示框（nTU~nHD）
 *****************************************************************************/
void HMI_UpdateThresholdDisplay(void)
{
    char buf[32];

    /* 先同步滑块位置，再刷新显示框 */
    sprintf(buf, "h0.val=%d", Temp_High);  HMI_SendString(buf);
    sprintf(buf, "h1.val=%d", Temp_Low);   HMI_SendString(buf);
    sprintf(buf, "h2.val=%d", Humi_High);  HMI_SendString(buf);
    sprintf(buf, "h3.val=%d", Humi_Low);   HMI_SendString(buf);

    sprintf(buf, "nTU.txt=\"%d\"", Temp_High);  HMI_SendString(buf);
    sprintf(buf, "nTD.txt=\"%d\"", Temp_Low);   HMI_SendString(buf);
    sprintf(buf, "nHU.txt=\"%d\"", Humi_High);  HMI_SendString(buf);
    sprintf(buf, "nHD.txt=\"%d\"", Humi_Low);   HMI_SendString(buf);
}

/******************************************************************************
 * 处理屏发来的记录命令
 *   05 = 记录间隔（h4 滑块 ×6，钳制 6~60 秒）
 *   06 = 总记录点数（h5 滑块 ×50，钳制 50~500 条）
 *   10 = 开始记录（计数清零，下一个主循环周期立即采第 1 个点）
 *   11 = 停止记录
 *****************************************************************************/
void HMI_HandleRecordCommand(uint8_t cmd, uint16_t value)
{
    switch (cmd)
    {
        case 0x05:                  /* 记录间隔 */
            if (value < 6)   value = 6;
            if (value > 60)  value = 60;
            Record_Interval = value;
            break;

        case 0x06:                  /* 总记录点数 */
            if (value < 50)  value = 50;
            if (value > 500) value = 500;
            Record_Total = value;
            break;

        case 0x10:                  /* 开始记录：计数清零、第1点立即采集、剩余时间开始倒计时 */
            Record_Count     = 0;
            Record_Running   = 1;
            Record_TotalTime = (Record_Total > 1)
                             ? (uint32_t)(Record_Total - 1) * Record_Interval
                             : 0;               /* 第1条立即采集，只需等后面 N-1 个间隔 */
            Record_StartTime = g_runSeconds;
            Record_NextTime  = g_runSeconds;    /* 主循环本轮立即采第1个点 */
            HMI_UpdateRecordDisplay();
            HMI_UpdateRemainTime();             /* 剩余时间立即显示总时长 */
            break;

        case 0x11:                  /* 停止记录：剩余时间归零 */
            Record_Running = 0;
            HMI_SendText(HMI_REC_LEFT_CTRL, "00:00:00");
            break;

        default:
            break;
    }
}

/******************************************************************************
 * 更新记录页显示：记录条数 tCnt、进度条 j0、百分比 tPct
 * （剩余时间 tLeft 由 HMI_UpdateRemainTime 每秒单独倒计时，不在这里刷）
 *****************************************************************************/
void HMI_UpdateRecordDisplay(void)
{
    char buf[32];
    uint16_t pct = (Record_Total > 0)
                 ? (uint16_t)((uint32_t)Record_Count * 100 / Record_Total)
                 : 0;

    if (pct > 100) pct = 100;

    HMI_SendNum(HMI_REC_CNT_CTRL, Record_Count);      /* 记录条数 */

    sprintf(buf, "%s.val=%d", HMI_REC_PROG_CTRL, pct);
    HMI_SendString(buf);                              /* 进度条 */

    sprintf(buf, "%s.txt=\"%d%%\"", HMI_REC_PCT_CTRL, pct);
    HMI_SendString(buf);                              /* 百分比文字 */
}

/******************************************************************************
 * 剩余时间倒计时（记录中每秒调用一次）：tLeft 显示 "HH:MM:SS"
 * remain = 总时长 - 已运行秒数，倒到 0 后保持 00:00:00
 *****************************************************************************/
void HMI_UpdateRemainTime(void)
{
    uint32_t elapsed;
    uint32_t remain;
    char buf[40];

    if (!Record_Running)
    {
        return;
    }

    elapsed = g_runSeconds - Record_StartTime;
    remain  = (elapsed >= Record_TotalTime) ? 0 : (Record_TotalTime - elapsed);

    sprintf(buf, "%02lu:%02lu:%02lu",
            (unsigned long)(remain / 3600),
            (unsigned long)(remain / 60 % 60),
            (unsigned long)(remain % 60));
    HMI_SendText(HMI_REC_LEFT_CTRL, buf);
}
