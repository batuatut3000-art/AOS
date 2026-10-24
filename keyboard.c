
#include <stddef.h>
#include <stdint.h>

static inline uint8_t in_port(uint16_t port)
{
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static const char scan_code_to_ascii[58] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
    '\n', 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',',
    '.', '/', 0, '*', 0, ' '
};

char keyboard_read_char(void)
{
    while (1) {
        if (in_port(0x64) & 0x01) {
            uint8_t scancode = in_port(0x60);

            /* Пропускаємо відпускання клавіш */
            if (scancode & 0x80)
                continue;

            if (scancode < sizeof(scan_code_to_ascii)) {
                char key = scan_code_to_ascii[scancode];

                if (key != 0)
                    return key;
            }
        }
    }
}
