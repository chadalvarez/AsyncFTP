# AsyncFTP Library

An asynchronous FTP server library for ESP32 devices, using AsyncTCP.  
Provides support for:

- Active (PORT) and Passive (PASV) modes
- File uploads/downloads
- Directory creation/deletion
- Renaming files/directories
- etc.

## Requirements

- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [AsyncTCP](https://github.com/ESP32Async/AsyncTCP)

## Installation

1. Copy this folder ("AsyncFTP") to your Arduino `libraries` folder.
2. Restart the Arduino IDE.
3. Search for the **AsyncFTP** examples.

## Usage

```cpp
#include <WiFi.h>
#include <AsyncFTP.h>

AsyncFTP ftpServer;

void setup() {
  // connect to WiFi, then
  ftpServer.begin("esp32", "esp32");
}

void loop() {
  // ...
}
