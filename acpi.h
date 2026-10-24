#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289u

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;

    /* Розширені поля ACPI 2.0+ */
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} acpi_rsdp_t;

const acpi_rsdp_t *acpi_find_rsdp(uint32_t multiboot_info);

int acpi_shutdown(void);
int acpi_shutdown_with_rsdp(const acpi_rsdp_t *rsdp);

#endif