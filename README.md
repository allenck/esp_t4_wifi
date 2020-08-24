ESP_T4_Wifi
===========

Example PlatformIO program for the esp32 and a TTGO T4 display. Uses the [HAGL](https://github.com/tuupola/hagl) and [hagl_esp_mipi](https://github.com/tuupola/hagl_esp_mipi) libraries. 

Important!
----------

PlatformIO does not support the ESP_IDF menuconfig so there is no easy way to maintain program options. In order to provide a means of entering user options, in this case, *WiFi SSID & password, latitude and longitude, and your OpenWeather API key* (you can get yours at [OpenWeatherMap API guide](https://openweathermap.org/guide)), these options must be entered into a file: "defines.h". There is a copy of this file in the root directory.
1. Make the necessary changes to defines.h.
2. Copy the updated defines.h to "lib/hagl_esp_mpi/include.
3. Make a minor change to "/lib/hagl_esp_mipi/include/hagl_hal.h": after #include "sdkconfig.h", add #include "defines.h"
