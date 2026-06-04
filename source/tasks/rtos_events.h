/**
 * @file rtos_events.h
 * @author Adam Shebani (adamsheb414@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2026-05-23
 * 
 * @copyright Copyright (c) 2026
 * 
 */

 #ifndef __RTOS_EVENTS_H__
 #define __RTOS_EVENTS_H__
 
#ifdef USE_FREERTOS

/*******************************************************************************
* Event Group for system events.
 ******************************************************************************/
extern EventGroupHandle_t ECE353_RTOS_Events;

/*******************************************************************************
* Macros used to define the system events
******************************************************************************/

#define ECE353_RTOS_EVENTS_IPC_ACK_RECEIVED (1 << 0)
#define ECE353_RTOS_EVENTS_SW1 (1 << 1)
#define ECE353_RTOS_EVENTS_SW2 (1 << 2) 
#define ECE353_RTOS_EVENTS_SW3 (1 << 3)
#define ECE353_RTOS_EVENTS_JOYSTICK (1 << 4)
#define ECE353_RTOS_EVENTS_IPC_READY_RECEIVED (1 << 5)
#define ECE353_RTOS_EVENTS_IPC_ACTIVE_RECEIVED (1 << 6)
#define ECE353_RTOS_EVENTS_IPC_GUESS_RECEIVED (1 << 7)
#define ECE353_RTOS_EVENTS_IPC_RESPONSE_RECEIVED (1 << 8) 
#define ECE353_RTOS_EVENTS_IPC_RESET_RECEIVED (1 << 9)



#endif // ECE353_FREERTOS

#endif // __RTOS_EVENTS_H__