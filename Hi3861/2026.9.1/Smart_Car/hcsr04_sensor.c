#include <stdio.h>

#include "wifiiot_gpio.h"
#include "hi_io.h"
#include "hi_time.h"

#include "hcsr04_sensor.h"


#define HCSR04_TRIG_GPIO    7
#define HCSR04_ECHO_GPIO    8

/*
 * 超时
 *
 * 单位 us
 */
#define HCSR04_TIMEOUT_US   30000


/*
 * 初始化 HC-SR04
 */
void HCSR04_Sensor_Init(void)
{
    GpioInit();

    /*
     * GPIO7 -> TRIG
     */
    hi_io_set_func(
        HI_IO_NAME_GPIO_7,
        HI_IO_FUNC_GPIO_7_GPIO
    );

    /*
     * GPIO8 -> ECHO
     */
    hi_io_set_func(
        HI_IO_NAME_GPIO_8,
        HI_IO_FUNC_GPIO_8_GPIO
    );

    /*
     * TRIG 输出
     */
    GpioSetDir(
        HCSR04_TRIG_GPIO,
        WIFI_IOT_GPIO_DIR_OUT
    );

    /*
     * ECHO 输入
     */
    GpioSetDir(
        HCSR04_ECHO_GPIO,
        WIFI_IOT_GPIO_DIR_IN
    );

    /*
     * TRIG 默认低
     */
    GpioSetOutputVal(
        HCSR04_TRIG_GPIO,
        WIFI_IOT_GPIO_VALUE0
    );

    printf("HC-SR04 init success\r\n");
}


/*
 * 测距
 */
float HCSR04_GetDistance(void)
{
    WifiIotGpioValue value =
        WIFI_IOT_GPIO_VALUE0;

    unsigned long wait_start;
    unsigned long echo_start;
    unsigned long echo_time;

    float distance;


    /*
     * 触发前先拉低
     */
    GpioSetOutputVal(
        HCSR04_TRIG_GPIO,
        WIFI_IOT_GPIO_VALUE0
    );

    hi_udelay(2);


    /*
     * 发送20us高电平
     */
    GpioSetOutputVal(
        HCSR04_TRIG_GPIO,
        WIFI_IOT_GPIO_VALUE1
    );

    hi_udelay(20);

    GpioSetOutputVal(
        HCSR04_TRIG_GPIO,
        WIFI_IOT_GPIO_VALUE0
    );


    /*
     * 等 Echo 变高
     */
    wait_start = hi_get_us();

    while (1)
    {
        GpioGetInputVal(
            HCSR04_ECHO_GPIO,
            &value
        );

        if (value == WIFI_IOT_GPIO_VALUE1)
        {
            break;
        }

        if (
            (hi_get_us() - wait_start)
            >
            HCSR04_TIMEOUT_US
        )
        {
            return 999.0f;
        }
    }


    /*
     * Echo 高电平开始时间
     */
    echo_start = hi_get_us();


    /*
     * 等 Echo 重新变低
     */
    while (1)
    {
        GpioGetInputVal(
            HCSR04_ECHO_GPIO,
            &value
        );

        if (value == WIFI_IOT_GPIO_VALUE0)
        {
            break;
        }

        if (
            (hi_get_us() - echo_start)
            >
            HCSR04_TIMEOUT_US
        )
        {
            return 999.0f;
        }
    }


    echo_time =
        hi_get_us() - echo_start;


    /*
     * cm
     */
    distance =
        echo_time * 0.034f / 2.0f;


    /*
     * 一些明显异常的数据直接当无效
     */
    if (distance < 2.0f)
    {
        return 999.0f;
    }

    if (distance > 400.0f)
    {
        return 999.0f;
    }


    return distance;
}