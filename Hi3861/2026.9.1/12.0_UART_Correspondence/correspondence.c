#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "ohos_init.h"
#include "cmsis_os2.h"

#include "wifiiot_uart.h"
#include "wifiiot_gpio.h"

#include "hi_io.h"


/*
 * 向 STM32 发送电机控制帧
 *
 * 格式：
 * FC A方向 A速度 B方向 B速度 FD
 *
 * 方向：
 * 0 = 正转
 * 1 = 反转
 *
 * 速度：
 * 0 ~ 150
 */
static void stm32motor_control(int motorA, int motorB)
{
    unsigned char buf[6];

    unsigned char dirA = 0;
    unsigned char dirB = 0;


    // =========================================
    // A 电机方向
    // =========================================

    if (motorA < 0)
    {
        dirA = 1;
        motorA = -motorA;
    }


    // =========================================
    // B 电机方向
    // =========================================

    if (motorB < 0)
    {
        dirB = 1;
        motorB = -motorB;
    }


    // =========================================
    // 限制最大速度
    // =========================================

    if (motorA > 150)
    {
        motorA = 150;
    }

    if (motorB > 150)
    {
        motorB = 150;
    }


    // =========================================
    // 组帧
    // =========================================

    buf[0] = 0xFC;

    buf[1] = dirA;
    buf[2] = (unsigned char)motorA;

    buf[3] = dirB;
    buf[4] = (unsigned char)motorB;

    buf[5] = 0xFD;


    // =========================================
    // UART2 发送
    // =========================================

    UartWrite(
        WIFI_IOT_UART_IDX_2,
        buf,
        6
    );
}


/*
 * 八字运动线程
 */
static void figure8_thread(void *arg)
{
    (void)arg;


    while (1)
    {
        // =====================================
        // 左半圈：A慢，B快
        // =====================================

        stm32motor_control(20, 150);

        usleep(2500000);


        // =====================================
        // 左半圈反向
        // =====================================

        stm32motor_control(-20, -150);

        usleep(2500000);


        // =====================================
        // 中间停车
        // =====================================

        stm32motor_control(0, 0);

        usleep(500000);


        // =====================================
        // 右半圈：A快，B慢
        // =====================================

        stm32motor_control(150, 20);

        usleep(2500000);


        // =====================================
        // 右半圈反向
        // =====================================

        stm32motor_control(-150, -20);

        usleep(2500000);


        // =====================================
        // 停车
        // =====================================

        stm32motor_control(0, 0);

        usleep(2000000);
    }
}


/*
 * 初始化
 */
static void correspondence(void)
{
    WifiIotUartAttribute uart_attr;

    osThreadAttr_t attr;


    // GPIO 初始化
    GpioInit();


    // =========================================
    // GPIO11 = UART2 TX
    // GPIO12 = UART2 RX
    // =========================================

    hi_io_set_func(
        HI_IO_NAME_GPIO_11,
        HI_IO_FUNC_GPIO_11_UART2_TXD
    );

    hi_io_set_func(
        HI_IO_NAME_GPIO_12,
        HI_IO_FUNC_GPIO_12_UART2_RXD
    );


    // =========================================
    // UART2
    // =========================================

    uart_attr.baudRate =
        115200;

    uart_attr.dataBits =
        WIFI_IOT_UART_DATA_BIT_8;

    uart_attr.stopBits =
        WIFI_IOT_UART_STOP_BIT_1;

    uart_attr.parity =
        WIFI_IOT_UART_PARITY_NONE;


    UartInit(
        WIFI_IOT_UART_IDX_2,
        &uart_attr,
        NULL
    );


    // =========================================
    // 创建线程
    // =========================================

    memset(
        &attr,
        0,
        sizeof(attr)
    );

    attr.name =
        "figure8_thread";

    attr.stack_size =
        4096;

    attr.priority =
        25;


    if (
        osThreadNew(
            figure8_thread,
            NULL,
            &attr
        )
        == NULL
    )
    {
        printf("create figure8_thread failed\r\n");
    }
    else
    {
        printf("figure8_thread start\r\n");
    }
}


APP_FEATURE_INIT(correspondence);