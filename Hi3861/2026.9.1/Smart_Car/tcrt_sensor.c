#include <stdio.h>
#include <unistd.h>

#include "wifiiot_gpio.h"
#include "hi_io.h"

#include "tcrt_sensor.h"


#define TCRT_LEFT_GPIO     13
#define TCRT_RIGHT_GPIO    14


/*
 * 初始化
 */
void TCRT_Sensor_Init(void)
{
    GpioInit();

    /*
     * GPIO13 普通 GPIO
     */
    hi_io_set_func(
        HI_IO_NAME_GPIO_13,
        HI_IO_FUNC_GPIO_13_GPIO
    );

    /*
     * GPIO14 普通 GPIO
     */
    hi_io_set_func(
        HI_IO_NAME_GPIO_14,
        HI_IO_FUNC_GPIO_14_GPIO
    );

    /*
     * 两个口都是输入
     */
    GpioSetDir(
        TCRT_LEFT_GPIO,
        WIFI_IOT_GPIO_DIR_IN
    );

    GpioSetDir(
        TCRT_RIGHT_GPIO,
        WIFI_IOT_GPIO_DIR_IN
    );

    printf("TCRT init success\r\n");
}


/*
 * 左边原始值
 */
uint8_t TCRT_ReadLeftRaw(void)
{
    WifiIotGpioValue value =
        WIFI_IOT_GPIO_VALUE0;

    GpioGetInputVal(
        TCRT_LEFT_GPIO,
        &value
    );

    if (value == WIFI_IOT_GPIO_VALUE1)
    {
        return 1;
    }

    return 0;
}


/*
 * 右边原始值
 */
uint8_t TCRT_ReadRightRaw(void)
{
    WifiIotGpioValue value =
        WIFI_IOT_GPIO_VALUE0;

    GpioGetInputVal(
        TCRT_RIGHT_GPIO,
        &value
    );

    if (value == WIFI_IOT_GPIO_VALUE1)
    {
        return 1;
    }

    return 0;
}


/*
 * 左边防抖
 *
 * 连续读取5次。
 *
 * 5次里面至少4次为0，
 * 才认为真的悬空。
 */
/*
 * 左侧 TCRT 防抖
 *
 * 当前实车已经确认：
 *
 * 0 = 有桌面，安全
 * 1 = 悬空，危险
 *
 * 连续采样5次，
 * 至少4次为1，
 * 才真正认为危险。
 */
uint8_t TCRT_LeftSafe(void)
{
    uint8_t danger_count = 0;
    uint8_t i;

    for (i = 0; i < 5; i++)
    {
        /*
         * 现在 1 才是危险
         */
        if (TCRT_ReadLeftRaw() == 1)
        {
            danger_count++;
        }

        usleep(2000);
    }

    /*
     * 5次里至少4次悬空
     */
    if (danger_count >= 4)
    {
        return 0;       // 不安全
    }

    return 1;           // 安全
}


/*
 * 右侧 TCRT 防抖
 */
uint8_t TCRT_RightSafe(void)
{
    uint8_t danger_count = 0;
    uint8_t i;

    for (i = 0; i < 5; i++)
    {
        /*
         * 1 = 悬空危险
         */
        if (TCRT_ReadRightRaw() == 1)
        {
            danger_count++;
        }

        usleep(2000);
    }

    if (danger_count >= 4)
    {
        return 0;       // 不安全
    }

    return 1;           // 安全
}