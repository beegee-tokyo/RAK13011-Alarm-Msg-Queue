/**
 * @file app.h
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief 
 * @version 0.1
 * @date 2022-09-16
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include <Arduino.h>
/** Add you required includes after Arduino.h */
#include <Wire.h>
/** Include the WisBlock-API */
#include <WisBlock-API-V2.h> // Click to install library: http://librarymanager/All#WisBlock-API-V2

/** Include Cayenne LPP library */
#include <wisblock_cayenne.h>

// Debug output set to 0 to disable app debug output
#ifndef MY_DEBUG
#define MY_DEBUG 1
#endif

#if MY_DEBUG > 0
#define MYLOG(tag, ...)                     \
	do                                      \
	{                                       \
		if (tag)                            \
			PRINTF("[%s] ", tag);           \
		PRINTF(__VA_ARGS__);                \
		PRINTF("\n");                       \
		if (g_ble_uart_is_connected)        \
		{                                   \
			g_ble_uart.printf(__VA_ARGS__); \
			g_ble_uart.printf("\n");        \
		}                                   \
	} while (0)
#else
#define MYLOG(...)
#endif

#define LPP_CHANNEL_BATT 1	  // Base Board
#define LPP_CHANNEL_HUMID 2	  // RAK1901
#define LPP_CHANNEL_TEMP 3	  // RAK1901
#define LPP_CHANNEL_SWITCH 48 // RAK13011

extern WisCayenne g_solution_data;

/** Define the version of your SW */
#define SW_VERSION_1 1 // major version increase on API change / not backwards compatible
#define SW_VERSION_2 0 // minor version increase on API change / backward compatible
#define SW_VERSION_3 0 // patch version increase on bugfix, no affect on API

/** Application function definitions */
void setup_app(void);
bool init_app(void);
void app_event_handler(void);
void ble_data_handler(void) __attribute__((weak));
void lora_data_handler(void);
bool init_rak1901(void);
void read_rak1901(void);
