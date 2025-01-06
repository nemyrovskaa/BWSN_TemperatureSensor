# Implementation of Temperature Sensor for BWSN project

2024

This repository contains the software for the Temperature Sensor (Temp Sensor) node, part of the Body Wireless Sensor Network (BWSN) project. The project is currently in development.

## BWSN Project Overview

The Body Wireless Sensor Network is being designed to monitor human vital signs in extreme conditions, with potential applications in military operations, research expeditions, and extreme tourism.

The system comprises the following components:
- Sensors: Wireless nodes that collect vital sign measurements.
- AM-Gateway: A node responsible for aggregating and analysing data.

The system operates by collecting physiological data from wearable sensors and transmitting this information to the AM-Gateway. The aggregated data is then processed to identify critical health conditions of the monitored individual. Bluetooth Low Energy (BLE) technology was chosen for communication within the body sensor network due to its energy efficiency and reliability.

In the future, the system could be expanded by integrating a satellite communication module and implementing an Internet-based mechanism for transmitting data from the AM-Gateway to a central data center (CDC). Authorised users will be able to request access to view the data of monitored individuals. In cases of detected critical conditions, the system will enable the identification of the person's location and initiate appropriate rescue actions.

The prototype of the wearable sensor network focuses on monitoring critical physiological parameters, including blood oxygen saturation (SpO2), heart rate (pulse), body temperature, and body movement and posture. To achieve this, the following sensors were selected for the network nodes:

- Pulse oximeter - combines heart rate and oxygen saturation monitoring.
- Thermometer - measures the body temperature of the individual.
- Inertial measurement unit (IMU) - tracks body movement and posture in space.

### Planned System Workflow
To prepare the system for operation, a registration process must be completed, where sensors are registered on the AM-Gateway and vice versa. After successful registration, the sensor’s list of registered devices will include the AM-Gateway to which it will send its measurements. Similarly, the AM-Gateway will maintain a list of registered sensors from which it will receive data.

The network operates on a synchronised timer. All devices alternate between two modes: data transmission/reception and deep sleep. Once registration is complete, the sensors switch to periodic directed advertising, while the AM-Gateway enters a periodic scanning mode to capture packets from registered sensors. Upon receiving a packet, the AM-Gateway stores the transmitted data in non-volatile memory.

Before entering sleep mode, the AM-Gateway’s controller analyses the received data to detect any critical health conditions. If such a condition is identified, the system generates an alert message that can be sent to the CDC for further action.

## Temperature Sensor

The Temp Sensor is implemented using the ESP32-C3 SuperMini microcontroller, which supports Bluetooth LE and temperature sensor MAX30205. The Temp Sensor reads data from temperature sensor and sends data to the AM-Gateway. Additionally, a button (connected to GPIO3) allows users to configure the Temp Sensor.

### Current Features
- AM-Gateway registration
- AM-Gateway deletion
- Sending temperature data to the AM-Gateway
- Switching between deep sleep and wake modes

### Workflow Description
The Temp Sensor operates in three modes: Registration, Deletion, and a general Unspecified mode. It maintains a whitelist with registered AM-Gateway.

*1. AM-Gateway Registration:*
In this mode, the Temp Sensor sends advertising packets to AM-Gateway. If the AM-Gateway identifies a AM Temp Sensor as a sensor of interest, it establishes a connection, so Temp Sensor adds the AM-Gateway to the whitelist, and disconnects.

*2. Data Collection:*
If registered AM-Gateway exists, the Temp Sensor periodically wakes up to advertise packets containing data and then goes to sleep.

*3. AM-Gateway Deletion:*
In this mode, the Temp Sensor sends advertising packets to AM-Gateway to be deleted. If deletion is possible, the AM-Gateway establishes a connection, so the Temp Sensor deletes the AM-Gateway from the whitelist, and disconnects. If no the AM-Gateway remains in the whitelist, the Temp Sensor enters deep sleep mode to conserve energy.

## User Guide

### AM-Gateway Registration

1. Press the button for 1–5 seconds to enter registration mode.
2. Ensure the AM-Gateway is also in registration mode and is within range.
3. Successful registration will be indicated by rapid LED blinking.
4. Exit registration mode by pressing the button again for 1–5 seconds.

### AM-Gateway Deletion

1. Press the button for at least 5 seconds to enter deletion mode.
2. Ensure the AM-Gateway is also in deletion mode and is within range.
3. Successful deletion will be indicated by slow LED blinking.
4. Exit deletion mode by pressing the button again for at least 5 seconds.

*Note:* Data transmission and reception can be identified by the periodic flashing of the LED (1s on, 5s off).
