#include "stm32f10x.h"
#include "sys.h"
#include "encoder.h"
#include "motor.h"
#include "control_system.h"
#include <stdio.h>

int main(void)
{
    Stm32_Clock_Init(9);            
    MY_NVIC_PriorityGroupConfig(2); 
    uart_init(115200);              
    
    JTAG_Set(JTAG_SWD_DISABLE);     
    JTAG_Set(SWD_ENABLE);           
    
    Encoder_Init_TIM2();            
    Encoder_Init_TIM3();            
    
    PWM_Init(7199, 9);              
    
    // ???1:?????
    // ??????,??????
    colorful_led_Init();         

    SysTick_Config(72000000/1000);  

    printf("QST??????!\r\n");

    /** ???? **/
    while(1)
    {
        // 1. ??? 10 ?
        car_speed = 1.0; 
        delay_ms(10000); // 10000?? = 10?

        // 2. ??? 10 ?
        car_speed = -1.0;
        delay_ms(10000);
        
        // (???????????,??????? car_speed = 0.0; delay_ms(10000);)
    }
}
