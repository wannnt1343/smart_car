#include "nfc.h"
#include "delay.h"           // ????
#include "usart.h"           // ??????(UART2SendFrame)
#include "colorful_led.h"    // ??????????

// --- ?????? ---
u8 const NFC_WakeUp[] = {0x55, 0x55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0x03, 0xFD, 0xD4, 0x14, 0x01, 0x17, 0x00}; //????
u8 const NFC_SearchCard[] = {0x00, 0x00, 0xFF, 0x04, 0xFC, 0xD4, 0x4A, 0x01, 0x00, 0xE1, 0x00};   //????

// --- ????? ---
u8 NFC_WakeUp_Ok = 0;           //NFC????
u8 NFC_find_Card = 0;           //NFC?????
u8 NFC_sendcmd_find = 1;        //NFC???????
u8 NFC_wait_Card = 0;
u8 NFC_read_id_flag = 0;
u8 NFC_DataBlock[16];           //????BLOCK???

u8 USART2_RX_BUF[USART2_REC_LEN]; //????
u16 USART2_RX_STA = 0;          //??????
u16 slen;                       //??????
u8 Sys_Stat;                    //nfc id???
u8 Sum = 0;                     //???
u8 REC_LEN = 0;
u8 led_flag = 0;                //??????

UART_Frame_TypeDef UART2Frame;  // ?????????

// --- ???????? ---
void NFC_Handler(void)
{
    if(NFC_WakeUp_Ok) //???,?????
    {
        if(NFC_find_Card == 1) //???????
        {
            //?????
            FoundCard_Handler();
        }
        else if(NFC_find_Card == 0 && NFC_sendcmd_find == 1)
        {
            UART2Frame.RxCounter = 0;
            //????,???
            UART2SendFrame((u8*)NFC_SearchCard, sizeof(NFC_SearchCard)); //??????
            NFC_sendcmd_find = 0;
            delay_ms(200);
        }
    }
}

// --- ??????????? ---
void FoundCard_Handler(void)      //?????????
{
    NFC_find_Card = 0;    //????
    if(led_flag == 0)     //????
    {
        led_flag = 1;
        R_led_mode();     // ??????????
    }
    else
    {
        led_flag = 0;
        R_led_CLC();      // ????
    }
    
    NFC_sendcmd_find = 1; //??????
    delay_ms(200);
}
// --- ??2???????? (NFC???????) ---
void USART2_IRQHandler(void)
{
    if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)  //????
    {
        UART2Frame.RxBuffer[ UART2Frame.RxCounter ] = USART_ReceiveData(USART2);//???????

        if(NFC_WakeUp_Ok==0)            //???
        {
            UART2Frame.RxCounter++;
            if(UART2Frame.RxCounter==15)
            {
                memcpy(USART2_RX_BUF, (uint8_t*)UART2Frame.RxBuffer, 15);
                memset((uint8_t*)UART2Frame.RxBuffer, 0, 20);
                UART2Frame.RxCounter=0;
                
                NFC_WakeUp_Ok = 1;      // ??!????????,???????1,?????????
            }
        }
        else                            //????,??????
        {
            UART2Frame.RxCounter++;
            if(UART2Frame.RxCounter==25)
            {
                memcpy(USART2_RX_BUF, (uint8_t*)UART2Frame.RxBuffer, 25);
                put_HEX(USART1, USART2_RX_BUF, 25);
                
                // ?????????????:25:F7:48:06
                if(
                ((0x25==USART2_RX_BUF[19])&&(0xF7==USART2_RX_BUF[20])&&(0x48==USART2_RX_BUF[21])&&(0x06==USART2_RX_BUF[22]))
                ||((0x50==USART2_RX_BUF[19])&&(0x84==USART2_RX_BUF[20])&&(0xFC==USART2_RX_BUF[21])&&(0x23==USART2_RX_BUF[22]))
                ||((0x40==USART2_RX_BUF[19])&&(0x74==USART2_RX_BUF[20])&&(0x80==USART2_RX_BUF[21])&&(0x23==USART2_RX_BUF[22]))
                )
                {
                    NFC_find_Card = 1;  // ?????,????
                }
                
                memset((uint8_t*)UART2Frame.RxBuffer, 0, 50);
                memset((uint8_t*)USART2_RX_BUF, 0, 50);
                UART2Frame.RxCounter=0;
            }
        }
    }
}
// ==========================================
// ????????? (???? usart.c ?)
// ==========================================

// 1. ??2????? (????NFC??, ???? PA2, PA3)
void uart2_init(u32 bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
     
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
 
    // USART2_TX   PA2
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
   
    // USART2_RX	  PA3
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
 
    // Usart2 NVIC ??
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3; //?????
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;        //????
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;           //IRQ????
    NVIC_Init(&NVIC_InitStructure);
 
    // USART ?????
    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
 
    USART_Init(USART2, &USART_InitStructure);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE); //????????
    USART_Cmd(USART2, ENABLE);                     //????
}

// 2. ??2??????? (????NFC??)
void UART2SendFrame(u8* buffer, u16 length)
{
    u16 i;
    for(i = 0; i < length; i++)
    {
        USART_SendData(USART2, buffer[i]);
        while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET); // ??????
    }
}

// 3. ?????????? (????1???????????)
void put_HEX(USART_TypeDef* USARTx, u8 *buf, u16 len)
{
    u16 i;
    u8 hex_chars[] = "0123456789ABCDEF";
    for(i = 0; i < len; i++) 
    {
        // ???4?
        USART_SendData(USARTx, hex_chars[(buf[i] >> 4) & 0x0F]);
        while(USART_GetFlagStatus(USARTx, USART_FLAG_TC) == RESET);
        // ???4?
        USART_SendData(USARTx, hex_chars[buf[i] & 0x0F]);
        while(USART_GetFlagStatus(USARTx, USART_FLAG_TC) == RESET);
        // ??????
        USART_SendData(USARTx, ' ');
        while(USART_GetFlagStatus(USARTx, USART_FLAG_TC) == RESET);
    }
    // ??
    USART_SendData(USARTx, '\r');
    while(USART_GetFlagStatus(USARTx, USART_FLAG_TC) == RESET);
    USART_SendData(USARTx, '\n');
    while(USART_GetFlagStatus(USARTx, USART_FLAG_TC) == RESET);
}
