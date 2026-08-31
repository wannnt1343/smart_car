#include <stdio.h>
#include <unistd.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "hal_bsp_ap3216c.h" 
#include "wifiiot_gpio.h"    
#include "wifiiot_gpio_ex.h" 

// 定义光强阈值：低于 50 认为天黑了
#define LIGHT_THRESHOLD 50 
// 定义接近传感器阈值：高于 50 认为有人/物体靠近
#define PROXIMITY_THRESHOLD 50 

// 根据原理图，LED 连接在 IO06 上
#define LED_GPIO WIFI_IOT_IO_NAME_GPIO_6 

void SmartLightTask(void)
{
    // 声明传感器数据变量：ir为红外，als为光强，ps为接近数据
    uint16_t ir = 0, als = 0, ps = 0;

    // 1. 初始化传感器
    AP3216C_Init(); 
    
    // 2. 初始化 GPIO 
    GpioInit(); 
    IoSetFunc(LED_GPIO, WIFI_IOT_IO_FUNC_GPIO_6_GPIO); 
    GpioSetDir(LED_GPIO, WIFI_IOT_GPIO_DIR_OUT); 

    printf("Smart Light System Started!\n");

    while (1)
    {
        // 3. 读取传感器数据
        AP3216C_ReadData(&ir, &als, &ps);
        printf("光强(als)=%d, 接近(ps)=%d\r\n", als, ps);

        // 4. 核心判断逻辑：环境暗 (als < 50) 且 有人靠近 (ps > 50)
        if ((als < LIGHT_THRESHOLD) && (ps > PROXIMITY_THRESHOLD)) 
        {
            // 满足两个条件，输出高电平开灯
            GpioSetOutputVal(LED_GPIO, WIFI_IOT_GPIO_VALUE1);
            printf(">>> 环境暗且检测到靠近，开灯！\n");
        } 
        else 
        {
            // 其他情况（天亮，或者天黑但没物体靠近），输出低电平关灯
            GpioSetOutputVal(LED_GPIO, WIFI_IOT_GPIO_VALUE0);
            printf(">>> 关灯状态\n");
        }

        // 延时 1 秒，不断循环检测
        sleep(1); 
    }
}

// 任务创建入口函数
static void smart_light_entry(void)
{
    osThreadAttr_t options;
    options.name = "SmartLightTask";
    options.attr_bits = 0;
    options.cb_mem = NULL;
    options.cb_size = 0;
    options.stack_mem = NULL;
    options.stack_size = 1024 * 4; 
    options.priority = osPriorityNormal;

    osThreadNew((osThreadFunc_t)SmartLightTask, NULL, &options);
}

// 注册启动项
APP_FEATURE_INIT(smart_light_entry);