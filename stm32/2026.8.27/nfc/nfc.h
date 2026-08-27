#ifndef __NFC_H
#define __NFC_H

#include "stm32f10x.h"  // ????????
#include "string.h"
#include "sys.h"        // ???? u8, u16 ?????

#define USART2_REC_LEN 200 // ????????

// --- ?????????? ---
typedef struct {
    u8 RxBuffer[50];
    u8 RxCounter;
} UART_Frame_TypeDef;

// --- ?????? (??????main.c??) ---
extern u8 NFC_WakeUp_Ok;
extern u8 NFC_find_Card;
extern u8 NFC_sendcmd_find;
extern u8 USART2_RX_BUF[USART2_REC_LEN];
extern UART_Frame_TypeDef UART2Frame;
extern u8 led_flag;
extern const u8 NFC_WakeUp[]; 
extern const u8 NFC_SearchCard[]; 

// --- NFC ???????? ---
void NFC_Handler(void);
void FoundCard_Handler(void);
void NFC_user_Handler(void);

// --- ??????????? (????) ---
void uart2_init(u32 bound);
void UART2SendFrame(u8* buffer, u16 length);
void put_HEX(USART_TypeDef* USARTx, u8 *buf, u16 len);

#endif
