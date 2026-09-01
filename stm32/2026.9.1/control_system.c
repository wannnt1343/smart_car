#include "control_system.h"
#include "encoder.h"
#include "motor.h"
#include <stdio.h>


// =========================================================
// ??????
// =========================================================

float car_speed = 0.0f;

int L_coder = 0;
int R_coder = 0;

int Motor_A = 0;
int Motor_B = 0;

int OverflowTime = 100;

volatile uint32_t millis = 0;
volatile uint32_t seconds = 0;


// =========================================================
// ?? PID
// =========================================================

int Incremental_PI_A(int Encoders_A, int Target_A)
{
    float Velocity_KP = 5.0f;
    float Velocity_KD = 0.005f;

    static int Pwm_A = 0;
    static float Error_prev_A = 0;

    float Error_A;


    Error_A = (float)(Target_A - Encoders_A);


    Pwm_A +=
        Velocity_KP * Error_A
        +
        Velocity_KD * (Error_A - Error_prev_A);


    if(Pwm_A > 7199)
    {
        Pwm_A = 7199;
    }

    if(Pwm_A < -7199)
    {
        Pwm_A = -7199;
    }


    Error_prev_A = Error_A;


    return Pwm_A;
}


// =========================================================
// ?? PID
// =========================================================

int Incremental_PI_B(int Encoders_B, int Target_B)
{
    float Velocity_KP = 5.0f;
    float Velocity_KD = 0.005f;

    static int Pwm_B = 0;
    static float Error_prev_B = 0;

    float Error_B;


    Error_B = (float)(Target_B - Encoders_B);


    Pwm_B +=
        Velocity_KP * Error_B
        +
        Velocity_KD * (Error_B - Error_prev_B);


    if(Pwm_B > 7199)
    {
        Pwm_B = 7199;
    }

    if(Pwm_B < -7199)
    {
        Pwm_B = -7199;
    }


    Error_prev_B = Error_B;


    return Pwm_B;
}


// =========================================================
// rad/s ???????
// =========================================================

int Rs_To_CPR(float rads)
{
    int CPR;

    CPR =
        (int)(
            rads *
            ((700 * 4) / (1000 / OverflowTime))
        );

    return CPR;
}


// =========================================================
// Hi3861 ???????
// =========================================================

float car_speed_A = 0.0f;
float car_speed_B = 0.0f;


// =========================================================
// ??????
// =========================================================

void System_Control(void)
{
    int Target_A;
    int Target_B;


    // =====================================================
    // ?????????? 0
    // ??? PID
    //
    // ??:
    // ????? PID ????
    // ????????
    // =====================================================

    if(
        car_speed_A == 0.0f
        &&
        car_speed_B == 0.0f
    )
    {
        Motor_A = 0;
        Motor_B = 0;

        Set_Pwm(0, 0);

        // ???????
        Read_Encoder(2);
        Read_Encoder(3);

        return;
    }


    // =====================================================
    // ?????
    // =====================================================

    L_coder = Read_Encoder(2);

    R_coder = Read_Encoder(3);


    // =====================================================
    // ?????????
    // =====================================================

    Target_A = Rs_To_CPR(car_speed_A);

    Target_B = Rs_To_CPR(car_speed_B);


    // =====================================================
    // PID
    // =====================================================

    Motor_A =
        Incremental_PI_A(
            L_coder,
            Target_A
        );


    Motor_B =
        Incremental_PI_B(
            R_coder,
            Target_B
        );


    // =====================================================
    // ?? PWM
    // =====================================================

    Set_Pwm(
        Motor_A,
        Motor_B
    );
}


// =========================================================
// SysTick ??
//
// 1ms???
// ?100ms?????PID
// =========================================================

void SysTick_Handler(void)
{
    millis++;


    if(millis >= OverflowTime)
    {
        millis = 0;

        System_Control();
    }
}