#include "motor.h"

/**************************************************************************
????:???????
????:?
??  ?:?
**************************************************************************/
void Motor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); //??PB????
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14|GPIO_Pin_13; //????
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;       //????
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;      //50M
    GPIO_Init(GPIOB, &GPIO_InitStructure);               //?????????GPIOB
AIN=0;
BIN=0;
}

/**************************************************************************
????:?????????pwm
????:?
??  ?:?
**************************************************************************/
void PWM_Init(u16 arr,u16 psc)
{
    GPIO_InitTypeDef GPIO_InitStructure;
TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
TIM_OCInitTypeDef  TIM_OCInitStructure;
    Motor_Init();
RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);//
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB , ENABLE);  //??GPIO??????
        //????????????,??TIM1 CH1 CH4?PWM????
GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6|GPIO_Pin_7; //TIM_CH1 //TIM_CH4
GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;  //??????
GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
GPIO_Init(GPIOB, &GPIO_InitStructure);

TIM_TimeBaseStructure.TIM_Period = arr; //???????????????????????????
TIM_TimeBaseStructure.TIM_Prescaler =psc; //??????TIMx???????????  ???
TIM_TimeBaseStructure.TIM_ClockDivision = 0; //??????:TDTS = Tck_tim
TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;  //TIM??????
TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure); //??TIM_TimeBaseInitStruct?????????TIMx???????


TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1; //???????:TIM????????1
TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable; //??????
TIM_OCInitStructure.TIM_Pulse = 0;                            //????????????????
TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;     //????:TIM???????
TIM_OC1Init(TIM4, &TIM_OCInitStructure);  //??TIM_OCInitStruct???????????TIMx
TIM_OC2Init(TIM4, &TIM_OCInitStructure);  //??TIM_OCInitStruct???????????TIMx

    TIM_CtrlPWMOutputs(TIM4,ENABLE); //MOE ?????
    
TIM_OC1PreloadConfig(TIM4, TIM_OCPreload_Enable);  //CH1?????
TIM_OC2PreloadConfig(TIM4, TIM_OCPreload_Enable);  //CH4?????

TIM_ARRPreloadConfig(TIM4, ENABLE); //??TIMx?ARR????????

TIM_Cmd(TIM4, ENABLE);  //??TIM1

}


u32 myabs(long int a)
{
    u32 temp;
if(a<0)
temp=-a;
    else
temp=a;

return temp;
}

void Set_Pwm(int moto1, int moto2)
{
    //XIN PWMX?motro.h????
    if(moto2>=0) {
AIN=0;
PWMA=myabs(moto2);
}else{
AIN=1;
PWMA=7199-myabs(moto2);
}

    if(moto1>=0){
BIN=0;
PWMB=myabs(moto1);
}else{
BIN=1;
PWMB=7199-myabs(moto1);
}
}
