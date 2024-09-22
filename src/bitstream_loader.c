/* SD card and FAT filesystem example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// This example uses SDMMC peripheral to communicate with SD card.

#include <string.h>
#include <sys/unistd.h>
#include <sys/stdio.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include "driver/gpio.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "soc/gpio_reg.h"

static const char *TAG = "BitstreamLoader";

#define FPGA_CCLK_PIN GPIO_NUM_17
#define FPGA_DIN_PIN GPIO_NUM_27
#define FPGA_PROGRAM_PIN GPIO_NUM_25
#define FPGA_INTB_PIN GPIO_NUM_26
#define FPGA_DONE_PIN GPIO_NUM_34

#define MAX_CHAR_SIZE   1024
#define MOUNT_POINT "/sdcard"
#define FILE_PATH "/default.bit"


static esp_err_t config_fpga_pins(void)
{
    gpio_reset_pin(FPGA_CCLK_PIN);
    gpio_set_direction(FPGA_CCLK_PIN, GPIO_MODE_OUTPUT);

    gpio_reset_pin(FPGA_DIN_PIN);
    gpio_set_direction(FPGA_DIN_PIN, GPIO_MODE_OUTPUT);
    
    gpio_reset_pin(FPGA_PROGRAM_PIN);
    gpio_set_direction(FPGA_PROGRAM_PIN, GPIO_MODE_OUTPUT);

    gpio_reset_pin(FPGA_INTB_PIN);
    gpio_set_direction(FPGA_INTB_PIN, GPIO_MODE_INPUT);

    gpio_reset_pin(FPGA_DONE_PIN);
    gpio_set_direction(FPGA_DONE_PIN, GPIO_MODE_INPUT);

     
    ESP_LOGI(TAG, "Configured FPGA pins!");
    return ESP_OK;
}

static esp_err_t load_bitstream(const char *path)
{
    char byte_buff[MAX_CHAR_SIZE];
    int byte_len = 0;
    unsigned byte;
    int i = 0;

    int fdes = open(path, O_RDONLY); //Try to open file
    if(fdes == -1)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }

    gpio_set_level(FPGA_PROGRAM_PIN, 1); //Prepare FPGA for bitstream loading
    while (gpio_get_level(FPGA_INTB_PIN) == 0) {}

    /*
    * loading the bitstream
    * If you want to know the details,you can Refer to the following documentation
    * https://www.xilinx.com/support/documentation/user_guides/ug470_7Series_Config.pdf
    */

    byte_len = read(fdes, byte_buff, MAX_CHAR_SIZE);

    // find the raw bits
    if(byte_buff[0] != 0xff)
    {
        // skip header
        i = ((byte_buff[0]<<8) | byte_buff[1]) + 4;

        // find the 'e' record
        while(byte_buff[i] != 0x65)
        {
            // skip the record
            i += (byte_buff[i+1]<<8 | byte_buff[i+2]) + 3;
            // exit if the next record isn't within the buffer
            if(i>= byte_len)
                return -1;
        }
        // skip the field name and bitstrem length
        i += 5;
    } // else it's already a raw bin file

    while (byte_len > 0) {
        for ( ;i < byte_len;i++) {
        byte = byte_buff[i];

            for(int j = 0;j < 8;j++) {
                REG_WRITE(GPIO_OUT_W1TC_REG, (1<<FPGA_CCLK_PIN));
                REG_WRITE((byte&0x80)?GPIO_OUT_W1TS_REG:GPIO_OUT_W1TC_REG, (1<<FPGA_DIN_PIN));
                byte = byte << 1;
                REG_WRITE(GPIO_OUT_W1TS_REG, (1<<FPGA_CCLK_PIN));
            }
        }
        byte_len = read(fdes, byte_buff, MAX_CHAR_SIZE);
        i = 0;
    }
    gpio_set_level(FPGA_CCLK_PIN, 0);
    
    close(fdes);

    ESP_LOGI(TAG, "Bitstream loaded!");
    return ESP_OK;
}



void app_main(void)
{
    esp_err_t ret;

    // Options for mounting the filesystem.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 1,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 40MHz for SDMMC)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // Set bus width to use:
    slot_config.width = 4;

    // Enable internal pullups on enabled pins. The internal pullups
    // are insufficient however, please make sure 10k external pullups are
    // connected on the bus. This is for debug / example purpose only.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;


    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    // Configure GPIO pins
    config_fpga_pins();

    const char *file = MOUNT_POINT FILE_PATH;
    //Program FPGA
    ret = load_bitstream(file);
    if (ret != ESP_OK) {
        return;
    }

    // All done, unmount partition and disable SDMMC peripheral
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG, "Card unmounted");
}
