#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ohos_init.h"
#include "cmsis_os2.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "wifiiot_uart.h"
#include "wifiiot_watchdog.h"
#include "hi_io.h"
#include "hi_time.h"

/* 引脚定义 */
#define SERVO_GPIO                 2
#define HCSR04_TRIG_GPIO           7
#define HCSR04_ECHO_GPIO           8
#define TCRT_LEFT_GPIO            13
#define TCRT_RIGHT_GPIO           14
/* 左右距离都小于这个值，认为没有正常出口 */
#define SIDE_ESCAPE_CM              45.0f

/* 墙角确认时，舵机最多来回扫描3次 */
#define CORNER_SCAN_ROUNDS          3

/* 确认困在墙角后，再后退一段距离 */
#define UTURN_EXTRA_REVERSE_US      300000

/* 接近180度掉头时间 */
/*
 * 根据你之前白色地面误报的问题，
 * 当前按高电平表示黑色处理。
 */
#define TCRT_BLACK_LEVEL           WIFI_IOT_GPIO_VALUE1

/* 电机速度，范围0～150 */
#define FORWARD_SPEED              85
#define REVERSE_SPEED              100
#define TURN_SPEED                 90

/* 超声波距离参数 */
#define FRONT_STOP_CM              25.0f
#define EMERGENCY_CM               15.0f
#define SIDE_BLOCKED_CM            30.0f

/*
 * 你提供的SG90代码：
 * 0度=左侧，90度=正前方，180度=右侧
 */
#define SERVO_LEFT_ANGLE           25U
#define SERVO_CENTER_ANGLE         90U
#define SERVO_RIGHT_ANGLE          155U
#define SERVO_PULSE_COUNT          12U

/* 动作时间参数 */
#define LOOP_DELAY_US              35000
#define STOP_GAP_US                80000
#define NORMAL_REVERSE_US          280000
#define CORNER_REVERSE_US          480000
#define NORMAL_TURN_US             560000
#define CORNER_TURN_US             820000
#define TAPE_REVERSE_US            320000

/* 连续超声波故障时，最多自动脱困次数 */
#define MAX_TIMEOUT_ESCAPES        2
#define UTURN_SPEED                 70
#define UTURN_TURN_US               1400000
static int g_turn_left_next = 1;
static int g_stuck_level = 0;
static int g_clear_cycles = 0;
static int g_timeout_escapes = 0;

/*
 * 向STM32发送电机控制帧：
 * FC A方向 A速度 B方向 B速度 FD
 */
static void MotorCommand(int motor_a, int motor_b)
{
    unsigned char frame[6];
    unsigned char dir_a = 0;
    unsigned char dir_b = 0;

    if (motor_a < 0) {
        dir_a = 1;
        motor_a = -motor_a;
    }

    if (motor_b < 0) {
        dir_b = 1;
        motor_b = -motor_b;
    }

    if (motor_a > 150) {
        motor_a = 150;
    }

    if (motor_b > 150) {
        motor_b = 150;
    }

    frame[0] = 0xFC;
    frame[1] = dir_a;
    frame[2] = (unsigned char)motor_a;
    frame[3] = dir_b;
    frame[4] = (unsigned char)motor_b;
    frame[5] = 0xFD;

    UartWrite(
        WIFI_IOT_UART_IDX_2,
        frame,
        6);
}

static void CarStop(void)
{
    MotorCommand(0, 0);
}

static void CarForward(void)
{
    MotorCommand(
        FORWARD_SPEED,
        FORWARD_SPEED);
}

static void CarReverse(void)
{
    MotorCommand(
        -REVERSE_SPEED,
        -REVERSE_SPEED);
}

static void PivotLeft(void)
{
    MotorCommand(
        -TURN_SPEED,
        TURN_SPEED);
}

static void PivotRight(void)
{
    MotorCommand(
        TURN_SPEED,
        -TURN_SPEED);
}

/*
 * SG90产生一个20ms周期脉冲。
 */
static void ServoPulse(unsigned int high_us)
{
    GpioSetOutputVal(
        SERVO_GPIO,
        WIFI_IOT_GPIO_VALUE1);

    hi_udelay(high_us);

    GpioSetOutputVal(
        SERVO_GPIO,
        WIFI_IOT_GPIO_VALUE0);

    hi_udelay(20000U - high_us);
}

/*
 * 设置舵机角度。
 * 0度对应500us，180度对应2500us。
 */
static void ServoSetAngle(unsigned int angle)
{
    unsigned int i;
    unsigned int high_us;

    if (angle > 180U) {
        angle = 180U;
    }

    high_us =
        500U + (angle * 2000U) / 180U;

    for (i = 0; i < SERVO_PULSE_COUNT; i++) {
        ServoPulse(high_us);
    }

    /* 等待舵机稳定 */
    usleep(60000);
}

/*
 * 读取一次超声波距离。
 * 成功返回cm，失败返回-1。
 */
static float ReadDistanceCm(void)
{
    WifiIotGpioValue echo =
        WIFI_IOT_GPIO_VALUE0;

    unsigned int wait_start;
    unsigned int echo_start;
    unsigned int pulse_us;

    GpioSetOutputVal(
        HCSR04_TRIG_GPIO,
        WIFI_IOT_GPIO_VALUE0);

    hi_udelay(2);

    GpioSetOutputVal(
        HCSR04_TRIG_GPIO,
        WIFI_IOT_GPIO_VALUE1);

    hi_udelay(12);

    GpioSetOutputVal(
        HCSR04_TRIG_GPIO,
        WIFI_IOT_GPIO_VALUE0);

    /*
     * 等待ECHO变成高电平。
     * 最多等待30ms，防止程序卡死。
     */
    wait_start = hi_get_us();

    do {
        GpioGetInputVal(
            HCSR04_ECHO_GPIO,
            &echo);

        if ((unsigned int)
            (hi_get_us() - wait_start) > 30000U) {
            return -1.0f;
        }
    } while (echo == WIFI_IOT_GPIO_VALUE0);

    /*
     * 测量ECHO高电平时间。
     */
    echo_start = hi_get_us();

    do {
        GpioGetInputVal(
            HCSR04_ECHO_GPIO,
            &echo);

        if ((unsigned int)
            (hi_get_us() - echo_start) > 30000U) {
            return -1.0f;
        }
    } while (echo == WIFI_IOT_GPIO_VALUE1);

    pulse_us =
        (unsigned int)
        (hi_get_us() - echo_start);

    return (float)pulse_us * 0.017f;
}

/*
 * 连续读取三次距离。
 * 使用最近的有效距离，尽量避免漏检墙体。
 */
static float ReadDistanceFiltered(void)
{
    unsigned int i;
    int valid_count = 0;

    float nearest = 500.0f;
    float distance;

    for (i = 0; i < 3U; i++) {
        distance = ReadDistanceCm();

        if (distance >= 2.0f &&
            distance <= 400.0f) {

            if (distance < nearest) {
                nearest = distance;
            }

            valid_count++;
        }

        usleep(12000);
    }

    if (valid_count == 0) {
        return -1.0f;
    }

    return nearest;
}

static void ReadTape(
    int *left_black,
    int *right_black)
{
    WifiIotGpioValue left_value;
    WifiIotGpioValue right_value;

    int left_count = 0;
    int right_count = 0;
    int i;

    /*
     * 快速读取3次。
     * 三次中至少两次相同才确认，过滤一次偶发抖动。
     */
    for (i = 0; i < 3; i++) {
        GpioGetInputVal(
            TCRT_LEFT_GPIO,
            &left_value);

        GpioGetInputVal(
            TCRT_RIGHT_GPIO,
            &right_value);

        if (left_value ==
            TCRT_BLACK_LEVEL) {
            left_count++;
        }

        if (right_value ==
            TCRT_BLACK_LEVEL) {
            right_count++;
        }

        usleep(1000);
    }

    *left_black =
        (left_count >= 2);

    *right_black =
        (right_count >= 2);
}

/*
 * 按指定方向原地转向。
 */
static void TurnForTime(
    int turn_left,
    unsigned int duration_us)
{
    unsigned int elapsed_us = 0;

    /*
     * 每80ms重新发送一次命令，
     * 防止STM32的一秒通信保护提前停车。
     */
    while (elapsed_us < duration_us) {
        if (turn_left) {
            PivotLeft();
        } else {
            PivotRight();
        }

        usleep(80000);
        elapsed_us += 80000;
    }

    CarStop();
    usleep(STOP_GAP_US);
}
static void UTurnForTime(int turn_left)
{
    unsigned int elapsed_us = 0;

    printf(
        "fast U-turn: %s\r\n",
        turn_left ? "left" : "right");

    /*
     * 使用更高速度持续转约1.4秒，
     * 接近完成180度掉头。
     */
    while (elapsed_us <
           UTURN_TURN_US) {

        if (turn_left) {
            MotorCommand(
                -UTURN_SPEED,
                UTURN_SPEED);
        } else {
            MotorCommand(
                UTURN_SPEED,
                -UTURN_SPEED);
        }

        usleep(80000);
        elapsed_us += 80000;
    }

    CarStop();
    usleep(STOP_GAP_US);
}
/*
 * 黑色危险区域恢复动作。
 */
static void RecoverFromTape(
    int left_black,
    int right_black)
{
    int turn_left;

    CarStop();
    usleep(STOP_GAP_US);

    CarReverse();
    usleep(TAPE_REVERSE_US);

    CarStop();
    usleep(STOP_GAP_US);

    /*
     * 左侧碰到黑色，向右转。
     * 右侧碰到黑色，向左转。
     */
    if (left_black && !right_black) {
        turn_left = 0;
    } else if (right_black && !left_black) {
        turn_left = 1;
    } else {
        turn_left = g_turn_left_next;
        g_turn_left_next =
            !g_turn_left_next;
    }

    TurnForTime(
        turn_left,
        NORMAL_TURN_US);
}

/*
 * 舵机扫描左右距离并执行避障。
 */
static void ScanAndAvoid(
    float front_distance,
    int timeout_triggered)
{
    float left_distance;
    float right_distance;

    float best_left = -1.0f;
    float best_right = -1.0f;

    int left_black;
    int right_black;

    int turn_left = 1;
    int exit_found = 0;
    int round;

    unsigned int first_reverse_us;

    /*
     * 先停车。
     */
    CarStop();
    usleep(STOP_GAP_US);

    /*
     * 后退前检查黑色区域。
     */
    ReadTape(
        &left_black,
        &right_black);

    if (left_black || right_black) {
        RecoverFromTape(
            left_black,
            right_black);

        return;
    }

    /*
     * 非常靠近墙、重复卡住或超声波超时时，
     * 使用更长的后退时间。
     */
    if (timeout_triggered ||
        g_stuck_level >= 2 ||
        (front_distance > 0.0f &&
         front_distance <= EMERGENCY_CM)) {

        first_reverse_us =
            CORNER_REVERSE_US;
    } else {
        first_reverse_us =
            NORMAL_REVERSE_US;
    }

    /*
     * 先离开正前方墙面。
     */
    CarReverse();
    usleep(first_reverse_us);

    CarStop();
    usleep(STOP_GAP_US);

    /*
     * 最多扫描三轮。
     */
    for (round = 0;
         round < CORNER_SCAN_ROUNDS;
         round++) {

        /*
         * 扫描左侧。
         */
        ServoSetAngle(
            SERVO_LEFT_ANGLE);

        left_distance =
            ReadDistanceFiltered();

        /*
         * 扫描右侧。
         */
        ServoSetAngle(
            SERVO_RIGHT_ANGLE);

        right_distance =
            ReadDistanceFiltered();

        printf(
            "scan %d: F=%d L=%d R=%d\r\n",
            round + 1,
            (int)front_distance,
            (int)left_distance,
            (int)right_distance);

        /*
         * 保存扫描到的最大空间。
         */
        if (left_distance > best_left) {
            best_left = left_distance;
        }

        if (right_distance > best_right) {
            best_right = right_distance;
        }

        /*
         * 左边有出口。
         */
        if (left_distance >= SIDE_ESCAPE_CM &&
            right_distance < SIDE_ESCAPE_CM) {

            turn_left = 1;
            exit_found = 1;
            break;
        }

        /*
         * 右边有出口。
         */
        if (right_distance >= SIDE_ESCAPE_CM &&
            left_distance < SIDE_ESCAPE_CM) {

            turn_left = 0;
            exit_found = 1;
            break;
        }

        /*
         * 左右都有出口，选择更宽的一边。
         */
        if (left_distance >= SIDE_ESCAPE_CM &&
            right_distance >= SIDE_ESCAPE_CM) {

            turn_left =
                (left_distance >=
                 right_distance);

            exit_found = 1;
            break;
        }
    }

    /*
     * 舵机回到正前方。
     */
    ServoSetAngle(
        SERVO_CENTER_ANGLE);

    /*
     * 扫描结束后再次检查黑色区域。
     */
    ReadTape(
        &left_black,
        &right_black);

    if (left_black || right_black) {
        RecoverFromTape(
            left_black,
            right_black);

        return;
    }

    /*
     * 找到正常出口，执行普通转向。
     */
    if (exit_found) {
        printf(
            "exit found: turn %s\r\n",
            turn_left ? "left" : "right");

        TurnForTime(
            turn_left,
            NORMAL_TURN_US);

        return;
    }

    /*
     * 三轮扫描都没有出口：
     * 判定处于墙角，直接掉头。
     */
    printf(
        "corner confirmed, make U-turn\r\n");

    /*
     * 再后退一点，为掉头留出空间。
     */
    CarReverse();
    usleep(
        UTURN_EXTRA_REVERSE_US);

    CarStop();
    usleep(STOP_GAP_US);

    /*
     * 选择三轮扫描中相对更宽的一侧。
     */
    if (best_left > best_right) {
        turn_left = 1;
    } else if (best_right > best_left) {
        turn_left = 0;
    } else {
        turn_left =
            g_turn_left_next;

        g_turn_left_next =
            !g_turn_left_next;
    }

    /*
     * 高速完成接近180度掉头。
     */
    UTurnForTime(turn_left);

    g_stuck_level = 0;
    g_clear_cycles = 0;

    printf("U-turn finished\r\n");
}

/*
 * 主避障任务。
 */
static void SmartAvoidanceTask(void *arg)
{
    int left_black;
    int right_black;
    int timeout_count = 0;

    float front_distance;

    (void)arg;

    printf(
        "servo scan avoidance started\r\n");

    CarStop();

    /*
     * 上电后舵机先回到正前方。
     */
    ServoSetAngle(
        SERVO_CENTER_ANGLE);

    usleep(200000);

    while (1) {
        /*
         * 黑色区域优先级最高。
         */
        ReadTape(
            &left_black,
            &right_black);

        if (left_black ||
            right_black) {

            printf(
                "tape: L=%d R=%d\r\n",
                left_black,
                right_black);

            g_stuck_level++;
            g_clear_cycles = 0;

            RecoverFromTape(
                left_black,
                right_black);

            continue;
        }

        /*
         * 正前方超声波测距。
         */
        front_distance =
            ReadDistanceFiltered();

        /*
         * 无回波时先停车。
         * 连续两次无回波后尝试扫描脱困。
         */
        if (front_distance < 0.0f) {
            CarStop();

            timeout_count++;

            printf(
                "hcsr04 timeout %d\r\n",
                timeout_count);

            if (timeout_count >= 2) {
                timeout_count = 0;

                if (g_timeout_escapes <
                    MAX_TIMEOUT_ESCAPES) {

                    g_timeout_escapes++;
                    g_stuck_level++;

                    ScanAndAvoid(
                        -1.0f,
                        1);
                } else {
                    /*
                     * 连续多次完全没有回波，
                     * 说明传感器可能断开。
                     */
                    printf(
                        "hcsr04 sensor fault "
                        "- check GPIO7/8\r\n");

                    ServoSetAngle(
                        SERVO_CENTER_ANGLE);

                    usleep(200000);
                }
            } else {
                usleep(LOOP_DELAY_US);
            }

            continue;
        }

        timeout_count = 0;
        g_timeout_escapes = 0;

        /*
         * 前方距离小于停车距离：
         * 停车并扫描左右方向。
         */
        if (front_distance <=
            FRONT_STOP_CM) {

            CarStop();

            g_stuck_level++;
            g_clear_cycles = 0;

            printf(
                "wall: %d cm\r\n",
                (int)front_distance);

            ScanAndAvoid(
                front_distance,
                0);

            continue;
        }

        /*
         * 连续一段时间没有遇到障碍物，
         * 清除“卡住”计数。
         */
        g_clear_cycles++;

        if (g_clear_cycles >= 15) {
            g_clear_cycles = 15;
            g_stuck_level = 0;
        }

        CarForward();
        usleep(LOOP_DELAY_US);
    }
}

/*
 * 初始化。
 */
static void SmartAvoidanceInit(void)
{
    WifiIotUartAttribute uart_attr;
    osThreadAttr_t attr;

    WatchDogDisable();
    GpioInit();

    /*
     * UART2：
     * GPIO11=TX
     * GPIO12=RX
     */
    hi_io_set_func(
        HI_IO_NAME_GPIO_11,
        HI_IO_FUNC_GPIO_11_UART2_TXD);

    hi_io_set_func(
        HI_IO_NAME_GPIO_12,
        HI_IO_FUNC_GPIO_12_UART2_RXD);

    memset(
        &uart_attr,
        0,
        sizeof(uart_attr));

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
        NULL);

    /*
     * SG90舵机GPIO2。
     */
    IoSetFunc(
        WIFI_IOT_IO_NAME_GPIO_2,
        WIFI_IOT_IO_FUNC_GPIO_2_GPIO);

    GpioSetDir(
        SERVO_GPIO,
        WIFI_IOT_GPIO_DIR_OUT);

    GpioSetOutputVal(
        SERVO_GPIO,
        WIFI_IOT_GPIO_VALUE0);

    /*
     * HC-SR04：
     * GPIO7=TRIG
     * GPIO8=ECHO
     */
    hi_io_set_func(
        HCSR04_TRIG_GPIO,
        0);

    hi_io_set_func(
        HCSR04_ECHO_GPIO,
        0);

    GpioSetDir(
        HCSR04_TRIG_GPIO,
        WIFI_IOT_GPIO_DIR_OUT);

    GpioSetDir(
        HCSR04_ECHO_GPIO,
        WIFI_IOT_GPIO_DIR_IN);

    GpioSetOutputVal(
        HCSR04_TRIG_GPIO,
        WIFI_IOT_GPIO_VALUE0);

    /*
     * TCRT5000：
     * GPIO13=左
     * GPIO14=右
     */
    IoSetFunc(
        WIFI_IOT_IO_NAME_GPIO_13,
        WIFI_IOT_IO_FUNC_GPIO_13_GPIO);

    IoSetFunc(
        WIFI_IOT_IO_NAME_GPIO_14,
        WIFI_IOT_IO_FUNC_GPIO_14_GPIO);

    GpioSetDir(
        TCRT_LEFT_GPIO,
        WIFI_IOT_GPIO_DIR_IN);

    GpioSetDir(
        TCRT_RIGHT_GPIO,
        WIFI_IOT_GPIO_DIR_IN);

    /*
     * 创建唯一的避障线程。
     */
    memset(
        &attr,
        0,
        sizeof(attr));

    attr.name =
        "servo_avoidance";

    attr.stack_size =
        10240;

    attr.priority =
        osPriorityNormal;

    if (osThreadNew(
            SmartAvoidanceTask,
            NULL,
            &attr) == NULL) {

        printf(
            "create servo_avoidance failed\r\n");
    }
}

APP_FEATURE_INIT(SmartAvoidanceInit);