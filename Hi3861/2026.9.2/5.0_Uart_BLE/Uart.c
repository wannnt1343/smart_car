#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ohos_init.h"
#include "cmsis_os2.h"

#include "wifiiot_errno.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "wifiiot_uart.h"

#include "hi_io.h"

/*
 * 手机 -> JDY-16蓝牙模块 -> HI3861 UART1
 * HI3861 UART2 -> STM32 -> 电机
 */
#define BLE_UART_INDEX        WIFI_IOT_UART_IDX_1
#define MOTOR_UART_INDEX      WIFI_IOT_UART_IDX_2

#define BLE_BAUD_RATE         9600
#define MOTOR_BAUD_RATE       115200

#define FORWARD_SPEED         60
#define BACKWARD_SPEED        100
#define TURN_SPEED            100

#define BLE_RX_BUFFER_SIZE    64
#define BT_TASK_STACK_SIZE    4096
#define BT_TASK_PRIORITY      25

/*
 * 向STM32发送电机控制帧
 *
 * 格式：
 * FC A方向 A速度 B方向 B速度 FD
 *
 * 方向：
 * 0 = 正转
 * 1 = 反转
 *
 * 速度：
 * 0~150
 */
static void stm32motor_control(int motorA, int motorB)
{
    unsigned char frame[6];
    unsigned char dirA = 0;
    unsigned char dirB = 0;

    /*
     * 判断A电机方向
     */
    if (motorA < 0) {
        dirA = 1;
        motorA = -motorA;
    }

    /*
     * 判断B电机方向
     */
    if (motorB < 0) {
        dirB = 1;
        motorB = -motorB;
    }

    /*
     * 限制最大速度
     */
    if (motorA > 150) {
        motorA = 150;
    }

    if (motorB > 150) {
        motorB = 150;
    }

    /*
     * 组帧
     */
    frame[0] = 0xFC;

    frame[1] = dirA;
    frame[2] = (unsigned char)motorA;

    frame[3] = dirB;
    frame[4] = (unsigned char)motorB;

    frame[5] = 0xFD;

    /*
     * 通过UART2发送给STM32
     */
    UartWrite(
        MOTOR_UART_INDEX,
        frame,
        sizeof(frame)
    );
}

/*
 * 停止
 */
static void car_stop(void)
{
    stm32motor_control(0, 0);
}

/*
 * 前进
 */
static void car_forward(void)
{
    stm32motor_control(
        FORWARD_SPEED,
        FORWARD_SPEED
    );
}

/*
 * 后退
 */
static void car_backward(void)
{
    stm32motor_control(
        -BACKWARD_SPEED,
        -BACKWARD_SPEED
    );
}

/*
 * 左转
 */
static void car_left(void)
{
    stm32motor_control(
        -TURN_SPEED,
        TURN_SPEED
    );
}

/*
 * 右转
 */
static void car_right(void)
{
    stm32motor_control(
        TURN_SPEED,
        -TURN_SPEED
    );
}

/*
 * 处理手机发送的单字节蓝牙命令
 */
static void handle_bt_command(unsigned char command)
{
    /*
     * 将小写字母转换成大写
     */
    if (command >= 'a' && command <= 'z') {
        command =
            (unsigned char)(command - 'a' + 'A');
    }

    switch (command) {
        /*
         * 停止
         */
        case 'O':
            car_stop();
            printf("BT: stop\r\n");
            break;

        /*
         * 前进
         */
        case 'W':
            car_forward();
            printf("BT: forward\r\n");
            break;

        /*
         * 左转
         */
        case 'A':
            car_left();
            printf("BT: left\r\n");
            break;

        /*
         * 右转
         */
        case 'D':
            car_right();
            printf("BT: right\r\n");
            break;

        /*
         * 后退
         */
        case 'S':
            car_backward();
            printf("BT: backward\r\n");
            break;

        /*
         * 速度100前进
         */
        case 'I':
            stm32motor_control(100, 100);
            printf("BT: forward speed 100\r\n");
            break;

        /*
         * 速度150前进
         */
        case 'K':
            stm32motor_control(150, 150);
            printf("BT: forward speed 150\r\n");
            break;

        /*
         * 忽略回车、换行、空格和Tab
         */
        case '\r':
        case '\n':
        case ' ':
        case '\t':
            break;

        /*
         * 未知命令
         */
        default:
            printf(
                "BT: unknown command 0x%02X\r\n",
                command
            );
            break;
    }
}

/*
 * 初始化一个UART
 */
static unsigned int init_uart(
    WifiIotUartIdx uart_index,
    unsigned int baud_rate)
{
    WifiIotUartAttribute uart_attr;

    memset(
        &uart_attr,
        0,
        sizeof(uart_attr)
    );

    uart_attr.baudRate =
        baud_rate;

    uart_attr.dataBits =
        WIFI_IOT_UART_DATA_BIT_8;

    uart_attr.stopBits =
        WIFI_IOT_UART_STOP_BIT_1;

    uart_attr.parity =
        WIFI_IOT_UART_PARITY_NONE;

    return UartInit(
        uart_index,
        &uart_attr,
        NULL
    );
}

/*
 * 蓝牙控制线程
 */
static void bluetooth_control_thread(void *arg)
{
    unsigned char rx_buffer[BLE_RX_BUFFER_SIZE];
    int read_length;
    int i;

    (void)arg;

    printf(
        "Bluetooth control ready: "
        "O/W/A/D/S/I/K\r\n"
    );

    while (1) {
        /*
         * 从UART1读取蓝牙数据
         */
        read_length = UartRead(
            BLE_UART_INDEX,
            rx_buffer,
            sizeof(rx_buffer)
        );

        /*
         * 一次可能收到多个字符，因此逐个处理
         */
        if (read_length > 0) {
            for (i = 0; i < read_length; i++) {
                handle_bt_command(
                    rx_buffer[i]
                );
            }
        } else {
            /*
             * 防止无数据时线程一直空转
             */
            usleep(10000);
        }
    }
}

/*
 * 程序入口
 */
static void uart_ble_entry(void)
{
    unsigned int ret;
    osThreadAttr_t attr;

    /*
     * GPIO总初始化
     */
    GpioInit();

    /*
     * UART1
     *
     * GPIO0 = UART1 TX
     * GPIO1 = UART1 RX
     *
     * 连接JDY-16蓝牙模块
     */
    IoSetFunc(
        WIFI_IOT_IO_NAME_GPIO_0,
        WIFI_IOT_IO_FUNC_GPIO_0_UART1_TXD
    );

    IoSetFunc(
        WIFI_IOT_IO_NAME_GPIO_1,
        WIFI_IOT_IO_FUNC_GPIO_1_UART1_RXD
    );

    /*
     * UART2
     *
     * GPIO11 = UART2 TX
     * GPIO12 = UART2 RX
     *
     * 连接STM32
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
     * 初始化UART2
     *
     * HI3861 -> STM32
     */
    ret = init_uart(
        MOTOR_UART_INDEX,
        MOTOR_BAUD_RATE
    );

    if (ret != WIFI_IOT_SUCCESS) {
        printf(
            "UART2 init failed: %u\r\n",
            ret
        );

        return;
    }

    /*
     * 初始化UART1
     *
     * 蓝牙模块 -> HI3861
     */
    ret = init_uart(
        BLE_UART_INDEX,
        BLE_BAUD_RATE
    );

    if (ret != WIFI_IOT_SUCCESS) {
        printf(
            "UART1 init failed: %u\r\n",
            ret
        );

        return;
    }

    /*
     * 上电后先发送停车帧
     */
    car_stop();

    /*
     * 创建蓝牙控制线程
     */
    memset(
        &attr,
        0,
        sizeof(attr)
    );

    attr.name =
        "bluetooth_control";

    attr.stack_size =
        BT_TASK_STACK_SIZE;

    attr.priority =
        BT_TASK_PRIORITY;

    if (
        osThreadNew(
            bluetooth_control_thread,
            NULL,
            &attr
        )
        ==
        NULL
    ) {
        printf(
            "Create bluetooth control "
            "thread failed\r\n"
        );

        car_stop();

        return;
    }

    printf(
        "UART1 BLE and UART2 motor "
        "initialized\r\n"
    );
}

APP_FEATURE_INIT(uart_ble_entry);