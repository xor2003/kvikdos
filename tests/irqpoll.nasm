; IRQ0 must progress while port reads force continuous VM exits.
bits 16
org 100h
mov ax,3508h
int 21h
mov [old8],bx
mov [old8+2],es
mov dx,timer
mov ax,2508h
int 21h
xor ah,ah
int 1ah
mov [start],dx
mov al,36h
out 43h,al
mov ax,11932
out 40h,al
mov al,ah
out 40h,al
mov dx,3dah
again:
in al,dx
cmp word [cs:ticks],20
jb again
xor ah,ah
int 1ah
sub dx,[start]
cmp dx,12
ja fail
push ds
lds dx,[old8]
mov ax,2508h
int 21h
pop ds
mov ax,4c00h
int 21h
fail:
mov ax,4c01h
int 21h
timer:
inc word [cs:ticks]
jmp far [cs:old8]
old8: dd 0
ticks: dw 0
start: dw 0
