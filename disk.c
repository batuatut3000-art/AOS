
#include "disk.h"

#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA0       0x1F3
#define ATA_LBA1       0x1F4
#define ATA_LBA2       0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_ALTSTATUS  0x3F6

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_FLUSH    0xE7

#define ATA_BSY 0x80
#define ATA_DRQ 0x08
#define ATA_ERR 0x01
#define ATA_DF  0x20

#define ATA_TIMEOUT 1000000U
#define BACKUP_SECTORS 32U
#define BACKUP_MAGIC 0x414F5348U
#define BACKUP_VERSION 1U

#define BACKUP_BYTES (BACKUP_SECTORS * DISK_SECTOR_SIZE)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t checksum;
    char history[HISTORY_SIZE][HISTORY_LINE_SIZE];
} history_image_t;

static uint32_t disk_sector_count = 0;
static int disk_ready = 0;

static uint16_t identify_words[256];
static uint8_t backup_buffer[BACKUP_BYTES];

/* -------------------------------------------------- */
/* Hardware I/O                                       */
/* -------------------------------------------------- */

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;

    __asm__ volatile (
        "inw %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static void ata_delay(void)
{
    (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS);
}

/*
 * Wait for the ATA device.
 * Returns 1 on success, 0 on error or timeout.
 */
static int ata_wait(int require_drq)
{
    uint32_t timeout;

    for (timeout = 0; timeout < ATA_TIMEOUT; timeout++) {
        uint8_t status = inb(ATA_STATUS);

        if (status == 0x00 || status == 0xFF)
            return 0;

        if (status & ATA_BSY)
            continue;

        if (status & (ATA_ERR | ATA_DF))
            return 0;

        if (require_drq) {
            if (status & ATA_DRQ)
                return 1;
        } else {
            return 1;
        }
    }

    return 0;
}

/* -------------------------------------------------- */
/* Disk initialization                                */
/* -------------------------------------------------- */

int disk_init(void)
{
    uint32_t i;
    uint8_t status;

    disk_ready = 0;
    disk_sector_count = 0;

    /* Select primary master ATA device. */
    outb(ATA_DRIVE, 0xA0);
    ata_delay();

    /* Request IDENTIFY DEVICE. */
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    status = inb(ATA_STATUS);

    if (status == 0x00 || status == 0xFF)
        return 0;

    /*
     * Wait until BSY clears.
     * A missing device may never respond.
     */
    for (i = 0; i < ATA_TIMEOUT; i++) {
        status = inb(ATA_STATUS);

        if (status == 0x00 || status == 0xFF)
            return 0;

        if (!(status & ATA_BSY))
            break;
    }

    if (i == ATA_TIMEOUT)
        return 0;

    /*
     * For an ATA disk, LBA mid/high should be zero
     * after IDENTIFY. Nonzero values can indicate
     * a non-ATA device.
     */
    if (inb(ATA_LBA1) != 0 || inb(ATA_LBA2) != 0)
        return 0;

    if (!ata_wait(1))
        return 0;

    for (i = 0; i < 256; i++)
        identify_words[i] = inw(ATA_DATA);

    /* LBA28 capacity, in sectors. */
    disk_sector_count =
        ((uint32_t)identify_words[61] << 16) |
        (uint32_t)identify_words[60];

    if (disk_sector_count <= BACKUP_SECTORS)
        return 0;

    /* This driver supports LBA28 only. */
    if (disk_sector_count > 0x10000000U)
        disk_sector_count = 0x10000000U;

    disk_ready = 1;
    return 1;
}

int disk_is_ready(void)
{
    return disk_ready;
}

/* -------------------------------------------------- */
/* Read one sector                                    */
/* -------------------------------------------------- */

int disk_read_sector(uint32_t lba, uint8_t *buffer)
{
    uint32_t i;

    if (!disk_ready || !buffer)
        return 0;

    /*
     * IMPORTANT:
     * Reads are allowed in the reserved backup area.
     * history_restore() needs to read those sectors.
     */
    if (lba >= disk_sector_count || lba >= 0x10000000U)
        return 0;

    if (!ata_wait(0))
        return 0;

    outb(
        ATA_DRIVE,
        (uint8_t)(0xE0 | ((lba >> 24) & 0x0F))
    );

    ata_delay();

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, (uint8_t)lba);
    outb(ATA_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_LBA2, (uint8_t)(lba >> 16));

    outb(ATA_COMMAND, ATA_CMD_READ);

    if (!ata_wait(1))
        return 0;

    for (i = 0; i < DISK_SECTOR_SIZE / 2; i++) {
        uint16_t word = inw(ATA_DATA);

        buffer[i * 2] = (uint8_t)(word & 0xFF);
        buffer[i * 2 + 1] = (uint8_t)(word >> 8);
    }

    return 1;
}

/* -------------------------------------------------- */
/* Write one sector                                   */
/* Only reserved backup sectors may be written.       */
/* -------------------------------------------------- */

int disk_write_sector(uint32_t lba, const uint8_t *buffer)
{
    uint32_t i;
    uint32_t first_backup_sector;

    if (!disk_ready || !buffer)
        return 0;

    first_backup_sector = disk_sector_count - BACKUP_SECTORS;

    if (lba < first_backup_sector || lba >= disk_sector_count)
        return 0;

    if (lba >= 0x10000000U)
        return 0;

    if (!ata_wait(0))
        return 0;

    outb(
        ATA_DRIVE,
        (uint8_t)(0xE0 | ((lba >> 24) & 0x0F))
    );

    ata_delay();

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, (uint8_t)lba);
    outb(ATA_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_LBA2, (uint8_t)(lba >> 16));

    outb(ATA_COMMAND, ATA_CMD_WRITE);

    if (!ata_wait(1))
        return 0;

    for (i = 0; i < DISK_SECTOR_SIZE / 2; i++) {
        uint16_t word =
            (uint16_t)buffer[i * 2] |
            ((uint16_t)buffer[i * 2 + 1] << 8);

        outw(ATA_DATA, word);
    }

    /*
     * Wait for the device to finish writing.
     */
    if (!ata_wait(0))
        return 0;

    /* Flush the device's write cache. */
    outb(ATA_COMMAND, ATA_CMD_FLUSH);

    return ata_wait(0);
}

/* -------------------------------------------------- */
/* Checksum                                            */
/* -------------------------------------------------- */

static uint32_t checksum_bytes(const uint8_t *data, uint32_t size)
{
    uint32_t hash = 2166136261U;
    uint32_t i;

    for (i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619U;
    }

    return hash;
}

/* -------------------------------------------------- */
/* Save command history                                */
/* -------------------------------------------------- */

int history_backup(
    const char history[HISTORY_SIZE][HISTORY_LINE_SIZE],
    uint32_t count)
{
    history_image_t *image;
    uint32_t first_lba;
    uint32_t sectors;
    uint32_t i;

    if (!disk_ready || !history || count > HISTORY_SIZE)
        return 0;

    for (i = 0; i < BACKUP_BYTES; i++)
        backup_buffer[i] = 0;

    image = (history_image_t *)backup_buffer;

    image->magic = BACKUP_MAGIC;
    image->version = BACKUP_VERSION;
    image->count = count;
    image->checksum = 0;

    for (i = 0; i < count; i++) {
        uint32_t j;

        for (j = 0; j < HISTORY_LINE_SIZE - 1; j++) {
            image->history[i][j] = history[i][j];

            if (history[i][j] == '\0')
                break;
        }

        image->history[i][HISTORY_LINE_SIZE - 1] = '\0';
    }

    image->checksum = checksum_bytes(
        backup_buffer,
        (uint32_t)sizeof(history_image_t)
    );

    first_lba = disk_sector_count - BACKUP_SECTORS;

    sectors =
        ((uint32_t)sizeof(history_image_t) +
         DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE;

    for (i = 0; i < sectors; i++) {
        if (!disk_write_sector(
                first_lba + i,
                &backup_buffer[i * DISK_SECTOR_SIZE])) {
            return 0;
        }
    }

    return 1;
}

/* -------------------------------------------------- */
/* Restore command history                             */
/* -------------------------------------------------- */

int history_restore(
    char history[HISTORY_SIZE][HISTORY_LINE_SIZE],
    uint32_t capacity,
    uint32_t *count)
{
    history_image_t *image;
    uint32_t first_lba;
    uint32_t sectors;
    uint32_t i;
    uint32_t saved_checksum;

    if (!disk_ready || !history || !count)
        return 0;

    if (capacity == 0 || capacity > HISTORY_SIZE)
        return 0;

    for (i = 0; i < BACKUP_BYTES; i++)
        backup_buffer[i] = 0;

    first_lba = disk_sector_count - BACKUP_SECTORS;

    sectors =
        ((uint32_t)sizeof(history_image_t) +
         DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE;

    for (i = 0; i < sectors; i++) {
        if (!disk_read_sector(
                first_lba + i,
                &backup_buffer[i * DISK_SECTOR_SIZE])) {
            return 0;
        }
    }

    image = (history_image_t *)backup_buffer;

    if (image->magic != BACKUP_MAGIC)
        return 0;

    if (image->version != BACKUP_VERSION)
        return 0;

    if (image->count > capacity)
        return 0;

    saved_checksum = image->checksum;

    image->checksum = 0;

    if (checksum_bytes(
            backup_buffer,
            (uint32_t)sizeof(history_image_t)) != saved_checksum) {
        return 0;
    }

    for (i = 0; i < capacity; i++) {
        uint32_t j;

        for (j = 0; j < HISTORY_LINE_SIZE - 1; j++) {
            history[i][j] = image->history[i][j];

            if (image->history[i][j] == '\0')
                break;
        }

        history[i][HISTORY_LINE_SIZE - 1] = '\0';
    }

    *count = image->count;

    return 1;
}


uint32_t disk_get_sector_count(void)
{
    if (!disk_ready)
        return 0;

    return disk_sector_count;
}


