/**
   @file RAK13011-Alarm.ino
   @author Bernd Giesecke (bernd.giesecke@rakwireless.com)
   @brief Simple example for the RAK13011
   @version 0.1
   @date 2021-09-10

   @copyright Copyright (c) 2021

*/
#include "app.h"

bool has_rak1901 = false;

/** Application stuff */

/** Timer to wakeup task frequently and send message */
SoftwareTimer delayed_sending;

/** Timer since last position message was sent */
time_t last_pos_send = 0;

/** LoRaWAN packet */
WisCayenne g_solution_data(255);

/** Set the device name, max length is 10 characters */
char g_ble_dev_name[10] = "RAK-TEST";

/** Flag showing if TX cycle is ongoing */
bool lora_busy = false;

/** Send Fail counter **/
uint8_t send_fail = 0;

uint32_t switch_status = LOW;

uint32_t last_switch_status = 0;

#define SWITCH_CHANGE 0b1000000000000000
#define N_SWITCH_CHANGE 0b0111111111111111

//******************************************************************//
// RAK13011 INT1_PIN
//******************************************************************//
// Slot A      WB_IO1
// Slot B      WB_IO2 ( not recommended, pin conflict with IO2)
// Slot C      WB_IO3
// Slot D      WB_IO5
// Slot E      WB_IO4
// Slot F      WB_IO6
//******************************************************************//

#define SWITCH_INT WB_IO3

// Event queue stuff
struct sw_events
{
	bool status;
};

QueueHandle_t event_queue;

/**
 * @brief Timer function used to avoid sending packages too often.
 *       Delays the next package by 10 seconds
 *
 * @param unused
 *      Timer handle, not used
 */
void send_delayed(TimerHandle_t unused)
{
	delayed_sending.stop();
	api_wake_loop(STATUS);
}

void switch_int_handler(void)
{
	switch_status = digitalRead(SWITCH_INT);
	// if (switch_status == LOW)
	// {
	// 	digitalWrite(LED_GREEN, HIGH);
	// 	digitalWrite(LED_BLUE, LOW);
	// }
	// else
	// {
	// 	digitalWrite(LED_GREEN, LOW);
	// 	digitalWrite(LED_BLUE, HIGH);
	// }
	api_wake_loop(SWITCH_CHANGE);
}

/**
   @brief Application specific setup functions

*/
void setup_app(void)
{
	Serial.begin(115200);
	time_t serial_timeout = millis();
	// On nRF52840 the USB serial is not available immediately
	while (!Serial)
	{
		if ((millis() - serial_timeout) < 5000)
		{
			delay(100);
			digitalWrite(LED_GREEN, !digitalRead(LED_GREEN));
		}
		else
		{
			break;
		}
	}
	digitalWrite(LED_GREEN, LOW);

	MYLOG("APP", "WisBlock Switch Node");

#ifdef NRF52_SERIES
	// Enable BLE
	g_enable_ble = true;
#endif
}

/**
   @brief Application specific initializations

   @return true Initialization success
   @return false Initialization failure
*/
bool init_app(void)
{
	MYLOG("APP", "init_app");
	pinMode(SWITCH_INT, INPUT);
	pinMode(WB_IO2, OUTPUT);
	digitalWrite(WB_IO2, LOW);

	// Setup event queue
	event_queue = xQueueCreate(50, sizeof(uint32_t));

	attachInterrupt(SWITCH_INT, switch_int_handler, CHANGE);
	switch_status = digitalRead(SWITCH_INT);
	last_switch_status = switch_status;

	// if (switch_status == LOW)
	// {
	// 	digitalWrite(LED_GREEN, HIGH);
	// 	digitalWrite(LED_BLUE, LOW);
	// }
	// else
	// {
	// 	digitalWrite(LED_GREEN, LOW);
	// 	digitalWrite(LED_BLUE, HIGH);
	// }

	// Prepare timer for delayed sending
	delayed_sending.begin(10000, send_delayed, NULL, false);

	// Remember last send time
	last_pos_send = millis();

	// Check if RAK1901 is available
	Wire.begin();
	has_rak1901 = init_rak1901();

	if (!g_lorawan_settings.lorawan_enable)
	{
		// LoRa P2P, send a first packet now
		delayed_sending.start();
	}
	return true;
}

/**
   @brief Application specific event handler
		  Requires as minimum the handling of STATUS event
		  Here you handle as well your application specific events
*/
void app_event_handler(void)
{
	// Switch triggered event
	if ((g_task_event_type & SWITCH_CHANGE) == SWITCH_CHANGE)
	{
		g_task_event_type &= N_SWITCH_CHANGE;
		if (switch_status == (uint32_t)digitalRead(SWITCH_INT))
		{
			MYLOG("SWITCH", "Switch Status confirmed");
		}
		else
		{
			MYLOG("SWITCH", "Switch bouncing");
			return;
		}

		// Add event to queue
		MYLOG("APP", "Adding event to queue, pending %ld", uxQueueMessagesWaiting(event_queue));
		xQueueSendToBack(event_queue, &switch_status, 0);

		// if ((millis() - last_pos_send) < 10000)
		if (lora_busy)
		{
			delayed_sending.stop();
			delayed_sending.setPeriod(10000);
			delayed_sending.start();
			return;
		}
		else
		{
			g_task_event_type |= STATUS;
		}
	}

	// Timer triggered event
	if ((g_task_event_type & STATUS) == STATUS)
	{
		g_task_event_type &= N_STATUS;
		MYLOG("APP", "Timer wakeup");

#ifdef NRF52_SERIES
		// If BLE is enabled, restart Advertising
		// if (g_enable_ble)
		// {
		// 	restart_advertising(15);
		// }
#endif
		if (lora_busy)
		{
			MYLOG("APP", "LoRaWAN TX cycle not finished, skip this event");
			// if (g_enable_ble)
			// {
			// 	restart_advertising(15);
			// }
		}
		else
		{
			if (g_lorawan_settings.lorawan_enable)
			{ // Skip sending if not yet joined
				if (g_lpwan_has_joined)
				{
					// Reset the packet
					g_solution_data.reset();

					// Get battery level
					float batt_level_f = read_batt();
					g_solution_data.addVoltage(LPP_CHANNEL_BATT, batt_level_f / 1000.0);

					// Check if an event is in the queue
					if (uxQueueMessagesWaiting(event_queue) != 0)
					{
						// Add switch status, get the event out of the queue
						xQueueReceive(event_queue, &last_switch_status, 0);
					}
					else
					{
						MYLOG("APP", "Queue is empty, using last status");
					}

					MYLOG("APP", "Pulled event from queue, pending %ld", uxQueueMessagesWaiting(event_queue));
					MYLOG("APP", "Last event from queue was %ld", last_switch_status);

					g_solution_data.addPresence(LPP_CHANNEL_SWITCH, last_switch_status);

					// Add temperature & humidity if the sensor is available
					if (has_rak1901)
					{
						read_rak1901();
					}
					lmh_error_status result = send_lora_packet(g_solution_data.getBuffer(), g_solution_data.getSize());
					switch (result)
					{
					case LMH_SUCCESS:
						MYLOG("APP", "Packet enqueued");
						// Set a flag that TX cycle is running
						lora_busy = true;

						// Remember last send time
						last_pos_send = millis();

						if (xQueuePeek(event_queue, &last_switch_status, 0) == pdTRUE)
						{
							delayed_sending.stop();
							delayed_sending.setPeriod(10000);
							delayed_sending.start();
						}
						break;
					case LMH_BUSY:
						MYLOG("APP", "LoRa transceiver is busy");
						break;
					case LMH_ERROR:
						MYLOG("APP", "Packet error, too big to send with current DR");
						break;
					}
				}
				else
				{
					MYLOG("APP", "Not yet joined");
				}
			}
			else
			{
				// Add unique identifier in front of the P2P packet, here we use the DevEUI
				g_solution_data.addDevID(0, &g_lorawan_settings.node_device_eui[4]);

				if (send_p2p_packet(g_solution_data.getBuffer(), g_solution_data.getSize()))
				{
					MYLOG("APP", "Packet enqueued");
				}
				else
				{
					AT_PRINTF("+EVT:SIZE_ERROR\n");
					MYLOG("APP", "Packet too big");
				}
			}
		}
	}
}

#ifdef NRF52_SERIES
/**
   @brief Handle BLE UART data

*/
void ble_data_handler(void)
{
	if (g_enable_ble)
	{
		/**************************************************************/
		/**************************************************************/
		/// \todo BLE UART data arrived
		/// \todo or forward them to the AT command interpreter
		/// \todo parse them here
		/**************************************************************/
		/**************************************************************/
		if ((g_task_event_type & BLE_DATA) == BLE_DATA)
		{
			MYLOG("AT", "RECEIVED BLE");
			// BLE UART data arrived
			// in this example we forward it to the AT command interpreter
			g_task_event_type &= N_BLE_DATA;

			while (g_ble_uart.available() > 0)
			{
				at_serial_input(uint8_t(g_ble_uart.read()));
				delay(5);
			}
			at_serial_input(uint8_t('\n'));
		}
	}
}
#endif

/**
   @brief Handle received LoRa Data

*/
void lora_data_handler(void)
{
	// LoRa Join finished handling
	if ((g_task_event_type & LORA_JOIN_FIN) == LORA_JOIN_FIN)
	{
		g_task_event_type &= N_LORA_JOIN_FIN;
		if (g_join_result)
		{
			MYLOG("APP", "Successfully joined network");

			// Force a sensor reading in 10 seconds
			last_pos_send = millis();
			delayed_sending.stop();
			delayed_sending.setPeriod(5000);
			delayed_sending.start();
		}
		else
		{
			MYLOG("APP", "Join network failed");
			/// \todo here join could be restarted.
			// lmh_join();
		}
	}

	// LoRa data handling
	if ((g_task_event_type & LORA_DATA) == LORA_DATA)
	{
		/**************************************************************/
		/**************************************************************/
		/// \todo LoRa data arrived
		/// \todo parse them here
		/**************************************************************/
		/**************************************************************/
		g_task_event_type &= N_LORA_DATA;
		MYLOG("APP", "Received package over LoRa");
		char log_buff[g_rx_data_len * 3] = {0};
		uint8_t log_idx = 0;
		for (int idx = 0; idx < g_rx_data_len; idx++)
		{
			sprintf(&log_buff[log_idx], "%02X ", g_rx_lora_data[idx]);
			log_idx += 3;
		}
		lora_busy = false;
		MYLOG("APP", "%s", log_buff);
	}

	// LoRa TX finished handling
	if ((g_task_event_type & LORA_TX_FIN) == LORA_TX_FIN)
	{
		g_task_event_type &= N_LORA_TX_FIN;

		MYLOG("APP", "LPWAN TX cycle %s", g_rx_fin_result ? "finished ACK" : "failed NAK");

		if (!g_rx_fin_result)
		{
			// Increase fail send counter
			send_fail++;

			if (send_fail == 10)
			{
				// Too many failed sendings, reset node and try to rejoin
				delay(100);
				api_reset();
			}
		}

		// Clear the LoRa TX flag
		lora_busy = false;
	}
}
