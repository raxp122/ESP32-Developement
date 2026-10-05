// doom_boot.c — ricerca dei file di gioco e passaggio launcher → Doom
#include "doom_app.h"
#include "sd.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_system.h"
#include "esp_attr.h"

#define BOOT_DOOM_MAGIC 0xD00D1E55u
static RTC_NOINIT_ATTR uint32_t boot_magic; // sopravvive al riavvio software

bool doom_find_wad(char *path, size_t n)
{
    static const char *names[] = {
        "doom1.wad", "doom.wad", "doom2.wad", "plutonia.wad", "tnt.wad", "freedoom1.wad", "freedoom2.wad",
        "DOOM1.WAD", "DOOM.WAD", "DOOM2.WAD",
    };
    static const char *dirs[] = {SD_MOUNT "/doom", SD_MOUNT};
    if (!sd_ok()) return false;
    for (int d = 0; d < 2; d++)
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            snprintf(path, n, "%s/%s", dirs[d], names[i]);
            if (access(path, R_OK) == 0) return true;
        }
    return false;
}

void doom_launch(void)
{
    boot_magic = BOOT_DOOM_MAGIC;
    esp_restart();
}

bool doom_boot_requested(void)
{
    bool r = boot_magic == BOOT_DOOM_MAGIC;
    boot_magic = 0;
    return r;
}
