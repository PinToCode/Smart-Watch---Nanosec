# BLE Smart Watch -- nRF52840 Sense Based Wearable System

## Project Overview

This project is a custom-designed BLE-enabled smart watch built using
the Seeed Studio XIAO nRF52840 Sense. The system connects to a
smartphone via Bluetooth Low Energy (BLE), synchronizes time, receives
notifications, and uses IMU-based motion detection to optimize power
consumption.

The watch provides haptic (vibration) feedback for notifications instead
of using sound.

------------------------------------------------------------------------

## Key Features

-   BLE 5.0 based smartphone connectivity
-   Automatic time synchronization from phone
-   Incoming call notifications
-   WhatsApp notifications
-   Instagram notifications
-   IMU-based wrist detection to turn ON OLED display
-   Power-efficient display control
-   Vibration motor for silent physical alerts

------------------------------------------------------------------------

## Hardware Components

-   Microcontroller: Seeed Studio XIAO nRF52840 Sense
-   SoC: Nordic nRF52840 (ARM Cortex-M4F with BLE 5.0)
-   Display: 1.3 inch I2C OLED
-   IMU: Built-in 6-axis sensor (on-board)
-   Battery: 3.7V 400mAh Li-ion
-   Motor Driver: NPN Transistor with base resistor
-   Notification Output: Vibration Motor

------------------------------------------------------------------------

## System Architecture

### BLE Configuration

-   Smart Watch → BLE Peripheral
-   Smartphone App → BLE Central

### Communication Flow

1.  Smartphone scans and connects to the watch.
2.  Time synchronization data is transmitted.
3.  Notification data is sent through BLE characteristics.
4.  Watch processes data and:
    -   Updates OLED display
    -   Activates vibration motor

------------------------------------------------------------------------

## Power Optimization Strategy

The built-in IMU is used for motion detection:

-   Wrist raise detected → OLED turns ON
-   No movement timeout → OLED turns OFF

This reduces unnecessary display usage and improves battery life.

------------------------------------------------------------------------

## Mobile Application

A custom mobile application was developed to:

-   Scan and connect via BLE
-   Sync real-time clock
-   Forward call notifications
-   Forward WhatsApp notifications
-   Forward Instagram notifications
-   Manage device connection status

------------------------------------------------------------------------

## Working Principle

1.  Watch powers ON using 3.7V 400mAh Li-ion battery.
2.  BLE connection is established.
3.  Time is synchronized from smartphone.
4.  Notifications are received and displayed.
5.  Vibration motor activates for alerts.
6.  IMU controls display wake functionality.

------------------------------------------------------------------------

## Future Improvements

-   Battery level monitoring
-   Message preview scrolling
-   Sleep tracking using IMU data
-   Custom watch faces
-   OTA firmware updates

------------------------------------------------------------------------

## Author

Jerit Jose\
Embedded Systems & IoT Developer
