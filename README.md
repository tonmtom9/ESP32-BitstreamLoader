# ESP32-BitstreamLoader
Heavily modified version of Pillar1989's [spartan-edge-esp32-boot](https://github.com/Pillar1989/spartan-edge-esp32-boot) code and ported to latest version of [ESP-IDF](https://github.com/espressif/esp-idf).

Managed by [PlatformIO](https://platformio.org/)

# Usage
Once uploaded. The ESP32 will try to read bitstream file (default.bit) from SD card (using SDMMC) and will program FPGA using defined pins.

