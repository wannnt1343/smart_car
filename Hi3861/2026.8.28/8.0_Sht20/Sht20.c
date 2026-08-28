#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "hal_bsp_sht20.h"     // 引入温湿度传感器头文件
#include "hal_bsp_ssd1306.h"   // 引入OLED屏幕头文件

osSemaphoreId_t sem1;

// 线程1：定时释放信号量
void thread1(void)
{
    while (1)
    {
        osSemaphoreRelease(sem1);
        printf("\nThread1释放信号量!\n");
        osDelay(300); // 延时3秒 (osDelay(100)相当于1秒)
    }
}

// 线程2：读取温湿度并显示在OLED上
void thread2(void)
{
    float temperature = 0, humidity = 0;
    uint8_t displayBuff[32] = {0};

    printf("i2c_sht20_demo()");
    
    // 初始化温湿度传感器
    SHT20_Init(); 
    
    // 初始化OLED屏幕
    SSD1306_Init();
    SSD1306_CLS(); // 清屏
    SSD1306_ShowStr(0, 0, (uint8_t *)"Temp & Humi", 16); // 屏幕第一行显示标题

    while (1)
    {
        // 等待信号量
        osSemaphoreAcquire(sem1, osWaitForever);
        
        // 读取温湿度
        SHT20_ReadData(&temperature, &humidity);
        
        // 串口打印 (方便在电脑上检查是否读到数据)
        printf("temperature = %.2f  humidity = %.2f\r\n", temperature, humidity);
        
        // 格式化温度数据，并显示在OLED第2行 (y=2)
        memset(displayBuff, 0, sizeof(displayBuff));
        sprintf((char*)displayBuff, "T: %.2f C", temperature);
        SSD1306_ShowStr(0, 2, (uint8_t *)displayBuff, 16);

        // 格式化湿度数据，并显示在OLED第4行 (y=4)
        memset(displayBuff, 0, sizeof(displayBuff));
        sprintf((char*)displayBuff, "H: %.2f %%", humidity);
        SSD1306_ShowStr(0, 4, (uint8_t *)displayBuff, 16);

        osDelay(1); // 延时10ms
    }
}

// 线程3：测试争抢信号量
void thread3(void)
{
    while (1)
    {
        osSemaphoreAcquire(sem1, osWaitForever);
        printf("Thread3 得到信号量!\n");
        osDelay(1);
    }
}

// 任务创建函数
static void i2c_sht20_demo(void)
{
    osThreadAttr_t attr;
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 1024 * 4;

    attr.name = "thread1";
    attr.priority = 25;
    if (osThreadNew((osThreadFunc_t)thread1, NULL, &attr) == NULL) {
        printf("Falied to create thread1!\n");
    }

    attr.name = "thread2";
    attr.priority = 25;
    if (osThreadNew((osThreadFunc_t)thread2, NULL, &attr) == NULL) {
        printf("Falied to create thread2!\n");
    }

    attr.name = "thread3";
    attr.priority = 25;
    if (osThreadNew((osThreadFunc_t)thread3, NULL, &attr) == NULL) {
        printf("Falied to create thread3!\n");
    }

    // 创建信号量，初始值为0，最大值为4
    sem1 = osSemaphoreNew(4, 0, NULL);
    if (sem1 == NULL) {
        printf("Falied to create Semaphore1!\n");
    }
}

APP_FEATURE_INIT(i2c_sht20_demo);