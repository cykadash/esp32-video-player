#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define PIN_NUM_MISO   13
#define PIN_NUM_MOSI   11
#define PIN_NUM_CLK    12
#define PIN_NUM_CS     10   // display chip select
#define PIN_NUM_DC     8
#define PIN_NUM_RST    9
#define PIN_NUM_SD_CS  4    // sd card chip select

#define LCD_WIDTH   320
#define LCD_HEIGHT  240
#define CHUNK_ROWS  40
#define FRAME_BYTES (LCD_WIDTH * LCD_HEIGHT * 2)   // rgb565
#define CHUNK_BYTES (LCD_WIDTH * CHUNK_ROWS * 2)   // 40 rows per transfer

// helpers
void lcd_send_cmd(spi_device_handle_t spi, const uint8_t cmd) {
    gpio_set_level(PIN_NUM_DC, 0);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;                     // 8 bits
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(spi, &t);
}

void lcd_send_data(spi_device_handle_t spi, const uint8_t *data, int len) {
    if (len == 0) return;
    gpio_set_level(PIN_NUM_DC, 1);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;               // length in bits
    t.tx_buffer = data;
    spi_device_polling_transmit(spi, &t);
}

void app_main(void) {

  // set dc, rst and display cs pins to output
  gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);

  // hardware reset
  gpio_set_level(PIN_NUM_RST, 0);
  vTaskDelay(100 / portTICK_PERIOD_MS);
  gpio_set_level(PIN_NUM_RST, 1);
  vTaskDelay(100 / portTICK_PERIOD_MS);

  // init the spi bus
  spi_bus_config_t buscfg = {
      .miso_io_num = PIN_NUM_MISO,
      .mosi_io_num = PIN_NUM_MOSI,
      .sclk_io_num = PIN_NUM_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = CHUNK_BYTES + 8
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

  // sd card
  printf("mounting sd on shared spi bus...\n");
  gpio_set_level(PIN_NUM_CS, 1); 
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = true,
    .max_files = 5,
    .allocation_unit_size = 16*1024
  };

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = PIN_NUM_SD_CS;
  slot_config.host_id = host.slot;

  // mounting
  sdmmc_card_t *card;
  esp_err_t ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);

  if (ret != ESP_OK) {
    printf("Failed to mount SD: %s\n", esp_err_to_name(ret));
    return;
  }
  printf("SD mounted successfully.\n");

  uint8_t *chunk_buffer = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA);

  if (chunk_buffer == NULL) {
    printf("FATAL: Failed to allocate DMA buffer\n");
    return;
  }

  // test video
  printf("Checking for test video...\n");
  FILE* f = fopen("/sdcard/test.raw", "rb");

  if (f == NULL) {
    printf("Test video not found. Creating test video now...\n");
    f = fopen("/sdcard/test.raw", "wb");
    if (f == NULL) {
      printf("FATAL: Failed to open file for writing.\n");
      heap_caps_free(chunk_buffer);
      return;
    }

    uint16_t *pixels = (uint16_t *)chunk_buffer;
    for (int frame = 0; frame < 30; frame++) {
      printf("Generating frame %d...\n", frame);
      for (int chunk_y = 0; chunk_y < LCD_HEIGHT; chunk_y += CHUNK_ROWS) {
        for (int row = 0; row < CHUNK_ROWS; row++) {
          int y = chunk_y + row;
          for (int x = 0; x < LCD_WIDTH; x++) {
            uint8_t r = ((x + (frame * 5)) * 31 / LCD_WIDTH) & 0x1F;
            uint8_t g = ((y + (frame * 2)) * 63 / LCD_HEIGHT) & 0x3F;
            uint8_t b = 0x1F;

            uint16_t color = (r << 11) | (g << 5) | b;
            pixels[row * LCD_WIDTH + x] = (color >> 8) | (color << 8);
          }
        }
        fwrite(chunk_buffer, 1, CHUNK_BYTES, f);
      }
    }
    fclose(f);
    printf("Test video created.\n");
  } else {
    printf("Test video already exists.\n");
    fclose(f);
  }

  // attach display
  spi_device_interface_config_t devcfg = {
    .clock_speed_hz = 40 * 1000 * 1000,
    .mode = 0,
    .spics_io_num = PIN_NUM_CS,
    .queue_size = 7,
  };
  spi_device_handle_t spi;
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi));

  // software reset
  lcd_send_cmd(spi, 0x01);
  vTaskDelay(150 / portTICK_PERIOD_MS);

  lcd_send_cmd(spi, 0x11); // wake
  vTaskDelay(255 / portTICK_PERIOD_MS);

  // colour mode 16-bit colour
  lcd_send_cmd(spi, 0x3A);
  const uint8_t colmod = 0x55;
  lcd_send_data(spi, &colmod, 1);

  // landscape orientation 
  lcd_send_cmd(spi, 0x36);
  const uint8_t madctl = 0x28;
  lcd_send_data(spi, &madctl, 1);

  // turn on display
  lcd_send_cmd(spi, 0x29);
  vTaskDelay(100 / portTICK_PERIOD_MS);

  // area of frame memory
  lcd_send_cmd(spi, 0x2A);
  uint8_t x_data[] = {0x00, 0x00, 0x01, 0x3F}; // col 0-319
  lcd_send_data(spi, x_data, 4);
  lcd_send_cmd(spi, 0x2B);
  uint8_t y_data[] = {0x00, 0x00, 0x00, 0xEF}; // row 0-239
  lcd_send_data(spi, y_data, 4);

  printf("Starting Video Playback...\n");

  while (1) {
    FILE* video = fopen("/sdcard/test.raw", "rb");
    if (video == NULL) {
      printf("Failed to open video!\n");
      break;
    }

    bool end_of_file = false;
    while (!end_of_file) {
      lcd_send_cmd(spi, 0x2C);

      for (int offset = 0; offset < FRAME_BYTES; offset += CHUNK_BYTES) {
        size_t bytes_read = fread(chunk_buffer, 1, CHUNK_BYTES, video);

        if (bytes_read < CHUNK_BYTES) {
          end_of_file = true;
          break;
        }

        lcd_send_data(spi, chunk_buffer, CHUNK_BYTES);
      }
    }

    fclose(video);
  }

  heap_caps_free(chunk_buffer);
}
