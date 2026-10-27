
#include <stdint.h>

#define KEY_UP   ((char)0x80)
#define KEY_DOWN ((char)0x81)

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

static int left_shift_pressed = 0;
static int right_shift_pressed = 0;
static int extended_scancode = 0;

/* US keyboard layout: lowercase letters and unshifted symbols */
static const char scan_code_to_ascii[128] = {
    [0x01] = 0x1B,  // Escape
    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',
    [0x0C] = '-',
    [0x0D] = '=',
    [0x0E] = '\b',
    [0x0F] = '\t',

    [0x10] = 'q',
    [0x11] = 'w',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'y',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',
    [0x1A] = '[',
    [0x1B] = ']',
    [0x1C] = '\n',

    [0x1E] = 'a',
    [0x1F] = 's',
    [0x20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',
    [0x27] = ';',
    [0x28] = '\'',
    [0x29] = '`',
    [0x2B] = '\\',

    [0x2C] = 'z',
    [0x2D] = 'x',
    [0x2E] = 'c',
    [0x2F] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = 'm',
    [0x33] = ',',
    [0x34] = '.',
    [0x35] = '/',
    [0x39] = ' '
};

static uint8_t in_port(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

/* Only Shift + 1 changes a character: 1 -> ! */
static char apply_shift(char c)
{
    if ((left_shift_pressed || right_shift_pressed) && c == '1') {
        return '!';
    }

    return c;
}

char keyboard_read_char(void)
{
    for (;;) {
        /* Wait for data from the keyboard controller. */
        if ((in_port(KEYBOARD_STATUS_PORT) & 1) == 0) {
            continue;
        }

        uint8_t scancode = in_port(KEYBOARD_DATA_PORT);

        /* Extended scancode prefix, used by arrow keys. */
        if (scancode == 0xE0) {
            extended_scancode = 1;
            continue;
        }

        if (extended_scancode) {
            extended_scancode = 0;

            /* Ignore extended key releases. */
            if (scancode & 0x80) {
                continue;
            }

            if (scancode == 0x48) {
                return KEY_UP;
            }

            if (scancode == 0x50) {
                return KEY_DOWN;
            }

            continue;
        }

        /* Key release: high bit is set. */
        if (scancode & 0x80) {
            uint8_t released = scancode & 0x7F;

            if (released == 0x2A) {
                left_shift_pressed = 0;
            } else if (released == 0x36) {
                right_shift_pressed = 0;
            }

            continue;
        }

        /* Left Shift pressed. */
        if (scancode == 0x2A) {
            left_shift_pressed = 1;
            continue;
        }

        /* Right Shift pressed. */
        if (scancode == 0x36) {
            right_shift_pressed = 1;
            continue;
        }

        /* Ignore Caps Lock: it does not change letter case. */
        if (scancode == 0x3A) {
            continue;
        }

        if (scancode >= 128) {
            continue;
        }

        char c = scan_code_to_ascii[scancode];

        if (c == '\0') {
            continue;
        }

        return apply_shift(c);
    }
}
