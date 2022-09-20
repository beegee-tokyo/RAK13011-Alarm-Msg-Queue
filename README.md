# RAK13011-Alarm-Msg-Queue

Example for RAK13011 magnetic switch. Sending status over LoRaWAN with event queue to make sure events are not missed.

Based on WisBlock-API, it uses the RAK4631 Core module for LoRa and BLE, the RAK1901 (optional) to measure temperature and humidity and the RAK13011 as interface to the magnetic switch.

As LoRaWAN TX cycles can be up to 6 seconds long, the code implemented a message queue for up to 50 open/close events and sends the events until the message queue is empty.