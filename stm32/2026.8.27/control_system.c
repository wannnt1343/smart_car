#include "control_system.h"
#include "encoder.h"
#include "motor.h" // ???????????,???? Set_Pwm
#include <stdio.h>
float car_speed = 0.0;
/*?? ?A ?B*/
int L_coder, R_coder;
int Motor_A, Motor_B;          //??PWM??
int OverflowTime = 100;
volatile uint32_t millis = 0;  // ?????
volatile uint32_t seconds = 0; // ????

/****************************************************************
????:??PI???(?A)
????:??????,????
??  ?:??PWM
****************************************************************/
int Incremental_PI_A(int Encoders_A, int Target_A)
{
    float Velocity_KP = 5.0, Velocity_KI = 0.016, Velocity_KD = 0.005;
    static int Pwm_A = 0;
    static int Integral_A = 0;
    static float Error_prev_A = 0;
    float MaxIntegral = 0.0;
    float MinIntegral = 0.0;
    float Error_A = (float)(Target_A - Encoders_A); // ????

    Integral_A += Error_A; // ?????

    // ????
    MaxIntegral = (float)(7199 / Velocity_KI);
    MinIntegral = -(float)(7199 / Velocity_KI);
    if (Integral_A > MaxIntegral) Integral_A = MaxIntegral;
    else if (Integral_A < MinIntegral) Integral_A = MinIntegral;

    Pwm_A += Velocity_KP * Error_A + Velocity_KD * (Error_A - Error_prev_A);

    if (Pwm_A > 7199) Pwm_A = 7199;
    else if (Pwm_A < -7199) Pwm_A = -7199;

    Error_prev_A = Error_A; // ???????

    return Pwm_A; // ????
}

/****************************************************************
????:??PI???(?B)
****************************************************************/
int Incremental_PI_B(int Encoders_B, int Target_B)
{
    float Velocity_KP = 5.0, Velocity_KI = 0.016, Velocity_KD = 0.005;
    static int Pwm_B = 0;
    static int Integral_B = 0;
    static float Error_prev_B = 0;
    float MaxIntegral = 0.0;
    float MinIntegral = 0.0;
    float Error_B = (float)(Target_B - Encoders_B); // ????

    Integral_B += Error_B; // ?????

    // ????
    MaxIntegral = (float)(7199 / Velocity_KI);
    MinIntegral = -(float)(7199 / Velocity_KI);
    if (Integral_B > MaxIntegral) Integral_B = MaxIntegral;
    else if (Integral_B < MinIntegral) Integral_B = MinIntegral;

    Pwm_B += Velocity_KP * Error_B + Velocity_KD * (Error_B - Error_prev_B);

    if (Pwm_B > 7199) Pwm_B = 7199;
    else if (Pwm_B < -7199) Pwm_B = -7199;

    Error_prev_B = Error_B; // ???????

    return Pwm_B; // ????
}

/****************************************************************
????:?????????
????:float rads (??: -1.5 ~ 1.5)
??  ?:int
****************************************************************/
int Rs_To_CPR(float rads)
{
    int CRP = 0;
    CRP = rads * ((700 * 4) / (1000 / OverflowTime));
    return CRP;
}

/****************************************************************
????:?????? (????? -> ??PID -> ??PWM)
****************************************************************/
void System_Control(void)
{
    //??????
    int TageA = 0;
    int TageB = 0;
    
    //??OverflowTime ms??????
    L_coder = Read_Encoder(2);
    R_coder = Read_Encoder(3);
    printf("left  coder : %d\r\n", L_coder);
    printf("right coder : %d\r\n", R_coder);

    //??OverflowTime????????????????
    TageA = Rs_To_CPR(car_speed);  
    TageB = Rs_To_CPR(car_speed);// ?????? -1.0?/?
    printf("TageA coder : %d\r\n", TageA);
    printf("TageB coder : %d\r\n", TageB);

    //????????????PWM 
    Motor_A = Incremental_PI_A(L_coder, TageA);         //===??????????A??PWM
    Motor_B = Incremental_PI_B(R_coder, TageB);
    printf("Motor_A pwm : %d\r\n", Motor_A);
    printf("Motor_B pwm : %d\r\n", Motor_B);

    Set_Pwm(Motor_A, Motor_B);           //??????
}

/**
  * @brief  ?????????????
  */
void SysTick_Handler(void)
{
    millis++; // ?????????,????1
    if (millis % OverflowTime == 0) // ???????100
    {
        millis = 0; // ?????
        //seconds++; // ???1
        
        System_Control();
    }
}
