; Distinct top/bottom halves expose vertical cropping in ANSI output.
bits 16
org 100h
mov ax,13h
int 10h
mov ax,0a000h
mov es,ax
xor di,di
mov ax,0404h
mov cx,16000
rep stosw
mov ax,0101h
mov cx,16000
rep stosw
mov ah,1
int 16h
mov ax,4c00h
int 21h
