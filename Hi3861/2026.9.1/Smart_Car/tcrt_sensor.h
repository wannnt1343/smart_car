#ifndef __TCRT_SENSOR_H__
#define __TCRT_SENSOR_H__

#include <stdint.h>

/*
 * 初始化左右两个 TCRT5000
 *
 * GPIO13
 * GPIO14
 */
void TCRT_Sensor_Init(void);


/*
 * 读取原始电平
 *
 * 返回：
 * 0 = 低电平
 * 1 = 高电平
 */
uint8_t TCRT_ReadLeftRaw(void);

uint8_t TCRT_ReadRightRaw(void);


/*
 * 防抖后的安全判断
 *
 * 返回：
 * 1 = 安全，有桌面
 * 0 = 危险，可能悬空
 */
uint8_t TCRT_LeftSafe(void);

uint8_t TCRT_RightSafe(void);

#endif