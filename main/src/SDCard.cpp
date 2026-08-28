#include "SDCard.hpp"
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "Config.hpp"
#include "images/CbmImage.hpp"
// #include "driver/sdmmc_default_configs.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
// #include "driver/spi_common.h"
#include "esp_err.h"
// #include "esp_intr_types.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "hal/ldo_types.h"
// #include "hal/spi_types.h"
#include "sd_protocol_types.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "soc/gpio_num.h"
#include "targets/tanmatsu/tanmatsu_hardware.h"


static const char* TAG = "SDCard";

SDCard::SDCard() : initialized(false) {
}

SDCard::~SDCard() {
    if (initialized) {
        sdspi_host_deinit();
    }
}

bool SDCard::init() {
    esp_err_t ret;
    if (initialized) {
        return true;
    }


#if defined(USE_SDCARD)

    // ESP_LOGI(TAG, "Initialize SDCard power");

    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = LDO_UNIT_4,  // SDCard powered by VO4
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
        return false;
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;

    vTaskDelay(500 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Setup sdio slot");

    slot_config.clk    = static_cast<gpio_num_t>(BSP_SDCARD_CLK);
    slot_config.cmd    = static_cast<gpio_num_t>(BSP_SDCARD_CMD);
    slot_config.d0     = static_cast<gpio_num_t>(BSP_SDCARD_D0);
    slot_config.d1     = static_cast<gpio_num_t>(BSP_SDCARD_D1);
    slot_config.d2     = static_cast<gpio_num_t>(BSP_SDCARD_D2);
    slot_config.d3     = static_cast<gpio_num_t>(BSP_SDCARD_D3);
    slot_config.width  = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting SDcard");
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed   = false,
        .max_files                = 5,
        .allocation_unit_size     = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat              = false,
    };

    static const char mount_point[] = SD_CARD_MOUNT_POINT;

    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &mount_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        return false;
    }

    // Get some info about the card
    sdmmc_card_print_info(stdout, mount_card);

    ESP_LOGI(TAG, "SDcard initialized");

    // Make sure the C64PRG directory exists if it doesn't already exist
    ESP_LOGI(TAG, "Checking if PRG directory exists");
    struct stat st;
    if (stat(SD_CARD_PRG_PATH, &st) != 0) {
        // directory does not exist, create it
        if(mkdir(SD_CARD_PRG_PATH, 0775) != 0) {
            ESP_LOGE(TAG, "Failed to create directory %s", SD_CARD_PRG_PATH);
            return false;
        }
        ESP_LOGE(TAG, "PRG directory has been created: %s", SD_CARD_PRG_PATH);
    } else if (!S_ISDIR(st.st_mode)) {
        ESP_LOGE(TAG, "%s is not a directory", SD_CARD_PRG_PATH);
        return false;
    } else {
        ESP_LOGI(TAG, "Found prg directory: %s" , SD_CARD_PRG_PATH);
    }

    initialized = true;
    return true;
#else
    return false;
#endif
}

void getPath(char* path, uint8_t* ram) {
    uint8_t  cury      = ram[0xd6];
    uint8_t  curx      = ram[0xd3];
    uint8_t* cursorpos = ram + 0x0400 + cury * 40 + curx;
    cursorpos--;  // char may be 160
    while (*cursorpos == 32) {
        cursorpos--;
    }
    while ((*cursorpos != 32) && (cursorpos >= ram + 0x0400)) {
        cursorpos--;
    }
    cursorpos++;
    path[0]   = '/';
    uint8_t i = 1;
    uint8_t p;
    while (((p = *cursorpos++) != 32) && (p != 160) && (i < 17)) {
        if ((p >= 1) && (p <= 26)) {
            path[i] = p + 96;
        } else if ((p >= 33) && (p <= 63)) {
            path[i] = p;
        }
        i++;
    }
    path[i++] = '.';
    path[i++] = 'p';
    path[i++] = 'r';
    path[i++] = 'g';
    path[i]   = '\0';
}

std::string SDCard::fullPath(const char* filename) {
    std::string path = SD_CARD_PRG_PATH;
    if (filename == nullptr || filename[0] == '\0') return path;
    if (filename[0] != '/') path += '/';
    path += filename;
    return path;
}

// Reads a .prg into RAM: two byte load address followed by the data. Returns
// the address one past the last byte written, or 0 if nothing was loaded.
static uint16_t loadPrgFile(const char* full_path, uint8_t* ram) {
    int fd = open(full_path, O_RDONLY);
    if (fd < 0) return 0;

    uint8_t hdr[2];
    if (read(fd, hdr, 2) != 2) {
        close(fd);
        return 0;
    }
    uint16_t addr = hdr[0] | (hdr[1] << 8);

    // A program longer than the space above its load address would otherwise
    // run straight off the end of the 64K RAM buffer, so stop at $FFFF.
    size_t pos  = addr;
    size_t room = (C64_RAM_SIZE - 1) - addr;
    while (room > 0) {
        ssize_t got = read(fd, ram + pos, room);
        if (got <= 0) break;
        pos  += static_cast<size_t>(got);
        room -= static_cast<size_t>(got);
    }
    close(fd);

    if (pos == addr) return 0;
    return static_cast<uint16_t>(pos);
}

uint16_t SDCard::load(const char* path, uint8_t* ram, size_t len) {
    (void)len;
    return loadPrgFile(fullPath(path).c_str(), ram);
}

uint16_t SDCard::load_auto(const char* path, uint8_t* ram, size_t len) {
    (void)path;
    (void)len;
    char file_path[64] = {0};
    if (!initialized) return 0;
    // The file name comes from the BASIC LOAD command still on screen.
    getPath(file_path, ram);
    ESP_LOGI(TAG, "load file %s", file_path);

    return loadPrgFile(fullPath(file_path).c_str(), ram);
}

bool SDCard::save(const char* path, const uint8_t* ram, size_t len) {
    (void)path;
    (void)len;
    if (!initialized) return false;

    // getPath() writes the name it scrapes off the screen into the buffer it
    // is given, so it needs somewhere writable of its own.
    char file_path[64] = {0};
    getPath(file_path, const_cast<uint8_t*>(ram));

    uint16_t startaddr = ram[43] + ram[44] * 256;
    uint16_t endaddr   = ram[45] + ram[46] * 256;
    if (endaddr <= startaddr) {
        ESP_LOGI(TAG, "nothing to save, start $%04x end $%04x", startaddr, endaddr);
        return false;
    }

    std::string full_path = fullPath(file_path);
    ESP_LOGI(TAG, "save file %s", full_path.c_str());

    int fd = open(full_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;

    write(fd, &ram[43], 2);
    write(fd, &ram[startaddr], endaddr - startaddr);
    close(fd);
    return true;
}

std::vector<std::string> SDCard::listPagedEntries(const char* path, size_t page, size_t pageSize) {
    std::vector<std::string> result;

    if (!initialized) return result;

    ESP_LOGI(TAG, "list paged entries %s, page %zu, pageSize %zu", path, page, pageSize);

    DIR* dir = opendir(path);
    if (!dir) {
        ESP_LOGI(TAG, "cannot open %s", path);
        return result;
    }

    // Only loadable files count towards a page. Letting every directory entry
    // count would make the page boundaries depend on whatever else happens to
    // be on the card.
    size_t         startOffset = page * pageSize;
    size_t         matched     = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (imageFormatFromName(name) == ImageFormat::UNKNOWN) continue;

        if (matched >= startOffset) {
            result.push_back(name);
            if (result.size() >= pageSize) break;
        }
        matched++;
    }

    closedir(dir);
    return result;
}

bool SDCard::listNextEntry(uint8_t* nextentry, size_t entrySize, bool start) {
    static DIR*           dir = nullptr;
    static struct dirent* ent;

    if (!initialized) return false;

    if (start) {
        if (dir) {
            closedir(dir);
            dir = nullptr;
        }

        dir = opendir(SD_CARD_PRG_PATH);
        if (!dir) {
            ESP_LOGI(TAG, "cannot open root dir");
            return false;
        }
    }

    while ((ent = readdir(dir)) != nullptr) {
        const char* name = ent->d_name;
        size_t      len  = strlen(name);
        ESP_LOGI(TAG, "found file: %s", name);
        if (len > 4 && strcmp(name + len - 4, ".prg") == 0) {
            char   fname[17] = {};
            size_t copy_len  = len - 4;
            if (copy_len >= sizeof(fname)) copy_len = sizeof(fname) - 1;
            memcpy(nextentry, name, copy_len);
            nextentry[copy_len] = '\0';
            nextentry[16]       = '\0';
            return true;
        }
    }

    if (dir) {
        closedir(dir);
        dir = nullptr;
    }
    nextentry[0] = '\0';
    return true;
}
