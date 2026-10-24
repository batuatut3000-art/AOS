
[BITS 32]

section .multiboot
align 8

mb2_header:
    dd 0xE85250D6              ; Multiboot 2 magic
    dd 0                       ; Architecture: i386
    dd mb2_header_end - mb2_header
    dd -(0xE85250D6 + 0 + (mb2_header_end - mb2_header))

    ; Запит графічного framebuffer 800x600x32
    align 8
    dw 5                       ; Framebuffer header tag
    dw 0                       ; Required
    dd 20                      ; Tag size
    dd 800
    dd 600
    dd 32

    ; Кінець заголовка
    align 8
    dw 0
    dw 0
    dd 8

mb2_header_end:

section .text
global start
extern kmain

start:
    cli
    cld

    mov esp, stack_space
    xor ebp, ebp

    ; Multiboot 2: EAX = magic, EBX = info pointer
    push ebx
    push eax
    call kmain

.halt:
    cli
    hlt
    jmp .halt

section .bss
align 16
resb 8192
stack_space:
