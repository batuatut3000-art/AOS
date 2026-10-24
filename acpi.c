#include "acpi.h"
#include <stdint.h>
#include <stddef.h>

#define ACPI_SLP_TYP_MASK (7u << 10)
#define ACPI_SLP_EN       (1u << 13)
#define ACPI_HW_REDUCED   (1u << 20)

typedef struct __attribute__((packed)) {
    uint32_t total_size;
    uint32_t reserved;
} multiboot2_info_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t size;
} multiboot2_tag_t;

typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

static int bytes_equal(const char *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

static int checksum_valid(const uint8_t *data, uint32_t length)
{
    uint8_t sum = 0;

    for (uint32_t i = 0; i < length; i++)
        sum = (uint8_t)(sum + data[i]);

    return sum == 0;
}

static int rsdp_signature_valid(const acpi_rsdp_t *rsdp)
{
    static const char signature[8] = "RSD PTR ";
    return bytes_equal(rsdp->signature, signature, 8);
}

const acpi_rsdp_t *acpi_find_rsdp(uint32_t multiboot_info)
{
    if (multiboot_info == 0)
        return 0;

    const multiboot2_info_t *info =
        (const multiboot2_info_t *)(uintptr_t)multiboot_info;

    if (info->total_size < 16 || info->total_size > 0x1000000u)
        return 0;

    const uint8_t *base = (const uint8_t *)info;
    uint32_t offset = 8;

    while (offset <= info->total_size - 8) {
        const multiboot2_tag_t *tag =
            (const multiboot2_tag_t *)(base + offset);

        if (tag->size < 8 ||
            tag->size > info->total_size - offset)
            return 0;

        if (tag->type == 0)
            break;

        if (tag->type == 14 || tag->type == 15) {
            uint32_t data_size = tag->size - 8;

            if (data_size >= 20) {
                const acpi_rsdp_t *rsdp =
                    (const acpi_rsdp_t *)(base + offset + 8);

                if (rsdp_signature_valid(rsdp) &&
                    checksum_valid((const uint8_t *)rsdp, 20)) {

                    if (tag->type == 15) {
                        if (data_size < 36)
                            goto next_tag;

                        uint32_t length =
                            *(const uint32_t *)
                            (base + offset + 8 + 20);

                        if (length < 36 || length > data_size)
                            goto next_tag;

                        if (!checksum_valid(
                                (const uint8_t *)rsdp, length))
                            goto next_tag;
                    }

                    return rsdp;
                }
            }
        }

next_tag:
        offset = (offset + tag->size + 7u) & ~7u;
    }

    return 0;
}

static uint32_t read_u32(const uint8_t *data, uint32_t offset)
{
    uint32_t value = 0;

    for (uint32_t i = 0; i < 4; i++)
        value |= (uint32_t)data[offset + i] << (i * 8);

    return value;
}

static const acpi_sdt_header_t *get_table(
    uint64_t address,
    const char signature[4])
{
    if (address == 0 || address > 0xFFFFFFFFu)
        return 0;

    const acpi_sdt_header_t *table =
        (const acpi_sdt_header_t *)(uintptr_t)(uint32_t)address;

    if (!bytes_equal(table->signature, signature, 4))
        return 0;

    if (table->length < sizeof(acpi_sdt_header_t))
        return 0;

    if (!checksum_valid((const uint8_t *)table, table->length))
        return 0;

    return table;
}

static const acpi_sdt_header_t *find_fadt(
    const acpi_rsdp_t *rsdp)
{
    const acpi_sdt_header_t *root = 0;
    int use_xsdt = 0;

    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
        root = get_table(rsdp->xsdt_address, "XSDT");

        if (root)
            use_xsdt = 1;
    }

    if (!root) {
        root = get_table(rsdp->rsdt_address, "RSDT");
        use_xsdt = 0;
    }

    if (!root)
        return 0;

    uint32_t entry_size = use_xsdt ? 8u : 4u;
    uint32_t header_size = sizeof(acpi_sdt_header_t);
    uint32_t entries_size = root->length - header_size;

    if (entries_size % entry_size != 0)
        return 0;

    const uint8_t *entries = (const uint8_t *)root + header_size;
    uint32_t count = entries_size / entry_size;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t address = 0;

        if (use_xsdt) {
            for (uint32_t b = 0; b < 8; b++)
                address |= (uint64_t)entries[i * 8 + b] << (b * 8);
        } else {
            address = read_u32(entries, i * 4);
        }

        const acpi_sdt_header_t *table =
            get_table(address, "FACP");

        if (table)
            return table;
    }

    return 0;
}

/* Читає AML PackageLength. */
static int aml_package_length(
    const uint8_t *aml,
    uint32_t end,
    uint32_t *pos,
    uint32_t *length)
{
    if (*pos >= end)
        return 0;

    uint8_t lead = aml[(*pos)++];
    uint32_t follow = lead >> 6;
    uint32_t result;

    if (follow == 0) {
        result = lead & 0x3Fu;
    } else {
        result = lead & 0x0Fu;

        if (follow > end - *pos)
            return 0;

        for (uint32_t i = 0; i < follow; i++)
            result |= (uint32_t)aml[(*pos)++] << (4u + 8u * i);
    }

    *length = result;
    return 1;
}

/* Читає AML integer: Zero, One, Byte, Word, DWord або QWord. */
static int aml_integer(
    const uint8_t *aml,
    uint32_t end,
    uint32_t *pos,
    uint64_t *value)
{
    if (*pos >= end)
        return 0;

    uint8_t op = aml[(*pos)++];

    if (op == 0x00) {
        *value = 0;
        return 1;
    }

    if (op == 0x01) {
        *value = 1;
        return 1;
    }

    uint32_t bytes;

    switch (op) {
        case 0x0A: bytes = 1; break;
        case 0x0B: bytes = 2; break;
        case 0x0C: bytes = 4; break;
        case 0x0E: bytes = 8; break;
        default: return 0;
    }

    if (bytes > end - *pos)
        return 0;

    uint64_t result = 0;

    for (uint32_t i = 0; i < bytes; i++)
        result |= (uint64_t)aml[(*pos)++] << (i * 8);

    *value = result;
    return 1;
}

/*
 * Знаходить поширений варіант:
 * Name (_S5_, Package (...) { sleep_type_a, sleep_type_b })
 *
 * Це спрощений AML-парсер, а не повна реалізація AML.
 */
static int find_s5(
    const acpi_sdt_header_t *dsdt,
    uint32_t *type_a,
    uint32_t *type_b)
{
    const uint8_t *aml = (const uint8_t *)dsdt;
    uint32_t length = dsdt->length;
    uint32_t header_size = sizeof(acpi_sdt_header_t);

    if (length <= header_size)
        return 0;

    for (uint32_t i = header_size; i + 6 < length; i++) {
        if (aml[i] != 0x08)
            continue;

        if (aml[i + 1] != '_' ||
            aml[i + 2] != 'S' ||
            aml[i + 3] != '5' ||
            aml[i + 4] != '_')
            continue;

        uint32_t pos = i + 5;

        if (pos >= length || aml[pos++] != 0x12)
            continue;

        uint32_t length_pos = pos;
        uint32_t package_length;

        if (!aml_package_length(
                aml, length, &pos, &package_length))
            continue;

        if (package_length < pos - length_pos ||
            package_length > length - length_pos)
            continue;

        uint32_t package_end = length_pos + package_length;

        if (pos >= package_end)
            continue;

        uint8_t elements = aml[pos++];

        if (elements < 2)
            continue;

        uint64_t a, b;

        if (!aml_integer(aml, package_end, &pos, &a))
            continue;

        if (!aml_integer(aml, package_end, &pos, &b))
            continue;

        if (a > 7 || b > 7)
            continue;

        *type_a = (uint32_t)a;
        *type_b = (uint32_t)b;

        return 1;
    }

    return 0;
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

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

int acpi_shutdown_with_rsdp(const acpi_rsdp_t *rsdp)
{
    if (!rsdp)
        return 0;

    const acpi_sdt_header_t *fadt = find_fadt(rsdp);

    if (!fadt || fadt->length < 90)
        return 0;

    const uint8_t *f = (const uint8_t *)fadt;

    uint32_t pm1a = read_u32(f, 64);
    uint32_t pm1b = read_u32(f, 68);
    uint8_t pm1_len = f[89];

    if (fadt->length >= 116 &&
        (read_u32(f, 112) & ACPI_HW_REDUCED))
        return 0;

    if (pm1a == 0 || pm1a > 0xFFFFu || pm1_len < 2)
        return 0;

    uint64_t dsdt_addr = read_u32(f, 40);

    if (fadt->length >= 148) {
        uint64_t xdsdt = 0;

        for (uint32_t i = 0; i < 8; i++)
            xdsdt |= (uint64_t)f[140 + i] << (i * 8);

        if (xdsdt != 0)
            dsdt_addr = xdsdt;
    }

    const acpi_sdt_header_t *dsdt =
        get_table(dsdt_addr, "DSDT");

    if (!dsdt)
        return 0;

    uint32_t s5a, s5b;

    if (!find_s5(dsdt, &s5a, &s5b))
        return 0;

    uint16_t value_a = inw((uint16_t)pm1a);

    value_a = (uint16_t)(
        (value_a & ~ACPI_SLP_TYP_MASK) |
        ((s5a & 7u) << 10) |
        ACPI_SLP_EN
    );

    uint16_t value_b = 0;

    if (pm1b != 0 && pm1b <= 0xFFFFu) {
        value_b = inw((uint16_t)pm1b);

        value_b = (uint16_t)(
            (value_b & ~ACPI_SLP_TYP_MASK) |
            ((s5b & 7u) << 10) |
            ACPI_SLP_EN
        );
    }

    __asm__ volatile ("cli" ::: "memory");

    outw((uint16_t)pm1a, value_a);

    if (pm1b != 0 && pm1b <= 0xFFFFu)
        outw((uint16_t)pm1b, value_b);

    return 1;
}

int acpi_shutdown(void)
{
    return 0;
}