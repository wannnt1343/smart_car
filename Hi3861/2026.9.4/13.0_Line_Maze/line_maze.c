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


#define LINE_SPEED              22
#define STRAIGHT_TRIM           6

#define EVENT_SPEED             18
#define REVERSE_SPEED           38
#define PIVOT_SPEED             70


#define LOOP_DELAY_US           10000U
#define MOTOR_GAP_US            80000U

#define EVENT_CONFIRM_COUNT     3
#define EVENT_CONFIRM_GAP_US    4000U
#define EVENT_CLEAR_COUNT       8

#define JUNCTION_CENTER_US      300000U
#define TURN_90_US              1000000U    /* 加大到 1 秒 */
#define TURN_EXIT_US            500000U

#define DEAD_END_BACK_US        400000U
#define UTURN_US                1500000U
#define UTURN_EXIT_US           600000U


typedef enum {
    FIRST_LEFT = 0,
    SECOND_RIGHT,
    FINISH,
    DONE
} RouteState;


static void MotorCommand(int motor_a, int motor_b)
{
    unsigned char frame[6];
    unsigned char dir_a = 0, dir_b = 0;

    if (motor_a < 0) { dir_a = 1; motor_a = -motor_a; }
    if (motor_b < 0) { dir_b = 1; motor_b = -motor_b; }
    if (motor_a > 150) motor_a = 150;
    if (motor_b > 150) motor_b = 150;

    frame[0] = 0xFC;
    frame[1] = dir_a;
    frame[2] = (unsigned char)motor_a;
    frame[3] = dir_b;
    frame[4] = (unsigned char)motor_b;
    frame[5] = 0xFD;

    UartWrite(WIFI_IOT_UART_IDX_2, frame, sizeof(frame));
}


static void CarStop(void)           { MotorCommand(0, 0); }
static void DriveStraight(int s)    { MotorCommand(s, s + STRAIGHT_TRIM); }
static void CarReverse(void)        { MotorCommand(-REVERSE_SPEED, -REVERSE_SPEED); }
static void PivotLeft(void)         { MotorCommand(-PIVOT_SPEED, PIVOT_SPEED); }
static void PivotRight(void)        { MotorCommand(PIVOT_SPEED, -PIVOT_SPEED); }


static void ReadTape(int *left_black, int *right_black)
{
    WifiIotGpioValue lv, rv;
    int lc = 0, rc = 0;

    for (int i = 0; i < 3; i++) {
        GpioGetInputVal(TCRT_LEFT_GPIO,  &lv);
        GpioGetInputVal(TCRT_RIGHT_GPIO, &rv);
        if (lv == TCRT_BLACK_LEVEL) lc++;
        if (rv == TCRT_BLACK_LEVEL) rc++;
        usleep(1000);
    }
    *left_black  = (lc >= 2);
    *right_black = (rc >= 2);
}


/*
 * 巡线（保持你原来的版本，能走直线）
 */
static void FollowLine(int left_black, int right_black)
{
    if (left_black && right_black) {
        CarStop();
        return;
    }

    if (left_black && !right_black) {
        MotorCommand(0, 55);
    } else if (!left_black && right_black) {
        MotorCommand(55, 0);
    } else {
        DriveStraight(LINE_SPEED);
    }
}


static int ConfirmJunction(void)
{
    int lb, rb;
    CarStop();
    usleep(12000);
    for (int i = 0; i < EVENT_CONFIRM_COUNT; i++) {
        ReadTape(&lb, &rb);
        if (!(lb && rb)) return 0;
        usleep(EVENT_CONFIRM_GAP_US);
    }
    return 1;
}


/*
 * 转弯后用巡线跑一段，确保对准轨道。
 */
static void ExitWithFollowLine(unsigned int duration_us)
{
    unsigned int start = hi_get_us();
    int lb, rb;

    while ((unsigned int)(hi_get_us() - start) < duration_us) {
        ReadTape(&lb, &rb);
        if (lb && rb) {
            DriveStraight(EVENT_SPEED);
        } else {
            FollowLine(lb, rb);
        }
        usleep(LOOP_DELAY_US);
    }
    CarStop();
    usleep(MOTOR_GAP_US);
}


/*
 * 左转。
 */
static void TurnLeft(void)
{
    printf("--> turn left\r\n");

    DriveStraight(EVENT_SPEED);
    usleep(JUNCTION_CENTER_US);

    CarStop();
    usleep(MOTOR_GAP_US);

    PivotLeft();
    usleep(TURN_90_US);

    CarStop();
    usleep(MOTOR_GAP_US);

    /* 转弯后用巡线跑一段，对准轨道 */
    ExitWithFollowLine(TURN_EXIT_US);
}


/*
 * 右转。
 */
static void TurnRight(void)
{
    printf("--> turn right\r\n");

    DriveStraight(EVENT_SPEED);
    usleep(JUNCTION_CENTER_US);

    CarStop();
    usleep(MOTOR_GAP_US);

    PivotRight();
    usleep(TURN_90_US);

    CarStop();
    usleep(MOTOR_GAP_US);

    /* 转弯后用巡线跑一段，对准轨道 */
    ExitWithFollowLine(TURN_EXIT_US);
}


/*
 * 死路掉头，原路返回。
 */
static void UTurnAndReturn(void)
{
    printf("--> dead end, U-turn\r\n");

    CarStop();
    usleep(MOTOR_GAP_US);

    CarReverse();
    usleep(DEAD_END_BACK_US);

    CarStop();
    usleep(MOTOR_GAP_US);

    PivotRight();
    usleep(UTURN_US);

    CarStop();
    usleep(MOTOR_GAP_US);

    /* 掉头后用巡线跑一段，对准轨道 */
    ExitWithFollowLine(UTURN_EXIT_US);
}


static void FinishAndStop(void)
{
    CarStop();
    printf("--> finish! stopped\r\n");
    while (1) usleep(500000);
}


static void HandleJunction(RouteState *state)
{
    switch (*state) {
        case FIRST_LEFT:
            printf("junction: first -> left\r\n");
            TurnLeft();
            *state = SECOND_RIGHT;
            break;
        case SECOND_RIGHT:
            printf("junction: second -> right\r\n");
            TurnRight();
            *state = FINISH;
            break;
        case FINISH:
            printf("junction: finish\r\n");
            *state = DONE;
            FinishAndStop();
            break;
        default:
            break;
    }
}


static void HandleDeadEnd(void)
{
    printf("dead end: U-turn\r\n");
    UTurnAndReturn();
}


static void LineMazeTask(void *arg)
{
    RouteState state = FIRST_LEFT;
    int left_black, right_black;
    int junction_armed = 1;
    int dead_end_armed = 1;
    unsigned int print_timer = 0;

    (void)arg;
    printf("===== line maze start =====\r\n");
    CarStop();
    usleep(500000);

    while (1) {
        ReadTape(&left_black, &right_black);

        if (left_black && right_black) {
            if (junction_armed && ConfirmJunction()) {
                HandleJunction(&state);
                junction_armed = 0;
                dead_end_armed = 0;

                /* 转弯后等离开路口 */
                {
                    int lb, rb, clear = 0;
                    while (clear < EVENT_CLEAR_COUNT) {
                        DriveStraight(EVENT_SPEED);
                        usleep(LOOP_DELAY_US);
                        ReadTape(&lb, &rb);
                        if (lb && rb) clear = 0;
                        else clear++;
                    }
                    CarStop();
                    usleep(MOTOR_GAP_US);
                }

                junction_armed = 1;
                dead_end_armed = 1;
                continue;
            }

            if (dead_end_armed && ConfirmJunction()) {
                HandleDeadEnd();
                junction_armed = 0;
                dead_end_armed = 0;

                {
                    int lb, rb, clear = 0;
                    while (clear < EVENT_CLEAR_COUNT) {
                        DriveStraight(EVENT_SPEED);
                        usleep(LOOP_DELAY_US);
                        ReadTape(&lb, &rb);
                        if (lb && rb) clear = 0;
                        else clear++;
                    }
                    CarStop();
                    usleep(MOTOR_GAP_US);
                }

                junction_armed = 1;
                dead_end_armed = 1;
                continue;
            }

            DriveStraight(EVENT_SPEED);
            usleep(LOOP_DELAY_US);
            continue;
        }

        FollowLine(left_black, right_black);

        print_timer += LOOP_DELAY_US;
        if (print_timer >= 500000U) {
            printf("track: L=%d R=%d state=%d\r\n", left_black, right_black, (int)state);
            print_timer = 0;
        }
        usleep(LOOP_DELAY_US);
    }
}


static void LineMazeInit(void)
{
    WifiIotUartAttribute uart_attr;
    osThreadAttr_t attr;

    WatchDogDisable();
    GpioInit();

    hi_io_set_func(HI_IO_NAME_GPIO_11, HI_IO_FUNC_GPIO_11_UART2_TXD);
    hi_io_set_func(HI_IO_NAME_GPIO_12, HI_IO_FUNC_GPIO_12_UART2_RXD);

    memset(&uart_attr, 0, sizeof(uart_attr));
    uart_attr.baudRate = 115200;
    uart_attr.dataBits = WIFI_IOT_UART_DATA_BIT_8;
    uart_attr.stopBits = WIFI_IOT_UART_STOP_BIT_1;
    uart_attr.parity   = WIFI_IOT_UART_PARITY_NONE;
    UartInit(WIFI_IOT_UART_IDX_2, &uart_attr, NULL);

    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_13, WIFI_IOT_IO_FUNC_GPIO_13_GPIO);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_14, WIFI_IOT_IO_FUNC_GPIO_14_GPIO);
    GpioSetDir(TCRT_LEFT_GPIO,  WIFI_IOT_GPIO_DIR_IN);
    GpioSetDir(TCRT_RIGHT_GPIO, WIFI_IOT_GPIO_DIR_IN);

    memset(&attr, 0, sizeof(attr));
    attr.name       = "line_maze";
    attr.stack_size = 6144;
    attr.priority   = osPriorityNormal;

    if (osThreadNew(LineMazeTask, NULL, &attr) == NULL)
        printf("create line_maze failed\r\n");
}

APP_FEATURE_INIT(LineMazeInit);
