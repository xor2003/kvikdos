; A BIOS key consumed before installing IRQ1 must not be replayed. Model
; F-15s ISR: read E0, then let the chained BIOS consume its following byte.
bits 16
org 100h
xor ah,ah
int 16h
mov ax,3509h
int 21h
mov [old9],bx
mov [old9+2],es
mov dx,keyboard
mov ax,2509h
int 21h
mov al,36h
out 43h,al
mov ax,1193
out 40h,al
mov al,ah
out 40h,al
mov dx,ready
mov ah,9
int 21h
call waitkey
cmp ax,4800h
jne fail
call waitkey
cmp ax,1c0dh
jne fail
call waitkey
cmp ax,011bh
jne fail
cmp byte [cs:badprefix],0
jne fail
mov ax,4c00h
int 21h
fail:
mov ax,4c01h
int 21h
waitkey:
mov ah,1
int 16h
jz waitkey
xor ah,ah
int 16h
ret
keyboard:
inc word [cs:irqs]
push ax
cmp byte [cs:prefix],0
jne skip
in al,60h
cmp al,0e0h
jne read_done
mov byte [cs:prefix],1
jmp read_done
skip:
mov byte [cs:prefix],0
read_done:
pushf
call far [cs:old9]
push es
push bx
mov ax,40h
mov es,ax
cmp word [cs:irqs],2
jne not_make
mov bx,[es:6ch]
mov [cs:make_tick],bx
not_make:
cmp word [cs:irqs],4
jne not_break
mov bx,[es:6ch]
sub bx,[cs:make_tick]
jnz not_break
mov byte [cs:badprefix],1
not_break:
cmp word [cs:irqs],1
jne clean
mov bx,[es:1ah]
cmp bx,[es:1ch]
je clean
mov byte [cs:badprefix],1
clean:
pop bx
pop es
checked:
pop ax
iret
old9: dd 0
make_tick: dw 0
badprefix: db 0
irqs: dw 0
prefix: db 0
ready: db "IRQKEYS-READY$"
