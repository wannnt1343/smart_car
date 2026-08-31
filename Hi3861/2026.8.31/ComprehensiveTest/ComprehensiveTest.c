#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "hal_bsp_sht20.h"     
#include "hal_bsp_ssd1306.h"   
#include "hal_bsp_ap3216c.h"   
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "wifiiot_watchdog.h" 
#include "hi_io.h"             
#include "hi_time.h"           

// ----- 引脚与宏定义 -----
#define GPIOL 13
#define GPIOR 14
#define GPIO_8 8   // 超声波 Echo (接收)
#define GPIO_7 7   // 超声波 Trig (触发)
#define GPIO_FUNC 0

// ----- 超声波测距功能实现 -----
float GetDistance(void)
{
    static unsigned long start_time = 0, time = 0;
    float distance = 0.0;
    WifiIotGpioValue value = WIFI_IOT_GPIO_VALUE0;
    unsigned int flag = 0;

    hi_io_set_func(GPIO_8, GPIO_FUNC);
    GpioSetDir(GPIO_8, WIFI_IOT_GPIO_DIR_IN);  // GPIO_8设置为输入引脚[cite: 6]
    GpioSetDir(GPIO_7, WIFI_IOT_GPIO_DIR_OUT); // GPIO_7设置为输出引脚[cite: 6]

    // GPIO_7输出一个脉冲触发信号到超声波测距模块，至少10us[cite: 6]
    GpioSetOutputVal(GPIO_7, WIFI_IOT_GPIO_VALUE1);
    hi_udelay(20); //[cite: 6]
    GpioSetOutputVal(GPIO_7, WIFI_IOT_GPIO_VALUE0);

    // 测量回响信号(高电平)时间[cite: 6]
    while (1) {
        GpioGetInputVal(GPIO_8, &value);
        if (value == WIFI_IOT_GPIO_VALUE1 && flag == 0) {
            start_time = hi_get_us(); //[cite: 6]
            flag = 1;
        }
        if (value == WIFI_IOT_GPIO_VALUE0 && flag == 1) {
            time = hi_get_us() - start_time; //[cite: 6]
            start_time = 0;
            break;
        }
    }

    // 距离 = 高电平时间 * 0.034 / 2[cite: 6]
    distance = time * 0.034 / 2; 
    return distance;
}

// ----- 综合任务主体 -----
void ComprehensiveTask(void *parame)
{
    (void)parame;
    float temperature = 0, humidity = 0, distance = 0;
    uint16_t ir = 0, als = 0, ps = 0;
    WifiIotGpioValue left_val, right_val;
    uint8_t displayBuff[32] = {0};

    // 1. 初始化传感器与外设
    SHT20_Init();
    AP3216C_Init();
    SSD1306_Init();
    SSD1306_CLS();
    
    // 初始化红外循迹GPIO
    GpioInit();
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_13, WIFI_IOT_IO_FUNC_GPIO_13_GPIO);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_14, WIFI_IOT_IO_FUNC_GPIO_14_GPIO);
    GpioSetDir(WIFI_IOT_IO_NAME_GPIO_13, WIFI_IOT_GPIO_DIR_IN);
    GpioSetDir(WIFI_IOT_IO_NAME_GPIO_14, WIFI_IOT_GPIO_DIR_IN);

    printf("Start Comprehensive Sensor Test\r\n");

    while (1)
    {
        // 2. 轮询读取各类传感器数据
        SHT20_ReadData(&temperature, &humidity);
        AP3216C_ReadData(&ir, &als, &ps);
        GpioGetInputVal(GPIOL, &left_val);
        GpioGetInputVal(GPIOR, &right_val);
        distance = GetDistance();

        // 3. 串口打印综合信息 (方便在电脑端调试查看)
        printf("---------------------------\r\n");
        printf("Temp: %.2fC, Humi: %.2f%%\r\n", temperature, humidity);
        printf("Light: %d, Prox: %d\r\n", als, ps);
        printf("IR_L: %d, IR_R: %d\r\n", left_val, right_val);
        printf("Dist: %.1f cm\r\n", distance);
        
        // 4. OLED 屏幕多行更新展示
        // 第一行：温湿度 (0对应Y坐标第1行)
        memset(displayBuff, 0, sizeof(displayBuff));
        sprintf((char*)displayBuff, "T:%.1f H:%.1f", temperature, humidity);
        SSD1306_ShowStr(0, 0, (uint8_t *)displayBuff, 16);

        // 第二行：光照与接近 (2对应Y坐标第2行)
        memset(displayBuff, 0, sizeof(displayBuff));
        sprintf((char*)displayBuff, "Ligt:%d P:%d", als, ps);
        SSD1306_ShowStr(0, 2, (uint8_t *)displayBuff, 16);
        
        // 第三行：红外状态 (4对应Y坐标第3行)
        memset(displayBuff, 0, sizeof(displayBuff));
        sprintf((char*)displayBuff, "IR L:%d R:%d", left_val, right_val);
        SSD1306_ShowStr(0, 4, (uint8_t *)displayBuff, 16);

        // 第四行：超声波距离 (6对应Y坐标第4行)
        memset(displayBuff, 0, sizeof(displayBuff));
        sprintf((char*)displayBuff, "Dist:%.1f cm", distance);
        SSD1306_ShowStr(0, 6, (uint8_t *)displayBuff, 16);

        // 测量周期设定
        osDelay(200); 
    }
}

// ----- 任务创建入口 -----
static void Sensor_Demo_Entry(void)
{
    WatchDogDisable(); // 关闭看门狗[cite: 6]
    
    osThreadAttr_t attr;
    attr.name = "ComprehensiveTask";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 10240; // 适当调大栈空间以支持多个传感器[cite: 6]
    attr.priority = osPriorityNormal;

    if (osThreadNew((osThreadFunc_t)ComprehensiveTask, NULL, &attr) == NULL) {
        printf("Failed to create Task!\n");
    }
}
APP_FEATURE_INIT(Sensor_Demo_Entry);