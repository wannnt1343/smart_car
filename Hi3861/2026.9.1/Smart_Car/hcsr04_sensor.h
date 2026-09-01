#ifndef __HCSR04_SENSOR_H__
#define __HCSR04_SENSOR_H__

/*
 * 初始化超声波
 *
 * GPIO7  -> TRIG
 * GPIO8  -> ECHO
 */
void HCSR04_Sensor_Init(void);


/*
 * 获取距离
 *
 * 单位：cm
 *
 * 如果超时：
 * 返回 999.0f
 */
float HCSR04_GetDistance(void);

#endif