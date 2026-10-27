#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile unsigned short*)0xB8000)
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT_FRAMEBUFFER_INFO (1 << 12)
#define CELL_WIDTH 12
#define CELL_HEIGHT 16
#include "acpi.h"
#define COLOR_WHITE 0x0F
#define COLOR_GREEN 0x02
#define COLOR_LIGHT_BLUE 0x0B
#define COLOR_LIGHT_RED 0x0C
#define COLOR_YELLOW 0x0E
#define KEY_UP   ((char)0x80)
#define KEY_DOWN ((char)0x81)
#define FS_MAX_ENTRIES 64
#define FS_NAME_SIZE   32
#define FS_PATH_SIZE   64
#include "disk.h"
#define HISTORY_SIZE 32
#define HISTORY_LINE_SIZE 256

#define FS_BACKUP_MAGIC   0x414F5346U
#define FS_BACKUP_VERSION 1U
#define FS_BACKUP_OFFSET  17U

typedef struct {
    char name[FS_NAME_SIZE];
    char parent[FS_PATH_SIZE];
    int is_dir;
    int used;
} fs_entry_t;

static fs_entry_t fs_entries[FS_MAX_ENTRIES];
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t checksum;
    fs_entry_t entries[FS_MAX_ENTRIES];
} fs_backup_image_t;
static uint8_t fs_backup_buffer[BACKUP_SECTORS * DISK_SECTOR_SIZE];

static uint32_t fs_checksum(const uint8_t *data, uint32_t size)
{
    uint32_t hash = 2166136261U;

    for (uint32_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619U;
    }

    return hash;
}

void fs_backup(void)
{
    if (!disk_is_ready()) {
        terminal_print_color(
            "Filesystem backup failed: disk unavailable.\n",
            COLOR_LIGHT_RED
        );
        return;
    }

    uint32_t total_sectors = disk_get_sector_count();

    if (total_sectors < BACKUP_SECTORS) {
        terminal_print("Filesystem backup failed: disk too small.\n");
        return;
    }

    uint32_t sectors =
        ((uint32_t)sizeof(fs_backup_image_t) +
         DISK_SECTOR_SIZE - 1U) / DISK_SECTOR_SIZE;

    if (FS_BACKUP_OFFSET + sectors > BACKUP_SECTORS ||
        sizeof(fs_backup_image_t) > sizeof(fs_backup_buffer)) {
        terminal_print("Filesystem backup failed: image too large.\n");
        return;
    }

    for (uint32_t i = 0; i < sizeof(fs_backup_buffer); i++)
        fs_backup_buffer[i] = 0;

    fs_backup_image_t *image =
        (fs_backup_image_t *)fs_backup_buffer;

    image->magic = FS_BACKUP_MAGIC;
    image->version = FS_BACKUP_VERSION;
    image->entry_count = FS_MAX_ENTRIES;
    image->checksum = 0;

    for (uint32_t i = 0; i < FS_MAX_ENTRIES; i++) {
        image->entries[i] = fs_entries[i];

        /* Захист від некоректних рядків */
        image->entries[i].name[FS_NAME_SIZE - 1] = '\0';
        image->entries[i].parent[FS_PATH_SIZE - 1] = '\0';
    }

    image->checksum = fs_checksum(
        fs_backup_buffer,
        (uint32_t)sizeof(fs_backup_image_t)
    );

    uint32_t first_lba =
        total_sectors - BACKUP_SECTORS + FS_BACKUP_OFFSET;

    for (uint32_t i = 0; i < sectors; i++) {
        if (!disk_write_sector(
                first_lba + i,
                &fs_backup_buffer[i * DISK_SECTOR_SIZE])) {
            terminal_print_color(
                "Filesystem backup failed: disk write error.\n",
                COLOR_LIGHT_RED
            );
            return;
        }
    }

    terminal_print_color(
        "Filesystem backup saved.\n",
        COLOR_GREEN
    );
}

void fs_restore(void)
{
    if (!disk_is_ready()) {
        terminal_print("Filesystem restore failed: disk unavailable.\n");
        return;
    }

    uint32_t total_sectors = disk_get_sector_count();

    if (total_sectors < BACKUP_SECTORS)
        return;

    uint32_t sectors =
        ((uint32_t)sizeof(fs_backup_image_t) +
         DISK_SECTOR_SIZE - 1U) / DISK_SECTOR_SIZE;

    if (FS_BACKUP_OFFSET + sectors > BACKUP_SECTORS ||
        sizeof(fs_backup_image_t) > sizeof(fs_backup_buffer)) {
        terminal_print("Filesystem restore failed: image too large.\n");
        return;
    }

    for (uint32_t i = 0; i < sizeof(fs_backup_buffer); i++)
        fs_backup_buffer[i] = 0;

    uint32_t first_lba =
        total_sectors - BACKUP_SECTORS + FS_BACKUP_OFFSET;

    for (uint32_t i = 0; i < sectors; i++) {
        if (!disk_read_sector(
                first_lba + i,
                &fs_backup_buffer[i * DISK_SECTOR_SIZE])) {
            terminal_print_color(
                "Filesystem restore failed: disk read error.\n",
                COLOR_LIGHT_RED
            );
            return;
        }
    }

    fs_backup_image_t *image =
        (fs_backup_image_t *)fs_backup_buffer;

    if (image->magic != FS_BACKUP_MAGIC ||
        image->version != FS_BACKUP_VERSION ||
        image->entry_count != FS_MAX_ENTRIES) {
        terminal_print("No valid filesystem backup found.\n");
        return;
    }

    uint32_t saved_checksum = image->checksum;
    image->checksum = 0;

    uint32_t actual_checksum = fs_checksum(
        fs_backup_buffer,
        (uint32_t)sizeof(fs_backup_image_t)
    );

    if (actual_checksum != saved_checksum) {
        terminal_print_color(
            "Filesystem restore failed: checksum error.\n",
            COLOR_LIGHT_RED
        );
        return;
    }

    for (uint32_t i = 0; i < FS_MAX_ENTRIES; i++) {
        fs_entries[i] = image->entries[i];

        fs_entries[i].name[FS_NAME_SIZE - 1] = '\0';
        fs_entries[i].parent[FS_PATH_SIZE - 1] = '\0';

        if (fs_entries[i].used != 0 &&
            fs_entries[i].used != 1) {
            terminal_print("Filesystem restore failed: invalid entry.\n");
            return;
        }
    }

    terminal_print_color(
        "Filesystem restored.\n",
        COLOR_GREEN
    );
}

static char command_history[HISTORY_SIZE][HISTORY_LINE_SIZE];
static int history_count = 0;
static int history_index = 0;
char keyboard_read_char(void);

static char last_command[256];
static char command_history[HISTORY_SIZE][256];

const acpi_rsdp_t *kernel_rsdp = 0;
void terminal_print(const char *str);
void string_copy(char *dest, const char *src);


static inline void outw(unsigned short port, unsigned short value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outl(unsigned short port, unsigned int value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

void shutdown_system(void)
{
    terminal_print("ACPI: shutting down...\n");

    if (!acpi_shutdown_with_rsdp(kernel_rsdp)) {
        terminal_print("ACPI shutdown failed.\n");
        return;
    }

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
/* created_file і file_count вже оголошені вище */

char created_file[32] = "";
int cursor_x = 0;
int cursor_y = 0;
int terminal_width = VGA_WIDTH;
int terminal_height = VGA_HEIGHT;

typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned long long u64;
typedef unsigned short u16;

typedef struct __attribute__((packed)) {
    u32 flags;
    u32 mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr;
    u32 syms[4];
    u32 mmap_length, mmap_addr, drives_length, drives_addr, config_table;
    u32 boot_loader_name, apm_table, vbe_control_info, vbe_mode_info;
    u16 vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    u64 framebuffer_addr;
    u32 framebuffer_pitch, framebuffer_width, framebuffer_height;
    unsigned char framebuffer_bpp, framebuffer_type;
    unsigned char red_position, red_mask_size, green_position, green_mask_size;
    unsigned char blue_position, blue_mask_size;
} multiboot_info_t;

typedef struct __attribute__((packed)) {
    u32 total_size;
    u32 reserved;
} multiboot2_info_t;

typedef struct __attribute__((packed)) {
    u32 type;
    u32 size;
} multiboot2_tag_t;

typedef struct __attribute__((packed)) {
    u32 type;
    u32 size;
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    unsigned char framebuffer_bpp;
    unsigned char framebuffer_type;
    u16 reserved;
    unsigned char color_info[6];
} multiboot2_framebuffer_tag_t;

volatile u32 *framebuffer = 0;
u32 framebuffer_pitch_pixels = 0;
u32 framebuffer_width = 0;
u32 framebuffer_height = 0;
unsigned char framebuffer_red_position = 16;
unsigned char framebuffer_green_position = 8;
unsigned char framebuffer_blue_position = 0;
int framebuffer_enabled = 0;

#define BUFFER_SIZE 256
char input_buffer[BUFFER_SIZE];
int input_index = 0;

char current_dir[64] = "/";
char current_user[32] = "svzt";
char custom_created_user[32] = "";

static inline void outb(unsigned short port, unsigned char val);
static inline unsigned char inb(unsigned short port);

static const unsigned char font5x7[36][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,2,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}
};
static const unsigned char unknown_glyph[7] = {14,17,1,2,4,0,4};
static const unsigned char glyph_space[7] = {0,0,0,0,0,0,0};
static const unsigned char glyph_colon[7] = {0,4,0,0,4,0,0};
static const unsigned char glyph_dot[7] = {0,0,0,0,0,4,0};
static const unsigned char glyph_comma[7] = {0,0,0,0,0,4,8};
static const unsigned char glyph_slash[7] = {1,2,2,4,8,8,16};
static const unsigned char glyph_backslash[7] = {16,8,8,4,2,2,1};
static const unsigned char glyph_dash[7] = {0,0,0,31,0,0,0};
static const unsigned char glyph_underscore[7] = {0,0,0,0,0,0,31};
static const unsigned char glyph_equals[7] = {0,31,0,31,0,0,0};
static const unsigned char glyph_plus[7] = {0,4,4,31,4,4,0};
static const unsigned char glyph_star[7] = {0,21,14,31,14,21,0};
static const unsigned char glyph_greater[7] = {16,8,4,2,4,8,16};
static const unsigned char glyph_less[7] = {1,2,4,8,4,2,1};
static const unsigned char glyph_at[7] = {14,17,23,21,23,16,15};
static const unsigned char glyph_left_paren[7] = {2,4,8,8,8,4,2};
static const unsigned char glyph_right_paren[7] = {8,4,2,2,2,4,8};
static const unsigned char glyph_left_bracket[7] = {14,8,8,8,8,8,14};
static const unsigned char glyph_right_bracket[7] = {14,2,2,2,2,2,14};
static const unsigned char glyph_exclamation[7] = {4,4,4,4,4,0,4};
static const unsigned char glyph_quote[7] = {10,10,0,0,0,0,0};

static const unsigned char *glyph_for(char c) {
    if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
    if (c >= 'A' && c <= 'Z') return font5x7[c - 'A'];
    if (c >= '0' && c <= '9') return font5x7[26 + c - '0'];
    switch (c) {
        case ' ': return glyph_space;
        case ':': return glyph_colon;
        case '.': return glyph_dot;
        case ',': return glyph_comma;
        case '/': return glyph_slash;
        case '\\': return glyph_backslash;
        case '-': return glyph_dash;
        case '_': return glyph_underscore;
        case '=': return glyph_equals;
        case '+': return glyph_plus;
        case '*': return glyph_star;
        case '>': return glyph_greater;
        case '<': return glyph_less;
        case '@': return glyph_at;
        case '(': return glyph_left_paren;
        case ')': return glyph_right_paren;
        case '[': return glyph_left_bracket;
        case ']': return glyph_right_bracket;
        case '!': return glyph_exclamation;
        case '\'': return glyph_quote;
        default: return unknown_glyph;
    }
}

static u32 color_to_rgb(char color) {
    static const u32 colors[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
        0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
        0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
    };
    u32 rgb = colors[color & 0x0F];
    u32 red = (rgb >> 16) & 0xFF;
    u32 green = (rgb >> 8) & 0xFF;
    u32 blue = rgb & 0xFF;
    return (red << framebuffer_red_position) |
           (green << framebuffer_green_position) |
           (blue << framebuffer_blue_position);
}

static void framebuffer_draw_cell(int cell_x, int cell_y, char c, char color) {
    const unsigned char *glyph = glyph_for(c);
    u32 foreground = color_to_rgb(color);
    int x, y;
    for (y = 0; y < CELL_HEIGHT; y++) {
        unsigned char row = (y >= 1 && y <= 14) ? glyph[(y - 1) / 2] : 0;
        for (x = 0; x < CELL_WIDTH; x++) {
            u32 pixel = 0;
            if (c != ' ' && x >= 1 && x <= 10 && (row & (1 << (4 - ((x - 1) / 2))))) pixel = foreground;
            framebuffer[(cell_y * CELL_HEIGHT + y) * framebuffer_pitch_pixels + cell_x * CELL_WIDTH + x] = pixel;
        }
    }
}


void terminal_initialize(u32 multiboot_info_address)
{
    framebuffer_enabled = 0;

    const multiboot2_info_t *info =
        (const multiboot2_info_t *)(uintptr_t)multiboot_info_address;

    if (info == 0 || info->total_size < 16 ||
        info->total_size > 0x1000000u) {
        return;
    }

    const u8 *base = (const u8 *)info;
    u32 offset = 8;

    while (offset <= info->total_size - 8) {
        const multiboot2_tag_t *tag =
            (const multiboot2_tag_t *)(base + offset);

        if (tag->size < 8 ||
            tag->size > info->total_size - offset) {
            return;
        }

        if (tag->type == 0) {
            break;
        }

        if (tag->type == 8 &&
            tag->size >= sizeof(multiboot2_framebuffer_tag_t)) {

            const multiboot2_framebuffer_tag_t *fb =
                (const multiboot2_framebuffer_tag_t *)tag;

            if (fb->framebuffer_type == 1 &&
                fb->framebuffer_bpp == 32 &&
                (fb->framebuffer_addr >> 32) == 0 &&
                fb->framebuffer_width >= CELL_WIDTH &&
                fb->framebuffer_height >= CELL_HEIGHT &&
                fb->framebuffer_pitch >= fb->framebuffer_width * 4u) {

                framebuffer = (volatile u32 *)(uintptr_t)
                    fb->framebuffer_addr;

                framebuffer_pitch_pixels = fb->framebuffer_pitch / 4;
                framebuffer_width = fb->framebuffer_width;
                framebuffer_height = fb->framebuffer_height;

                framebuffer_red_position = fb->color_info[0];
                framebuffer_green_position = fb->color_info[2];
                framebuffer_blue_position = fb->color_info[4];

                terminal_width = framebuffer_width / CELL_WIDTH;
                terminal_height = framebuffer_height / CELL_HEIGHT;
                framebuffer_enabled = 1;
                return;
            }
        }

        offset = (offset + tag->size + 7u) & ~7u;
    }
}


void terminal_clear() {
    int y, x;
    if (framebuffer_enabled) {
        for (y = 0; y < terminal_height; y++) {
            for (x = 0; x < terminal_width; x++) framebuffer_draw_cell(x, y, ' ', COLOR_WHITE);
        }
    } else {
        volatile unsigned short *vga = VGA_MEMORY;
        for (y = 0; y < VGA_HEIGHT; y++) {
            for (x = 0; x < VGA_WIDTH; x++) vga[y * VGA_WIDTH + x] = 0x0700;
        }
    }
    cursor_x = 0;
    cursor_y = 0;
}

void terminal_scroll() {
    int y, x;
    if (framebuffer_enabled) {
        for (y = 0; y < (terminal_height - 1) * CELL_HEIGHT; y++) {
            for (x = 0; x < (int)framebuffer_width; x++) framebuffer[y * framebuffer_pitch_pixels + x] = framebuffer[(y + CELL_HEIGHT) * framebuffer_pitch_pixels + x];
        }
        for (y = (terminal_height - 1) * CELL_HEIGHT; y < terminal_height * CELL_HEIGHT; y++) {
            for (x = 0; x < (int)framebuffer_width; x++) framebuffer[y * framebuffer_pitch_pixels + x] = 0;
        }
    } else {
        volatile unsigned short *vga = VGA_MEMORY;
        for (y = 0; y < VGA_HEIGHT - 1; y++) {
            for (x = 0; x < VGA_WIDTH; x++) vga[y * VGA_WIDTH + x] = vga[(y + 1) * VGA_WIDTH + x];
        }
        for (x = 0; x < VGA_WIDTH; x++) vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = 0x0700;
    }
    cursor_y = terminal_height - 1;
}

void terminal_putchar(char c, char color) {
    static const unsigned short serial_port = 0x3F8;
    if (c == '\n') {
        while ((inb(serial_port + 5) & 0x20) == 0) { }
        outb(serial_port, '\r');
        while ((inb(serial_port + 5) & 0x20) == 0) { }
        outb(serial_port, '\n');
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= terminal_height) {
            terminal_scroll();
        }
        return;
    }

    if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            if (framebuffer_enabled) framebuffer_draw_cell(cursor_x, cursor_y, ' ', color);
            else VGA_MEMORY[cursor_y * VGA_WIDTH + cursor_x] = ((unsigned short)color << 8) | ' ';
        }
        return;
    }

    while ((inb(serial_port + 5) & 0x20) == 0) { }
    outb(serial_port, (unsigned char)c);
    if (framebuffer_enabled) framebuffer_draw_cell(cursor_x, cursor_y, c, color);
    else VGA_MEMORY[cursor_y * VGA_WIDTH + cursor_x] = ((unsigned short)color << 8) | (unsigned char)c;

    cursor_x++;
    if (cursor_x >= terminal_width) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= terminal_height) {
            terminal_scroll();
        }
    }
}

void terminal_print_color(const char *str, char color) {
    int i = 0;
    while (str[i] != '\0') {
        terminal_putchar(str[i], color);
        i++;
    }
}

void terminal_print(const char *str) {
    terminal_print_color(str, COLOR_WHITE);
}

int string_compare(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) {
            return 0;
        }
        i++;
    }
    return s1[i] == s2[i];
}

int string_starts_with(const char *str, const char *prefix) {
    int i = 0;
    while (prefix[i] != '\0') {
        if (str[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static int fs_has_extension(const char *name)
{
    int last_dot = -1;
    int i = 0;

    while (name[i] != '\0') {
        if (name[i] == '.')
            last_dot = i;
        i++;
    }

    // Крапка має бути не першою і не останнім символом
    return last_dot > 0 && name[last_dot + 1] != '\0';
}

static int fs_name_valid(const char *name)
{
    int i = 0;

    if (name[0] == '\0')
        return 0;

    if (string_compare(name, ".") ||
        string_compare(name, ".."))
        return 0;

    while (name[i] != '\0') {
        if (name[i] == '/' || name[i] == '\\')
            return 0;

        if (i >= FS_NAME_SIZE - 1)
            return 0;

        i++;
    }

    return 1;
}

static int fs_find(const char *name, const char *parent)
{
    int i;

    for (i = 0; i < FS_MAX_ENTRIES; i++) {
        if (fs_entries[i].used &&
            string_compare(fs_entries[i].name, name) &&
            string_compare(fs_entries[i].parent, parent))
            return i;
    }

    return -1;
}

static int fs_create(const char *name, const char *parent, int is_dir)
{
    int i;

    if (!fs_name_valid(name))
        return -1;

    if (fs_find(name, parent) >= 0)
        return -2; // Уже існує

    for (i = 0; i < FS_MAX_ENTRIES; i++) {
        if (!fs_entries[i].used) {
            string_copy(fs_entries[i].name, name);
            string_copy(fs_entries[i].parent, parent);
            fs_entries[i].is_dir = is_dir;
            fs_entries[i].used = 1;
            return i;
        }
    }

    return -3; // Немає вільних записів
}

static void fs_list(void)
{
    int i;
    int found = 0;

    for (i = 0; i < FS_MAX_ENTRIES; i++) {
        if (!fs_entries[i].used)
            continue;

        // Показуємо лише об'єкти поточної папки
        if (!string_compare(fs_entries[i].parent, current_dir))
            continue;

        if (fs_entries[i].is_dir) {
            terminal_print("[DIR]  ");
            terminal_print(fs_entries[i].name);
            terminal_print("/");
        } else {
            terminal_print("[FILE] ");
            terminal_print(fs_entries[i].name);
        }

        terminal_print("\n");
        found = 1;
    }

    if (!found)
        terminal_print("Directory is empty\n");
}
static int fs_make_path(
    char *out,
    const char *parent,
    const char *name
);
static int fs_move_file(
    const char *src_name,
    const char *src_parent,
    const char *dst_dir
)
{
    int src = fs_find(src_name, src_parent);

    if (src < 0) {
        terminal_print("mv: source not found\n");
        return 0;
    }

    if (fs_entries[src].is_dir) {
        terminal_print("mv: source must be a file\n");
        return 0;
    }

    /* Перевіряємо, що директорія призначення існує */
    if (string_compare(dst_dir, "/")) {
        /* Коренева директорія існує завжди */
    } else {
        int found_dir = 0;

        for (int i = 0; i < FS_MAX_ENTRIES; i++) {
            if (fs_entries[i].used &&
                fs_entries[i].is_dir) {
                char path[FS_PATH_SIZE];

                if (fs_make_path(
                        path,
                        fs_entries[i].parent,
                        fs_entries[i].name) &&
                    string_compare(path, dst_dir)) {
                    found_dir = 1;
                    break;
                }
            }
        }

        if (!found_dir) {
            terminal_print("mv: destination directory not found\n");
            return 0;
        }
    }

    /* Не переміщуємо файл у ту саму директорію */
    if (string_compare(src_parent, dst_dir)) {
        terminal_print("mv: source and destination are the same\n");
        return 0;
    }

    /* Не перезаписуємо файл з таким самим ім'ям */
    if (fs_find(src_name, dst_dir) >= 0) {
        terminal_print("mv: destination already exists\n");
        return 0;
    }

    if (string_compare(dst_dir, "/")) {
        string_copy(fs_entries[src].parent, "/");
    } else {
        string_copy(fs_entries[src].parent, dst_dir);
    }

    terminal_print("mv: file moved\n");
    return 1;
}

static int fs_copy_file(
    const char *src_name,
    const char *src_parent,
    const char *dst_dir
)
{
    int src = fs_find(src_name, src_parent);

    if (src < 0) {
        terminal_print("cp: source not found\n");
        return 0;
    }

    if (fs_entries[src].is_dir) {
        terminal_print("cp: source must be a file\n");
        return 0;
    }

    int dst_exists = string_compare(dst_dir, "/");

    for (int i = 0; i < FS_MAX_ENTRIES && !dst_exists; i++) {
        if (!fs_entries[i].used || !fs_entries[i].is_dir)
            continue;

        char path[FS_PATH_SIZE];

        if (fs_make_path(path,
                         fs_entries[i].parent,
                         fs_entries[i].name) &&
            string_compare(path, dst_dir)) {
            dst_exists = 1;
        }
    }

    if (!dst_exists) {
        terminal_print("cp: destination directory not found\n");
        return 0;
    }

    if (fs_find(src_name, dst_dir) >= 0) {
        terminal_print("cp: destination already exists\n");
        return 0;
    }

    int result = fs_create(src_name, dst_dir, 0);

    if (result < 0) {
        terminal_print("cp: cannot create destination file\n");
        return 0;
    }

    terminal_print("cp: file copied\n");
    return 1;
}

static int fs_make_path(
    char *out,
    const char *parent,
    const char *name
) {
    int i = 0;
    int j = 0;

    if (string_compare(parent, "/")) {
        out[i++] = '/';
    } else {
        while (parent[j] != '\0') {
            if (i >= FS_PATH_SIZE - 1)
                return 0;

            out[i++] = parent[j++];
        }

        if (i >= FS_PATH_SIZE - 1)
            return 0;

        out[i++] = '/';
    }

    j = 0;

    while (name[j] != '\0') {
        if (i >= FS_PATH_SIZE - 1)
            return 0;

        out[i++] = name[j++];
    }

    out[i] = '\0';
    return 1;
}

static int fs_split_file_path(
    const char *full_path,
    char *parent,
    char *name
)
{
    int len = 0;
    int slash = -1;

    while (full_path[len] != '\0') {
        if (len >= FS_PATH_SIZE - 1)
            return 0;

        if (full_path[len] == '/')
            slash = len;

        len++;
    }

    if (len == 0)
        return 0;

    int start = slash + 1;
    int name_len = len - start;

    if (name_len <= 0 || name_len >= FS_NAME_SIZE)
        return 0;

    for (int i = 0; i < name_len; i++)
        name[i] = full_path[start + i];

    name[name_len] = '\0';

    if (slash <= 0) {
        string_copy(parent, "/");
    } else {
        for (int i = 0; i < slash; i++)
            parent[i] = full_path[i];

        parent[slash] = '\0';
    }

    return 1;
}

static int fs_change_dir(const char *target)
{
    int index;
    int i;
    char new_path[FS_PATH_SIZE];

    // Перехід у корінь
    if (string_compare(target, "/")) {
        string_copy(current_dir, "/");
        return 1;
    }

    // Перехід на рівень вище
    if (string_compare(target, "..")) {
        if (string_compare(current_dir, "/"))
            return 1;

        i = 0;

        while (current_dir[i] != '\0')
            i++;

        // Прибираємо останній компонент шляху
        while (i > 0 && current_dir[i - 1] != '/')
            i--;

        if (i > 1)
            current_dir[i - 1] = '\0';
        else
            current_dir[1] = '\0';

        return 1;
    }

    // Шукаємо об'єкт у поточній папці RAM-файлової системи
    index = fs_find(target, current_dir);

    if (index < 0)
        return 0;

    // Не можна перейти у файл
    if (!fs_entries[index].is_dir)
        return 0;

    // Формуємо новий шлях
    if (!fs_make_path(new_path, current_dir, target))
        return 0;

    string_copy(current_dir, new_path);
    return 1;
}

static int fs_remove(const char *name, const char *parent, int recursive)
{
    int index;
    int i;
    char path[FS_PATH_SIZE];

    index = fs_find(name, parent);

    if (index < 0)
        return -1; // Не знайдено

    if (!fs_entries[index].is_dir) {
        fs_entries[index].used = 0;
        return 0;
    }

    if (!fs_make_path(path, parent, name))
        return -3; // Шлях задовгий

    for (i = 0; i < FS_MAX_ENTRIES; i++) {
        if (fs_entries[i].used &&
            string_compare(fs_entries[i].parent, path)) {

            if (!recursive)
                return -2; // Папка не порожня

            // Видаляємо дочірній об'єкт
            if (fs_remove(fs_entries[i].name, path, 1) != 0)
                return -3;

            // Після рекурсивного видалення перевіряємо
            // цей самий індекс знову
            i = -1;
        }
    }

    fs_entries[index].used = 0;
    return 0;
}
void string_copy(char *dest, const char *src) {
    int i = 0;
    while (src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void serial_initialize() {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

void print_prompt() {
    terminal_print_color(current_user, COLOR_YELLOW);
    terminal_print("@AOS:");
    terminal_print_color(current_dir, COLOR_LIGHT_BLUE);
    terminal_print("> ");
}

/* Ці функції вже мають бути оголошені вище */
int string_compare(const char *s1, const char *s2);
void string_copy(char *dest, const char *src);



void reboot_system(void)
{
    terminal_print_color("Rebooting AOS...\n", COLOR_YELLOW);

    __asm__ volatile ("cli");

    /* Спроба перезавантаження через контролер клавіатури */
    unsigned char status;
    do {
        __asm__ volatile ("inb $0x64, %0" : "=a"(status));
    } while (status & 0x02);

    __asm__ volatile ("outb %0, $0x64" : : "a"((unsigned char)0xFE));
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void execute_command_internal(char *cmd, int is_budo);

void execute_command(const char *cmd) {
    char command_copy[256];
    unsigned int i = 0;

    // Копіюємо команду для виконання
    while (i < sizeof(command_copy) - 1 && cmd[i] != '\0') {
        command_copy[i] = cmd[i];
        i++;
    }
    command_copy[i] = '\0';

    // Не перезаписуємо попередню команду через !budo
    if (!(
        string_compare(command_copy, "!budo") ||
        string_compare(command_copy, "!BUDO")
    )) {
        i = 0;

        while (i < sizeof(last_command) - 1 &&
               command_copy[i] != '\0') {
            last_command[i] = command_copy[i];
            i++;
        }

        last_command[i] = '\0';
    }

    execute_command_internal(command_copy, 0);
}

void execute_command_internal(char *cmd, int is_budo) {
    
if (string_compare(cmd, "backup")) {
    if (!disk_is_ready()) {
        terminal_print_color(
            "Backup failed: disk not available.\n",
            COLOR_LIGHT_RED
        );
        return;
    }

    if (history_backup(
            command_history,
            (uint32_t)history_count
        )) {
        terminal_print_color(
            "Command history backup saved.\n",
            COLOR_GREEN
        );
    } else {
        terminal_print_color(
            "Backup failed: could not save history.\n",
            COLOR_LIGHT_RED
        );
    }

    fs_backup();

    return;
}

    if (string_compare(cmd, "!budo") ||
        string_compare(cmd, "!BUDO")) {

        if (last_command[0] == '\0') {
            terminal_print("No previous command!\n");
            return;
        }

        char retry[256];
        unsigned int i = 0;

        while (i < sizeof(retry) - 1 &&
               last_command[i] != '\0') {
            retry[i] = last_command[i];
            i++;
        }

        retry[i] = '\0';

        execute_command_internal(retry, 1);
        return;
    }

    if (string_compare(cmd, "help")) {
        terminal_print("AOS Available commands:\n");
        terminal_print("\n");
        terminal_print("  help      - Show this help message\n");
        terminal_print("\n");
        terminal_print("  fastfetch - Display system info & logo\n");
        terminal_print("\n");
        terminal_print("  ls        - List directory contents\n");
        terminal_print("\n");
        terminal_print("  cd        - Change directory (e.g. cd home)\n");
        terminal_print("\n");
        terminal_print("  touch     - Create file (e.g. touch test.txt)\n");
        terminal_print("\n");
        terminal_print("  rm        - Remove file (e.g. rm test.txt)\n");
        terminal_print("\n");
        terminal_print("  useradd   - Create user [budo required] (e.g. useradd bob)\n");
        terminal_print("\n");
        terminal_print("  echo      - Print text (e.g. echo hello)\n");
        terminal_print("\n");
        terminal_print("  budo      - Execute command as root (e.g. budo shutdown)\n");
        terminal_print("\n");
        terminal_print("  clear     - Clear the screen\n");
        terminal_print("\n");
        terminal_print("  version   - Display OS version\n");
        terminal_print("\n");
        terminal_print("  reboot    - Reboot system [budo required]\n");
        terminal_print("\n");
        terminal_print("  shutdown  - Power off VM [budo required]\n");
        terminal_print("\n");
        terminal_print("  hello     - Display a greeting message\n");
        terminal_print("\n");
        terminal_print("  mv        - Move file (e.g. mv test.txt /home)\n");
        terminal_print("\n");
        terminal_print("  cp        - Copy file (e.g. cp test.txt /home)\n");
    } else if (string_compare(cmd, "fastfetch")) {
        terminal_print_color("      /\\        ", COLOR_GREEN); terminal_print("OS: AOS (First Version)\n");
        terminal_print_color("     /  \\       ", COLOR_GREEN); terminal_print("Kernel: Bare-metal x86 C\n");
        terminal_print_color("    / /\\ \\      ", COLOR_GREEN); terminal_print("Arch: i386 Protected Mode\n");
        terminal_print_color("   / ____ \\     ", COLOR_GREEN); terminal_print("Display: GOP framebuffer + VGA fallback\n");
        terminal_print_color("  /_/    \\_\\    ", COLOR_GREEN); terminal_print("Shell: AOS Kernel Shell\n");
    } else if (string_compare(cmd, "ls")) {
    fs_list();

    } else if (string_compare(cmd, "cd")) {
        terminal_print("Usage: cd <directory>\n");
    } else if (string_starts_with(cmd, "cd ")) {
        const char *target = cmd + 3;

        if (!fs_change_dir(target)) {
            terminal_print("cd: directory not found\n");
        }
    } else if (string_starts_with(cmd, "touch ")) {

    const char *name = cmd + 6;
    int is_dir = !fs_has_extension(name);
    int result = fs_create(name, current_dir, is_dir);

    if (result == -1) {
        terminal_print("touch: invalid name\n");
    } else if (result == -2) {
        terminal_print("touch: already exists\n");
    } else if (result == -3) {
        terminal_print("touch: filesystem is full\n");
    } else {
        terminal_print(is_dir ? "Directory created: "
                              : "File created: ");
        terminal_print(name);
        terminal_print("\n");
    }
    } else if (string_starts_with(cmd, "rm ")) {

    const char *name = cmd + 3;
    int recursive = is_budo;
    int result = fs_remove(name, current_dir, recursive);

    if (result == -1) {
        terminal_print("rm: not found\n");
    } else if (result == -2) {
        terminal_print("rm: directory is not empty\n");
    } else if (result == -3) {
        terminal_print("rm: error\n");
    } else {
        terminal_print("Removed: ");
        terminal_print(name);
        terminal_print("\n");
    }
    
    } else if (string_starts_with(cmd, "mv ")) {
        char *args = (char *)cmd + 3;
        char *space = args;

        while (*space != '\0' && *space != ' ')
            space++;

        if (*space == '\0') {
            terminal_print("Usage: mv <source> <directory>\n");
            return;
        }

        *space = '\0';
        char *src_path = args;
        char *dst_path = space + 1;

        while (*dst_path == ' ')
            dst_path++;

        int dst_len = 0;
        while (dst_path[dst_len] != '\0')
            dst_len++;

        if (dst_len > 0 && dst_path[dst_len - 1] == '/')
            dst_path[dst_len - 1] = '\0';

        char src_parent[FS_PATH_SIZE];
        char src_name[FS_NAME_SIZE];

        if (!fs_split_file_path(src_path, src_parent, src_name)) {
            terminal_print("mv: invalid source path\n");
            return;
        }

        if (src_parent[0] != '/') {
            terminal_print("mv: use an absolute source path for now\n");
            return;
        }

        if (dst_path[0] != '/') {
            terminal_print("mv: use an absolute destination path for now\n");
            return;
        }

        if (fs_move_file(src_name, src_parent, dst_path)) {
            terminal_print("Moved successfully\n");
        } else {
            terminal_print("mv: operation failed\n");
        }
        
        } else if (string_starts_with(cmd, "cp ")) {
    char *args = (char *)cmd + 3;
    char *space = args;

    while (*space != '\0' && *space != ' ')
        space++;

    if (*space == '\0') {
        terminal_print("Usage: cp <source> <directory>\n");
        return;
    }

    *space = '\0';
    char *src_path = args;
    char *dst_arg = space + 1;

    while (*dst_arg == ' ')
        dst_arg++;

    char dst_resolved[FS_PATH_SIZE];

    int dst_len = 0;
    while (dst_arg[dst_len] != '\0')
        dst_len++;

    if (dst_len > 0 && dst_arg[dst_len - 1] == '/')
        dst_arg[dst_len - 1] = '\0';

    if (dst_arg[0] == '/') {
        string_copy(dst_resolved, dst_arg);
    } else {
        if (!fs_make_path(dst_resolved, current_dir, dst_arg)) {
            terminal_print("cp: destination path too long\n");
            return;
        }
    }

    char src_parent[FS_PATH_SIZE];
    char src_name[FS_NAME_SIZE];

    if (!fs_split_file_path(src_path, src_parent, src_name)) {
        terminal_print("cp: invalid source path\n");
        return;
    }

    if (src_parent[0] != '/') {
        terminal_print("cp: use an absolute source path for now\n");
        return;
    }

    fs_copy_file(src_name, src_parent, dst_resolved);
    } else if (string_starts_with(cmd, "useradd ")) {
        if (!is_budo) {
            terminal_print_color("Permission denied: 'useradd' requires budo privileges!\n", COLOR_LIGHT_RED);
            return;
        }
        const char *username = cmd + 8;
        string_copy(custom_created_user, username);
        terminal_print("User created successfully: ");
        terminal_print(username);
        terminal_print("\n");
    } else if (string_starts_with(cmd, "echo ")) {
        terminal_print(cmd + 5);
        terminal_print("\n");
    } else if (string_starts_with(cmd, "budo ")) {
        terminal_print_color("[budo] Authenticated as root for: ", COLOR_LIGHT_BLUE);
        terminal_print(cmd + 5);
        terminal_print("\n");
        execute_command_internal(cmd + 5, 1);
    } else if (string_compare(cmd, "budo")) {
        terminal_print("budo: missing command specification\n");
    } else if (string_compare(cmd, "clear")) {
        terminal_clear();
    } else if (string_compare(cmd, "version")) {
        terminal_print("AOS v0.1 - Built by svzt\n");
    } else if (string_compare(cmd, "hello")) {
        terminal_print("Hello AOS it's my first operational system.\n");
    } else if (string_compare(cmd, "reboot")) {
    if (!is_budo) {
        terminal_print_color(
            "Permission denied: 'reboot' requires budo privileges!\n",
            COLOR_LIGHT_RED
        );
        return;
    }
    reboot_system();
    return;
    

    } else if (string_compare(cmd, "shutdown")) {
        if (!is_budo) {
            terminal_print_color("Permission denied: 'shutdown' requires budo privileges!\n", COLOR_LIGHT_RED);
            return;
        }
        terminal_print_color("Shutting down AOS system...\n", COLOR_LIGHT_RED);
        shutdown_system();
        return;
    } else {
        terminal_print("Unknown command: ");
        terminal_print(cmd);
        terminal_print("\n");
    }
}
      
extern char keyboard_read_char();


void kmain(unsigned int multiboot_magic,
           unsigned int multiboot_info)
{
    serial_initialize();

    if (multiboot_magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        terminal_initialize(multiboot_info);

        terminal_print_color(
            "Boot error: not loaded via Multiboot 2.\n",
            COLOR_LIGHT_RED
        );

        while (1) {
            __asm__ __volatile__("cli; hlt");
        }
    }

    /* Ініціалізуємо термінал один раз */
    terminal_initialize(multiboot_info);
    terminal_clear();

    /* Ініціалізуємо диск один раз */
    int disk_ready = disk_init();

    if (disk_ready) {
        /* Відновлюємо історію команд */
        uint32_t restored_count = 0;

        if (history_restore(
                command_history,
                HISTORY_SIZE,
                &restored_count)) {

            history_count = (int)restored_count;

            terminal_print(
                "Command history restored.\n"
            );
        } else {
            terminal_print(
                "No valid history backup found.\n"
            );
        }

        /* Відновлюємо файлову систему */
        fs_restore();

    } else {
        terminal_print_color(
            "Disk not available.\n",
            COLOR_LIGHT_RED
        );
    }

    /* Банер AOS */
    terminal_print_color(
        "=== AOS v0.1 (First Version) ===\n",
        COLOR_GREEN
    );

    terminal_print(
        "Type 'help' for available commands.\n\n"
    );

    /* ACPI */
    const acpi_rsdp_t *rsdp =
        acpi_find_rsdp(multiboot_info);

    kernel_rsdp = rsdp;

    if (rsdp != 0) {
        terminal_print(
            "ACPI: RSDP found via Multiboot 2\n"
        );
    } else {
        terminal_print_color(
            "ACPI: RSDP not found in Multiboot 2 tags\n",
            COLOR_LIGHT_RED
        );
    }

    /* Початковий prompt */
    print_prompt();
    input_index = 0;
    input_buffer[0] = '\0';
    history_index = history_count;

    /* Основний цикл оболонки */
    while (1) {
        char c = keyboard_read_char();

        /* Стрілки вгору та вниз — історія команд */
        if (c == KEY_UP || c == KEY_DOWN) {
            if (history_count == 0) {
                continue;
            }

            int available = history_count < HISTORY_SIZE
                ? history_count
                : HISTORY_SIZE;

            int oldest = history_count - available;

            if (c == KEY_UP) {
                if (history_index > oldest) {
                    history_index--;
                }
            } else {
                if (history_index < history_count) {
                    history_index++;
                }
            }

            /* Очистити поточний рядок */
            while (input_index > 0) {
                terminal_print("\b \b");
                input_index--;
            }

            input_buffer[0] = '\0';

            /* Порожній рядок після останньої команди */
            if (history_index >= history_count) {
                continue;
            }

            unsigned int slot =
                (unsigned int)history_index % HISTORY_SIZE;

            unsigned int i = 0;

            while (i < sizeof(input_buffer) - 1 &&
                   command_history[slot][i] != '\0') {
                input_buffer[i] = command_history[slot][i];
                i++;
            }

            input_buffer[i] = '\0';
            input_index = (int)i;

            terminal_print(input_buffer);
            continue;
        }

        /* Enter */
        if (c == '\n') {
            input_buffer[input_index] = '\0';
            terminal_print("\n");

            if (input_index > 0) {
                unsigned int slot =
                    (unsigned int)history_count % HISTORY_SIZE;

                unsigned int i = 0;

                while (i < sizeof(command_history[slot]) - 1 &&
                       input_buffer[i] != '\0') {
                    command_history[slot][i] = input_buffer[i];
                    i++;
                }

                command_history[slot][i] = '\0';

                if (history_count < HISTORY_SIZE) {
                    history_count++;
                }

                history_index = history_count;
            }

            execute_command(input_buffer);

            input_index = 0;
            input_buffer[0] = '\0';
            history_index = history_count;

            print_prompt();
        }

        /* Backspace */
        else if (c == '\b') {
            if (input_index > 0) {
                input_index--;
                input_buffer[input_index] = '\0';

                terminal_print("\b \b");
            }
        }

        /* Звичайні символи */
        else if ((unsigned char)c >= 32 &&
                 input_index < (int)sizeof(input_buffer) - 1) {

            input_buffer[input_index++] = c;
            input_buffer[input_index] = '\0';

            char temp[2] = { c, '\0' };
            terminal_print(temp);
        }
    }
}