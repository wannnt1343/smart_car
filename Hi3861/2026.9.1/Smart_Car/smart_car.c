#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "ohos_init.h"
#include "cmsis_os2.h"

#include "wifiiot_uart.h"
#include "wifiiot_gpio.h"

#include "hi_io.h"

#include "tcrt_sensor.h"
#include "hcsr04_sensor.h"


/*
 * =========================================================
 * 参数
 * =========================================================
 */

/*
 * 电机速度
 *
 * 暂时不要再调低。
 */
#define FORWARD_SPEED              60
#define BACK_SPEED                 100
#define TURN_SPEED                 100


/*
 * 超声波避障距离
 */
#define OBSTACLE_DISTANCE          20.0f


/*
 * 电机动作持续时间
 */
#define BACK_TIME_US               450000
#define TURN_TIME_US               300000
#define STOP_TIME_US               80000


/*
 * 主循环
 */
#define LOOP_DELAY_US              30000


/*
 * TCRT连续多少轮危险
 * 才真正认为到了桌边
 */
#define EDGE_CONFIRM_COUNT         1


/*
 * 超声波连续多少轮小于20cm
 * 才真正避障
 */
#define OBSTACLE_CONFIRM_COUNT     3


/*
 * 避障后的超声波冷却周期
 *
 * 15 * 大约30~50ms
 * 约0.5秒左右
 */
#define OBSTACLE_COOLDOWN_COUNT    15



/*
 * =========================================================
 * UART -> STM32
 *
 * 协议：
 *
 * FC A方向 A速度 B方向 B速度 FD
 *
 * 正数 = 正转
 * 负数 = 反转
 * =========================================================
 */
static void stm32motor_control(
    int motorA,
    int motorB
)
{
    unsigned char buf[6];

    unsigned char dirA = 0;
    unsigned char dirB = 0;


    /*
     * A方向
     */
    if (motorA < 0)
    {
        dirA = 1;

        motorA = -motorA;
    }


    /*
     * B方向
     */
    if (motorB < 0)
    {
        dirB = 1;

        motorB = -motorB;
    }


    /*
     * 最大150
     */
    if (motorA > 150)
    {
        motorA = 150;
    }

    if (motorB > 150)
    {
        motorB = 150;
    }


    /*
     * 组帧
     */
    buf[0] = 0xFC;

    buf[1] = dirA;
    buf[2] = (unsigned char)motorA;

    buf[3] = dirB;
    buf[4] = (unsigned char)motorB;

    buf[5] = 0xFD;


    /*
     * UART2发送
     */
    UartWrite(
        WIFI_IOT_UART_IDX_2,
        buf,
        6
    );
}



/*
 * =========================================================
 * 基本运动
 * =========================================================
 */

static void car_stop(void)
{
    stm32motor_control(
        0,
        0
    );
}


static void car_forward(void)
{
    stm32motor_control(
        FORWARD_SPEED,
        FORWARD_SPEED
    );
}


static void car_backward(void)
{
    stm32motor_control(
        -BACK_SPEED,
        -BACK_SPEED
    );
}


/*
 * 左转
 */
static void car_turn_left(void)
{
    stm32motor_control(
        -TURN_SPEED,
        TURN_SPEED
    );
}


/*
 * 右转
 */
static void car_turn_right(void)
{
    stm32motor_control(
        TURN_SPEED,
        -TURN_SPEED
    );
}



/*
 * =========================================================
 * 左侧桌边
 *
 * 后退
 * ->
 * 向右
 * =========================================================
 */
static void avoid_left_edge(void)
{
    printf("ACTION: LEFT EDGE\r\n");


    /*
     * 停
     */
    car_stop();

    usleep(STOP_TIME_US);


    /*
     * 后退
     */
    car_backward();

    usleep(BACK_TIME_US);


    /*
     * 停
     */
    car_stop();

    usleep(STOP_TIME_US);


    /*
     * 往右转
     */
    car_turn_right();

    usleep(TURN_TIME_US);


    /*
     * 最后停一下
     */
    car_stop();

    usleep(STOP_TIME_US);
}



/*
 * =========================================================
 * 右侧桌边
 *
 * 后退
 * ->
 * 向左
 * =========================================================
 */
static void avoid_right_edge(void)
{
    printf("ACTION: RIGHT EDGE\r\n");


    car_stop();

    usleep(STOP_TIME_US);


    car_backward();

    usleep(BACK_TIME_US);


    car_stop();

    usleep(STOP_TIME_US);


    car_turn_left();

    usleep(TURN_TIME_US);


    car_stop();

    usleep(STOP_TIME_US);
}



/*
 * =========================================================
 * 前方障碍
 * =========================================================
 */
static void avoid_obstacle(void)
{
    printf("ACTION: OBSTACLE\r\n");


    /*
     * 先停车
     */
    car_stop();

    usleep(STOP_TIME_US);


    /*
     * 后退
     */
    car_backward();

    usleep(BACK_TIME_US);


    /*
     * 停
     */
    car_stop();

    usleep(STOP_TIME_US);


    /*
     * 第一版固定左转
     *
     * 以后加SG90以后
     * 再让它自己选择左右。
     */
    car_turn_left();

    usleep(TURN_TIME_US);


    /*
     * 最后停一下
     */
    car_stop();

    usleep(STOP_TIME_US);
}



/*
 * =========================================================
 * 主线程
 * =========================================================
 */
static void smart_car_thread(void *arg)
{
    uint8_t left_raw;
    uint8_t right_raw;

    uint8_t left_safe;
    uint8_t right_safe;


    /*
     * 连续桌边计数
     */
    uint8_t left_edge_count = 0;
    uint8_t right_edge_count = 0;


    /*
     * 障碍物连续计数
     */
    uint8_t obstacle_count = 0;


    /*
     * 超声波冷却
     *
     * >0 时：
     * 暂时不再次触发超声波避障。
     *
     * 但是TCRT仍然正常工作。
     */
    uint8_t obstacle_cooldown = 0;


    float distance;


    (void)arg;


    printf("\r\n");
    printf("=================================\r\n");
    printf(" SMART CAR SIMPLE VERSION\r\n");
    printf("=================================\r\n");


    while (1)
    {
        /*
         * =================================================
         * TCRT
         * =================================================
         */

        left_raw =
            TCRT_ReadLeftRaw();

        right_raw =
            TCRT_ReadRightRaw();


        /*
         * tcrt_sensor.c里面
         * 已经做过5次采样防抖
         */
        left_safe =
            TCRT_LeftSafe();

        right_safe =
            TCRT_RightSafe();



        /*
         * =================================================
         * 左侧危险计数
         * =================================================
         */

        if (left_safe == 0)
        {
            if (
                left_edge_count
                <
                EDGE_CONFIRM_COUNT
            )
            {
                left_edge_count++;
            }
        }
        else
        {
            left_edge_count = 0;
        }



        /*
         * =================================================
         * 右侧危险计数
         * =================================================
         */

        if (right_safe == 0)
        {
            if (
                right_edge_count
                <
                EDGE_CONFIRM_COUNT
            )
            {
                right_edge_count++;
            }
        }
        else
        {
            right_edge_count = 0;
        }



        /*
         * =================================================
         * 超声波
         * =================================================
         */

        distance =
            HCSR04_GetDistance();



        /*
         * 冷却计时
         */
        if (obstacle_cooldown > 0)
        {
            obstacle_cooldown--;
        }



        /*
         * =================================================
         * 障碍物确认
         * =================================================
         *
         * 只有不在冷却的时候
         * 才检查超声波。
         */
        if (obstacle_cooldown == 0)
        {
            if (
                distance > 2.0f
                &&
                distance < OBSTACLE_DISTANCE
            )
            {
                if (
                    obstacle_count
                    <
                    OBSTACLE_CONFIRM_COUNT
                )
                {
                    obstacle_count++;
                }
            }
            else
            {
                obstacle_count = 0;
            }
        }
        else
        {
            /*
             * 冷却时直接清掉
             */
            obstacle_count = 0;
        }



        /*
         * =================================================
         * 打印状态
         * =================================================
         */
        printf(
            "L=%d R=%d "
            "LS=%d RS=%d "
            "LC=%d RC=%d "
            "D=%.1f "
            "OC=%d CD=%d\r\n",

            left_raw,
            right_raw,

            left_safe,
            right_safe,

            left_edge_count,
            right_edge_count,

            distance,

            obstacle_count,
            obstacle_cooldown
        );



        /*
         * =================================================
         * 第一优先级：
         *
         * 左右同时悬空
         * =================================================
         *
         * 这个情况有两种可能：
         *
         * 1. 小车整个被你拿起来了
         *
         * 2. 两个探头同时越过桌边
         *
         * 为了安全，
         * 这一版直接停车。
         *
         * 所以你把车拿起来后，
         * 两个轮子不会再高速旋转。
         */
        if (
            left_safe == 0
            &&
            right_safe == 0
        )
        {
            printf(
                "BOTH TCRT DANGER -> STOP\r\n"
            );


            car_stop();


            /*
             * 清除其他动作计数
             */
            obstacle_count = 0;


            usleep(LOOP_DELAY_US);


            continue;
        }



        /*
         * =================================================
         * 左侧真正到桌边
         * =================================================
         */

        if (
            left_edge_count
            >=
            EDGE_CONFIRM_COUNT
        )
        {
            printf(
                "CONFIRMED LEFT EDGE\r\n"
            );


            /*
             * 清零
             */
            left_edge_count = 0;
            right_edge_count = 0;

            obstacle_count = 0;


            /*
             * 防跌落
             */
            avoid_left_edge();


            /*
             * 动作后重新检测
             */
            usleep(LOOP_DELAY_US);


            continue;
        }



        /*
         * =================================================
         * 右侧真正到桌边
         * =================================================
         */

        if (
            right_edge_count
            >=
            EDGE_CONFIRM_COUNT
        )
        {
            printf(
                "CONFIRMED RIGHT EDGE\r\n"
            );


            left_edge_count = 0;
            right_edge_count = 0;

            obstacle_count = 0;


            avoid_right_edge();


            usleep(LOOP_DELAY_US);


            continue;
        }



        /*
         * =================================================
         * 第二优先级：
         *
         * 前方真正有障碍物
         * =================================================
         */

        if (
            obstacle_count
            >=
            OBSTACLE_CONFIRM_COUNT
        )
        {
            printf(
                "CONFIRMED OBSTACLE %.1f cm\r\n",
                distance
            );


            /*
             * 清零
             */
            obstacle_count = 0;


            /*
             * 执行一次避障
             */
            avoid_obstacle();


            /*
             * 开启短暂冷却。
             *
             * 不会永久锁住。
             */
            obstacle_cooldown =
                OBSTACLE_COOLDOWN_COUNT;


            usleep(LOOP_DELAY_US);


            continue;
        }



        /*
         * =================================================
         * 正常状态
         * =================================================
         *
         * 到这里说明：
         *
         * 左边安全
         * 右边安全
         * 前面没有确认障碍
         *
         * 所以持续前进。
         */
        car_forward();



        /*
         * 循环
         */
        usleep(
            LOOP_DELAY_US
        );
    }
}



/*
 * =========================================================
 * 初始化
 * =========================================================
 */
static void smart_car_entry(void)
{
    WifiIotUartAttribute uart_attr;

    osThreadAttr_t attr;


    printf("\r\n");
    printf("Smart Car Init\r\n");


    /*
     * GPIO总初始化
     */
    GpioInit();



    /*
     * =====================================================
     * TCRT
     * =====================================================
     */
    TCRT_Sensor_Init();



    /*
     * =====================================================
     * HC-SR04
     * =====================================================
     */
    HCSR04_Sensor_Init();



    /*
     * =====================================================
     * UART2
     *
     * GPIO11 TX
     * GPIO12 RX
     * =====================================================
     */

    hi_io_set_func(
        HI_IO_NAME_GPIO_11,
        HI_IO_FUNC_GPIO_11_UART2_TXD
    );


    hi_io_set_func(
        HI_IO_NAME_GPIO_12,
        HI_IO_FUNC_GPIO_12_UART2_RXD
    );



    /*
     * UART参数
     */
    uart_attr.baudRate =
        115200;


    uart_attr.dataBits =
        WIFI_IOT_UART_DATA_BIT_8;


    uart_attr.stopBits =
        WIFI_IOT_UART_STOP_BIT_1;


    uart_attr.parity =
        WIFI_IOT_UART_PARITY_NONE;



    /*
     * UART2初始化
     */
    UartInit(
        WIFI_IOT_UART_IDX_2,
        &uart_attr,
        NULL
    );


    printf(
        "UART2 init success\r\n"
    );



    /*
     * =====================================================
     * 创建主控制线程
     * =====================================================
     */

    memset(
        &attr,
        0,
        sizeof(attr)
    );


    attr.name =
        "smart_car_thread";


    attr.stack_size =
        8192;


    attr.priority =
        25;



    if (
        osThreadNew(
            smart_car_thread,
            NULL,
            &attr
        )
        ==
        NULL
    )
    {
        printf(
            "create smart_car_thread failed\r\n"
        );
    }
    else
    {
        printf(
            "smart_car_thread start\r\n"
        );
    }
}



APP_FEATURE_INIT(smart_car_entry);