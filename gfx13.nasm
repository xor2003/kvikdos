; gfx13.nasm -- mode 13h smoke test under kvikdos.
; Sets mode 13h, writes DAC entry 1 (red) via ports, fills the left half
; of A000:0000 with color 1 and the right half with color 2, exercises
; the int 10h pixel services, then exits.
org 0x100
start:
    mov ax, 0x13
    int 0x10                ; enter mode 13h

    mov dx, 0x3c8           ; DAC write index = 1
    mov al, 1
    out dx, al
    inc dx                  ; 0x3c9: R,G,B triple
    mov al, 63
    out dx, al              ; R = max
    xor al, al
    out dx, al              ; G = 0
    out dx, al              ; B = 0

    mov ax, 0xa000
    mov es, ax
    xor di, di
    mov bx, 200
.row:
    mov cx, 160
.left:
    mov al, 1
    stosb
    loop .left
    mov cx, 160
.right:
    mov al, 2
    stosb
    loop .right
    dec bx
    jnz .row

    mov ax, 0x0c03          ; int 10h write pixel: color 3 at (0,0)
    xor bx, bx
    xor cx, cx
    xor dx, dx
    int 0x10

    mov ah, 0x0d            ; read it back into AL
    xor cx, cx
    xor dx, dx
    int 0x10
    cmp al, 3
    je .rdok
    mov ax, 0x4c42          ; exit 0x42 on read-back failure
    int 0x21
.rdok:
    mov ax, 0x0e21          ; teletype '!' (nop draw in gfx mode)
    xor bx, bx
    int 0x10

    mov ax, 0x4c00          ; exit 0
    int 0x21
