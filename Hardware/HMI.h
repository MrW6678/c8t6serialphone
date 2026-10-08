#ifndef __HMI_H
#define __HMI_H

#include "stm32f10x.h"

/* 淘晶驰(TJC) X5 串口屏驱动（USART HMI 协议：ASCII指令 + 0xFF 0xFF 0xFF 结束符） */

/* ★ 屏上各控件名，若要改，只改这里！★
 * 实测解析 串口屏_可编辑工程.HMI 的页面-控件布局：
 *   Main(主页)      : nTemp(当前温度) nHumi(当前湿度) tRun tStatus t5(时间) tDate
 *   RealTime(实时页): tNow/hNow(右侧两个"当前温度") tAvgT(温度平均) tAvgH(湿度平均) tAlarmN(报警次数)
 *   Recorder(记录页): nHumi2(当前湿度) tCnt(记录条数) nTemp2(当前温度)
 *   Setting(设置页) : nTU(温度上限) nTD(温度下限) nHU(湿度上限) nHD(湿度下限) h0~h3(滑块)
 * 注意：TJC 只能修改"当前显示页面"上的控件，发给其他页面控件会返回错误帧(无影响)。 */
#define HMI_TEMP_CTRL   "nTemp"  /* Main 页 平均温度控件 */
#define HMI_TEMP_CTRL2  "tAvgT"  /* RealTime 页 平均温度控件 */
#define HMI_HUMI_CTRL   "nHumi"  /* Main 页 平均湿度控件 */
#define HMI_HUMI_CTRL2  "tAvgH"  /* RealTime 页 平均湿度控件 */
#define HMI_RUNTIME_CTRL "tRun"  /* Main 页 上电运行时长控件（"00:00"） */
#define HMI_CLOCK_CTRL   "t5"    /* Main 页 实时时钟控件（右下角，"14:30"） */
#define HMI_DATE_CTRL    "tDate" /* Main 页 日期控件（"10-03 周六"） */
#define HMI_HUMI_HL_CTRL "tHStat" /* Main 页 湿度高低统计框（demo 默认"高45 低20"）——若屏上不显示，点该框核对控件名 */
#define HMI_RT_T1_CTRL  "tNow"   /* RealTime 页 右侧"当前温度"①大数值框（传感器1温度） */
#define HMI_RT_T2_CTRL  "hNow"   /* RealTime 页 右侧"当前温度"②大数值框（传感器2温度） */
#define HMI_REC_TEMP_CTRL "nTemp2" /* Recorder 页 当前温度（平均值，一路成功就用那路） */
#define HMI_REC_HUMI_CTRL "nHumi2" /* Recorder 页 当前湿度（平均值，一路成功就用那路） */
#define HMI_REC_CNT_CTRL  "tCnt"   /* Recorder 页 记录条数（点"开始"后从0累计，由记录功能更新） */
#define HMI_REC_LEFT_CTRL "tLeft"  /* Recorder 页 剩余时间 "HH:MM:SS" */
#define HMI_REC_PROG_CTRL "j0"     /* Recorder 页 进度条 */
#define HMI_REC_PCT_CTRL  "tPct"   /* Recorder 页 百分比文字 "N%" */
#define HMI_SCOPE_CTRL    "s1"     /* RealTime 页 波形(曲线)控件名 —— 实测旧工程里叫 s0！若屏不画曲线，改这里 */
#define HMI_STATUS_CTRL   "tStatus" /* Main 页 状态文字（正常/温度超限/湿度超限/传感器离线） */
#define HMI_ALARM_CTRL    "tAlarmN" /* 报警次数控件（Main/RealTime 页同名，当前页生效） */
#define HMI_EVAL_PIC_CTRL "pEval"   /* Main 页 笑脸图片控件（2026-10-05 实测 USART_HMI_可编辑版.hmi 里存在，默认pic=40） */
#define HMI_EVAL_TXT_CTRL "tEval"   /* Main 页 笑脸旁评价文字 */
#define PIC_FACE_BAD      39        /* 红脸（异常/超限）图片编号 */
#define PIC_FACE_GOOD     40        /* 绿脸（正常）图片编号（新工程默认即为40） */
#define PIC_FACE_MID      41        /* 橙脸（接近阈值）图片编号 */

#define HMI_BAUDRATE    115200   /* 屏波特率，与 TJC 工程 Program.s 里 baud=115200 保持一致 */

/*==========================================================
 * 报警阈值（℃/%RH 整数，可在屏"阈值设置页"用滑块实时修改）
 *==========================================================*/
extern volatile uint8_t Temp_High;  /* 温度上限（继电器1吸合点） */
extern volatile uint8_t Temp_Low;   /* 温度下限（屏显示/预留） */
extern volatile uint8_t Humi_High;  /* 湿度上限（继电器2吸合点） */
extern volatile uint8_t Humi_Low;   /* 湿度下限（屏显示/预留） */
extern volatile uint8_t HMI_ThresholdChanged; /* 屏改过阈值→1，主循环回写屏显示后清0 */

/*==========================================================
 * 记录功能状态（屏"记录测量页"的开始/停止按钮控制）
 *==========================================================*/
extern volatile uint16_t Record_Interval;  /* 记录间隔（秒）：屏 h4 滑块 ×6，范围 6~60 */
extern volatile uint16_t Record_Total;     /* 总记录点数：屏 h5 滑块 ×50，范围 50~500 */
extern volatile uint16_t Record_Count;     /* 已记录点数（点"开始"后从 0 累计） */
extern volatile uint8_t  Record_Running;   /* 1=记录中 0=停止 */
extern volatile uint32_t Record_NextTime;  /* 下一次采样时间（g_runSeconds 运行秒时间轴） */
extern volatile uint32_t Record_StartTime; /* 开始记录时刻（g_runSeconds 时间轴） */
extern volatile uint32_t Record_TotalTime; /* 本次记录总时长（秒）= (总点数-1)×间隔 */

extern volatile uint32_t g_runSeconds;     /* 运行秒计数（main.c 中 TIM2 每秒+1） */

/* ---------- 发送 ---------- */
void HMI_Init(void);                          /* USART1(PA9=TX, PA10=RX) 初始化，115200 8N1，开接收中断 */
void HMI_SendString(const char *str);         /* 发一条指令，自动加 0xFF 0xFF 0xFF 结束符 */
void HMI_SendText(const char *ctrl, const char *str);   /* 往任意控件写字符串 */
void HMI_SendNum(const char *ctrl, uint16_t num);       /* 往任意控件写整数 */
void HMI_SendPic(const char *ctrl, uint16_t pic);       /* 切换图片控件图片（pEval.pic=39） */
void HMI_SendTemp(int16_t temp_x10);          /* 平均温度 → HMI_TEMP_CTRL（×10，可为负） */
void HMI_SendHumi(uint16_t humi_x10);         /* 平均湿度 → HMI_HUMI_CTRL（×10） */
void HMI_SendTempTo(const char *ctrl, int16_t temp_x10); /* 温度 → 指定控件（×10，可为负） */
void HMI_SendHumiTo(const char *ctrl, uint16_t humi_x10);/* 湿度 → 指定控件（×10） */
void HMI_SendWaveTemp(int16_t temp_x10);       /* 温度 → 波形曲线点（0~50℃ 映射 0~255，发 add 指令） */
void HMI_SendHumiHighLow(int16_t max_x10, int16_t min_x10); /* 湿度高低统计 → Main 页 tHStat */

/* ---------- 接收（屏 → 单片机 阈值帧） ---------- */
void HMI_ParseByte(uint8_t data);              /* 解析屏发来的字节（USART1_IRQHandler 逐字节调用） */
void HMI_HandleCommand(uint8_t cmd, uint8_t value); /* 处理完整阈值命令（含钳制+上下限互锁） */
void HMI_UpdateThresholdDisplay(void);         /* 把当前阈值同步回屏（滑块位置 h0~h3 + 显示框 nTU~nHD） */
void HMI_HandleRecordCommand(uint8_t cmd, uint16_t value); /* 处理记录命令（05间隔/06点数/10开始/11停止） */
void HMI_UpdateRecordDisplay(void);            /* 更新记录页：记录条数/进度条/百分比 */
void HMI_UpdateRemainTime(void);               /* 记录中每秒调用：tLeft 剩余时间倒计时 "HH:MM:SS" */

#endif
