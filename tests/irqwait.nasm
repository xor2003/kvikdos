; A blocking BIOS key read must leave IRQ0 running without host input.
bits 16
org 100h
mov ax,3508h
int 21h
mov [old8],bx
mov [old8+2],es
mov dx,timer
mov ax,2508h
int 21h
mov al,36h
out 43h,al
mov ax,11932
out 40h,al
mov al,ah
out 40h,al
%ifdef DOS_POLL
waiting:
mov ah,6
mov dl,0ffh
int 21h
jz waiting
cmp al,0dh
%elifdef DOS_WAIT
mov ah,7
int 21h
cmp al,0dh
%else
xor ah,ah
int 16h
cmp ax,1c0dh
%endif
jne fail
cmp word [cs:ticks],20
jb fail
mov ax,4c00h
int 21h
fail:
mov ax,4c01h
int 21h
timer:
inc word [cs:ticks]
cmp word [cs:ticks],20
jne chain
push ax
push cx
mov ah,5
mov cx,1c0dh
int 16h
pop cx
pop ax
chain:
jmp far [cs:old8]
old8: dd 0
ticks: dw 0
