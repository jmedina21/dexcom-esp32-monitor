# Dexcom Monitor

This is a simple monitor to display the current glucose level and trend through an ESP32 and a 2.8" TFT LCD display.

## Features

As of December 2025
![IMG_1012](https://github.com/user-attachments/assets/913a7c67-1cd0-44fd-90eb-141ca4048f58)

Previous version
![IMG_8577](https://github.com/user-attachments/assets/b6ac8dad-ef86-4e08-9859-ca909362449d)

- Display the current glucose level in mg/dL and mmol/L
- Display the change from the previous reading
- Display the trend (up, down, flat)
- Display the time and date
- Display a graph with the last 6h of readings

## Requirements

- ESP32
- 2.8" TFT LCD display
- Jumper wires (if the microcontroller and display are separated, recommended to have it as one unit).
- Have an account on Dexcom Share

### Arduino Libraries Required

Install the following libraries via Arduino IDE Library Manager:

- **WiFiManager** by tzapu (https://github.com/tzapu/WiFiManager)
- Adafruit GFX Library
- Adafruit ILI9341
- ArduinoJson

- ### Optional
    - [3d Model I used to house the device](https://makerworld.com/en/models/1012388-housing-display-tft-ili9341-esp32-esp8266#profileId-991967) designed by [@maker.bamboo](https://makerworld.com/en/@maker.bamboo)

## Configuration

No need to modify the code! WiFi and Dexcom credentials are configured via a web portal.

- Cable Connection (if the units are separated)

| ILI9341 Pin | ESP32 Pin    | Function             |
| ----------- | ------------ | -------------------- |
| VCC         | 3.3V         | Power                |
| GND         | GND          | Ground               |
| SCK         | GPIO 23      | SPI Clock (SCK)      |
| SDI (MOSI)  | GPIO 18      | SPI Data Out (MOSI)  |
| CS          | GPIO 5       | Chip Select (CS)     |
| D/C         | GPIO 2       | Data/Command (D/C)   |
| RESET       | GPIO 4       | Reset (RST)          |
| LED         | 3.3V (or 5V) | Backlight (optional) |

## Setup

1. Connect the ESP32 to the TFT LCD display
2. Connect the ESP32 to your computer via USB
3. Install the required Arduino libraries (see Requirements above)
4. Upload the code to the ESP32
5. On first boot, the device will create a WiFi network called **"DexcomMonitor-Setup"**
6. Connect to this network with your phone or computer
7. A configuration page will open automatically (or navigate to http://192.168.4.1)
8. Enter your WiFi credentials and Dexcom Share username/password
9. Click Save - the device will restart and connect to your WiFi

### Resetting Configuration

To reset the WiFi and Dexcom credentials:

- Hold the **BOOT button** (GPIO 0) while the device is starting up
- The device will clear saved credentials and re-enter setup mode

## Notes

- I haven't personally tested the non US regions, due to not having accounts in these regions, if you have, feel free to add to the Readme, or open a PR with the fix
- The code is designed to run on an ESP32 microcontroller
- The code is designed to display on an 2.8" TFT LCD display
- If you experience any issues or have suggestions, please open an issue or submit a pull request or contact me at jmedinamulet@gmail.com

## Changelog

- July 8, 2026
    - Support regions selection on wifi config

- December 28, 2025
    - Added WiFiManager for easy WiFi and Dexcom credential configuration
    - No longer need to modify code to set credentials
    - Credentials stored securely in ESP32 flash memory
    - Hold BOOT button during startup to reset configuration

- December 27, 2025
    - Implemented dynamic polling of Dexcom Readings
    - Fix Diagonal arrows
    - Added graph with 6 hours of recording

- August 16, 2025
    - Improve retry authentication logic

- March 12, 2025
    - Improve glucose difference calculation
    - Change diagonal trend arrow, since the graphics library couldn't render the original one
    - Added wifi reconnection logic
    - Added session token request when it is expired
    - Minor style fixes

- Apr 07, 2025
    - Added Links to non US Dexcom users
