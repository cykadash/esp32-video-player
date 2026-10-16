# ESP32-S3 SPI Display Video Player

This firmware plays a video on an ILI9341 TFT display. The firmware is in C and uses ESP-IDF. The display and an SD card use the same SPI bus. I tested the firmware in the [Wokwi](https://wokwi.com) simulator only.

**[Run it in Wokwi](https://wokwi.com/projects/478064516305716225)**

## Function

1. The firmware resets the display and sets its colour mode, orientation and display area.
2. The firmware reads a raw RGB565 video file from the SD card.
3. If the file is not on the card, the firmware makes a 30-frame test video.
4. The firmware sends the video to the display in small blocks through a DMA buffer.
5. The video plays again and again.

## Scope of my work

I wrote the display commands, the start sequence and the video loop. ESP-IDF supplies the SPI driver, the SD card file system and the FreeRTOS delays.

## Files

- `main.c`: the firmware.
- `diagram.json`: the Wokwi wiring diagram.
