// sd.c
#include "sd.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "board.h"

static sdmmc_card_t *card;

bool sd_mount(void)
{
    if (card) return true;
    esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    if (BOARD_IS_ROUND()) {   // AMOLED 1.75
        slot.clk = 2;
        slot.cmd = 1;
        slot.d0 = 3;
    } else {                  // 3.49
        slot.clk = 41;
        slot.cmd = 39;
        slot.d0 = 40;
    }
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    if (esp_vfs_fat_sdmmc_mount(SD_MOUNT, &host, &slot, &mc, &card) != ESP_OK) {
        card = NULL;
        ESP_LOGW("sd", "microSD non montata (assente o non FAT32)");
        return false;
    }
    return true;
}

bool sd_ok(void) { return card != NULL; }
float sd_size_gb(void) { return card ? (float)card->csd.capacity * card->csd.sector_size / 1e9f : 0; }
