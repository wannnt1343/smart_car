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


#define TCRT_LEFT_GPIO          13
#define TCRT_RIGHT_GPIO         14
#define TCRT_BLACK_LEVEL        WIFI_IOT_GPIO_VALUE1


/*
 * ============================================================
 * 速度参数
 * ============================================================
 */

#define LINE_SPEED              38
#define STRAIGHT_TRIM           6

#define EVENT_SPEED             34
#define CORRECT_SPEED           72

/*
 * 电机启动加速参数。
 * 低速启动时，先给电机较高速度，避免电机无法克服静摩擦。
 */
#define START_BOOST_SPEED       65
#define START_BOOST_US          70000U

#define PIVOT_SPEED             82


/*
 * ============================================================
 * 时间参数
 * ============================================================
 */

#define LOOP_DELAY_US           6000U
#define MOTOR_GAP_US            50000U

#define EVENT_CONFIRM_COUNT     3
#define EVENT_CONFIRM_GAP_US    3000U
#define EVENT_CLEAR_COUNT       5

#define JUNCTION_CENTER_US      230000U

/*
 * 原地转弯时间。
 * 如果转弯角度不足，增大该值。
 * 如果转弯过头，减小该值。
 */
#define TURN_90_US              850000U

/*
 * 转弯后继续巡线的时间。
 */
#define TURN_EXIT_US            650000U


typedef enum {
    FIRST_LEFT = 0,
    SECOND_RIGHT,
    FINISH,
    DONE
} RouteState;


/*
 * ============================================================
 * 电机控制
 * ============================================================
 *
 * motor_a、motor_b：
 *   正数：正转
 *   负数：反转
 *   0：停止
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

    UartWrite(WIFI_IOT_UART_IDX_2, frame, sizeof(frame));
}


static void CarStop(void)
{
    MotorCommand(0, 0);
}


static void DriveStraight(int speed)
{
    MotorCommand(speed, speed + STRAIGHT_TRIM);
}


static void PivotLeft(void)
{
    MotorCommand(-PIVOT_SPEED, PIVOT_SPEED);
}


static void PivotRight(void)
{
    MotorCommand(PIVOT_SPEED, -PIVOT_SPEED);
}


/*
 * 停车后重新起步。
 * 先使用较高速度启动电机，再进入正常速度。
 */
static void StartStraight(int normal_speed)
{
    MotorCommand(START_BOOST_SPEED,
                 START_BOOST_SPEED + STRAIGHT_TRIM);

    usleep(START_BOOST_US);

    DriveStraight(normal_speed);
}


/*
 * ============================================================
 * 传感器读取
 * ============================================================
 *
 * 连续读取 3 次，取多数结果。
 *
 * left_black：
 *   1：左传感器检测到黑线
 *   0：左传感器没有检测到黑线
 *
 * right_black：
 *   1：右传感器检测到黑线
 *   0：右传感器没有检测到黑线
 */
static void ReadTape(int *left_black, int *right_black)
{
    WifiIotGpioValue lv;
    WifiIotGpioValue rv;

    int left_count = 0;
    int right_count = 0;
    int i;

    for (i = 0; i < 3; i++) {
        GpioGetInputVal(TCRT_LEFT_GPIO, &lv);
        GpioGetInputVal(TCRT_RIGHT_GPIO, &rv);

        if (lv == TCRT_BLACK_LEVEL) {
            left_count++;
        }

        if (rv == TCRT_BLACK_LEVEL) {
            right_count++;
        }

        usleep(1000);
    }

    *left_black = (left_count >= 2);
    *right_black = (right_count >= 2);
}


/*
 * ============================================================
 * 普通巡线
 * ============================================================
 *
 * 传感器状态：
 *
 *   00：直行
 *   10：左传感器检测到黑线，向左修正
 *   01：右传感器检测到黑线，向右修正
 *   11：由主任务处理为路口
 */
static void FollowLine(int left_black, int right_black)
{
    /*
     * 11 交给主任务判断当前是第几个路口。
     */
    if (left_black && right_black) {
        CarStop();
        return;
    }

    if (left_black && !right_black) {
        /*
         * 保持原来已经验证过的纠偏方向。
         */
        MotorCommand(0, CORRECT_SPEED);
    } else if (!left_black && right_black) {
        MotorCommand(CORRECT_SPEED, 0);
    } else {
        DriveStraight(LINE_SPEED);
    }
}


/*
 * ============================================================
 * 路口确认
 * ============================================================
 *
 * 连续多次检测到 11，才确认是有效路口。
 */
static int ConfirmJunction(void)
{
    int left_black;
    int right_black;
    int i;

    CarStop();
    usleep(12000);

    for (i = 0; i < EVENT_CONFIRM_COUNT; i++) {
        ReadTape(&left_black, &right_black);

        if (!(left_black && right_black)) {
            return 0;
        }

        usleep(EVENT_CONFIRM_GAP_US);
    }

    return 1;
}


/*
 * ============================================================
 * 转弯后的巡线
 * ============================================================
 */
static void ExitWithFollowLine(unsigned int duration_us)
{
    unsigned int start;
    int left_black;
    int right_black;

    /*
     * 转弯后先给电机启动加速。
     */
    StartStraight(EVENT_SPEED);

    start = hi_get_us();

    while ((unsigned int)(hi_get_us() - start) < duration_us) {
        ReadTape(&left_black, &right_black);

        if (left_black && right_black) {
            /*
             * 转弯刚结束时可能暂时仍然检测到 11，
             * 此时继续向前，不重新触发路口。
             */
            DriveStraight(EVENT_SPEED);
        } else {
            FollowLine(left_black, right_black);
        }

        usleep(LOOP_DELAY_US);
    }

    CarStop();
    usleep(MOTOR_GAP_US);
}


/*
 * ============================================================
 * 第一次 11：左转
 * ============================================================
 */
static void TurnLeft(void)
{
    printf("--> FIRST 11: turn LEFT\r\n");

    /*
     * 进入路口中心。
     */
    StartStraight(EVENT_SPEED);
    usleep(JUNCTION_CENTER_US);

    CarStop();
    usleep(MOTOR_GAP_US);

    /*
     * 第一次路口固定左转。
     */
    PivotLeft();
    usleep(TURN_90_US);

    CarStop();
    usleep(MOTOR_GAP_US);

    /*
     * 转弯后重新找线。
     */
    ExitWithFollowLine(TURN_EXIT_US);
}


/*
 * ============================================================
 * 第二次 11：右转
 * ============================================================
 */
static void TurnRight(void)
{
    printf("--> SECOND 11: turn RIGHT\r\n");

    /*
     * 进入路口中心。
     */
    StartStraight(EVENT_SPEED);
    usleep(JUNCTION_CENTER_US);

    CarStop();
    usleep(MOTOR_GAP_US);

    /*
     * 第二次路口固定右转。
     */
    PivotRight();
    usleep(TURN_90_US);

    CarStop();
    usleep(MOTOR_GAP_US);

    /*
     * 转弯后重新找线。
     */
    ExitWithFollowLine(TURN_EXIT_US);
}


/*
 * ============================================================
 * 终点停车
 * ============================================================
 */
static void FinishAndStop(void)
{
    CarStop();

    printf("--> FINISH: stopped\r\n");

    while (1) {
        usleep(500000);
    }
}


/*
 * ============================================================
 * 路口状态处理
 * ============================================================
 *
 * 第一次 11：FIRST_LEFT
 * 第二次 11：SECOND_RIGHT
 * 第三次 11：FINISH
 */
static void HandleJunction(RouteState *state)
{
    switch (*state) {
        case FIRST_LEFT:
            printf("junction number 1 confirmed\r\n");
            printf("route action: LEFT\r\n");

            TurnLeft();

            *state = SECOND_RIGHT;
            break;

        case SECOND_RIGHT:
            printf("junction number 2 confirmed\r\n");
            printf("route action: RIGHT\r\n");

            TurnRight();

            *state = FINISH;
            break;

        case FINISH:
            printf("junction number 3 confirmed\r\n");
            printf("route action: STOP\r\n");

            *state = DONE;
            FinishAndStop();
            break;

        case DONE:
        default:
            CarStop();
            break;
    }
}


/*
 * ============================================================
 * 清除当前路口状态
 * ============================================================
 *
 * 转弯后可能短时间再次读到 11。
 * 连续多次读到非 11，才认为已经离开当前路口。
 */
static void ClearCurrentEvent(void)
{
    int left_black;
    int right_black;
    int clear_count = 0;

    StartStraight(EVENT_SPEED);

    while (clear_count < EVENT_CLEAR_COUNT) {
        ReadTape(&left_black, &right_black);

        if (left_black && right_black) {
            clear_count = 0;
            DriveStraight(EVENT_SPEED);
        } else {
            clear_count++;
            FollowLine(left_black, right_black);
        }

        usleep(LOOP_DELAY_US);
    }

    CarStop();
    usleep(MOTOR_GAP_US);
}


/*
 * ============================================================
 * 主巡线任务
 * ============================================================
 */
static void LineMazeTask(void *arg)
{
    RouteState state = FIRST_LEFT;

    int left_black;
    int right_black;

    /*
     * 1：允许检测新的路口
     * 0：当前路口已经处理，防止重复转弯
     */
    int event_armed = 1;

    unsigned int print_timer = 0;

    (void)arg;

    printf("===== boosted line maze start =====\r\n");
    printf("route: first LEFT, second RIGHT, third STOP\r\n");

    CarStop();
    usleep(500000);

    /*
     * 初始启动。
     */
    StartStraight(LINE_SPEED);

    while (1) {
        ReadTape(&left_black, &right_black);

        /*
         * 检测到 11。
         */
        if (left_black && right_black) {
            if (event_armed && ConfirmJunction()) {
                /*
                 * 先锁定当前事件，避免同一个路口重复处理。
                 */
                event_armed = 0;

                printf("11 confirmed, current state=%d\r\n",
                       (int)state);

                /*
                 * 按状态执行：
                 * 第一次左转，第二次右转，第三次停车。
                 */
                HandleJunction(&state);

                /*
                 * 第三次路口会进入死循环停车，
                 * 正常不会返回到这里。
                 */
                ClearCurrentEvent();

                /*
                 * 离开旧路口后，允许检测下一个路口。
                 */
                event_armed = 1;

                continue;
            }

            /*
             * 当前路口已经处理过，但小车还没有离开 11。
             * 继续向前通过，不重复转弯。
             */
            DriveStraight(EVENT_SPEED);
            usleep(LOOP_DELAY_US);
            continue;
        }

        /*
         * 普通巡线。
         */
        FollowLine(left_black, right_black);

        print_timer += LOOP_DELAY_US;

        if (print_timer >= 500000U) {
            printf("track: L=%d R=%d state=%d armed=%d\r\n",
                   left_black,
                   right_black,
                   (int)state,
                   event_armed);

            print_timer = 0;
        }

        usleep(LOOP_DELAY_US);
    }
}


/*
 * ============================================================
 * 初始化
 * ============================================================
 */
static void LineMazeInit(void)
{
    WifiIotUartAttribute uart_attr;
    osThreadAttr_t attr;

    WatchDogDisable();

    GpioInit();

    /*
     * UART2 电机控制。
     */
    hi_io_set_func(HI_IO_NAME_GPIO_11,
                   HI_IO_FUNC_GPIO_11_UART2_TXD);

    hi_io_set_func(HI_IO_NAME_GPIO_12,
                   HI_IO_FUNC_GPIO_12_UART2_RXD);

    memset(&uart_attr, 0, sizeof(uart_attr));

    uart_attr.baudRate = 115200;
    uart_attr.dataBits = WIFI_IOT_UART_DATA_BIT_8;
    uart_attr.stopBits = WIFI_IOT_UART_STOP_BIT_1;
    uart_attr.parity = WIFI_IOT_UART_PARITY_NONE;

    UartInit(WIFI_IOT_UART_IDX_2,
             &uart_attr,
             NULL);

    /*
     * 左右红外传感器。
     */
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_13,
              WIFI_IOT_IO_FUNC_GPIO_13_GPIO);

    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_14,
              WIFI_IOT_IO_FUNC_GPIO_14_GPIO);

    GpioSetDir(TCRT_LEFT_GPIO,
               WIFI_IOT_GPIO_DIR_IN);

    GpioSetDir(TCRT_RIGHT_GPIO,
               WIFI_IOT_GPIO_DIR_IN);

    /*
     * 创建巡线任务。
     */
    memset(&attr, 0, sizeof(attr));

    attr.name = "line_maze";
    attr.stack_size = 6144;
    attr.priority = osPriorityNormal;

    if (osThreadNew(LineMazeTask, NULL, &attr) == NULL) {
        printf("create line_maze failed\r\n");
    }
}


APP_FEATURE_INIT(LineMazeInit);
