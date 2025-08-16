# Dexcom Monitor

This is a simple monitor to display the current glucose level and trend through an ESP32 and a 2.8" TFT LCD display.

## Known issues

-   Timestamp will not display correctly on certain timezones
-   Diagonal arrows don't look correct

## Features

![IMG_8577](https://github.com/user-attachments/assets/b6ac8dad-ef86-4e08-9859-ca909362449d)

-   Display the current glucose level in mg/dL and mmol/L
-   Display the change from the previous reading
-   Display the trend (up, down, flat)
-   Display the time and date

## Requirements

-   ESP32
-   2.8" TFT LCD display
-   Jumper wires (if the microcontroller and display are separated, recommended to have it as one unit).
-   Have an account on Dexcom Share

## Configuration

-   Dexcom Share Username and Password
-   WiFi SSID and Password

-   Cable Connection (if the units are separated)

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
3. Replace the `xxxx` in the code with your Dexcom Share Username and Password and WiFi SSID and Password
4. Upload the code to the ESP32
5. Open the Serial Monitor at 115200 baud to see the output

## Notes

-   The code is designed to run on an ESP32 microcontroller
-   The code is designed to display on an 2.8" TFT LCD display
-   If you experience any issues or have suggestions, please open an issue or submit a pull request or contact me at jmedinamulet@gmail.com

## Changelog

-   August 16, 2025

    -   Improve retry authentication logic

-   March 12, 2025

    -   Improve glucose difference calculation
    -   Change diagonal trend arrow, since the graphics library couldn't render the original one
    -   Added wifi reconnection logic
    -   Added session token request when it is expired
    -   Minor style fixes

-   Apr 07, 2025
    -   Added Links to non US Dexcom users
