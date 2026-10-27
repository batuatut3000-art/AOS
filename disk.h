
#ifndef AOS_DISK_H
#define AOS_DISK_H

#include <stdint.h>

#define DISK_SECTOR_SIZE 512
#define HISTORY_SIZE 32
#define HISTORY_LINE_SIZE 256

#define BACKUP_SECTORS 32U

int disk_init(void);

int disk_is_ready(void);
uint32_t disk_get_sector_count(void);

int disk_read_sector(uint32_t lba, uint8_t *buffer);
int disk_write_sector(uint32_t lba, const uint8_t *buffer);

int history_backup(
    const char history[HISTORY_SIZE][HISTORY_LINE_SIZE],
    uint32_t count
);

int history_restore(
    char history[HISTORY_SIZE][HISTORY_LINE_SIZE],
    uint32_t capacity,
    uint32_t *count
);

void fs_backup(void);
void fs_restore(void);

#endif
