; Live A20 aliasing, gate toggles, HMA persistence and trapped BDA writes.
bits 16
org 100h
mov bp,1
xor ax,ax
mov es,ax
mov word [es:9000h],5566h
mov ax,0ffffh
mov es,ax
inc bp
cmp word [es:9010h],5566h
jne fail
mov word [es:04a0h],1234h
xor ax,ax
mov es,ax
inc bp
cmp word [es:0490h],1234h
jne fail
mov al,2
out 92h,al
mov ax,0ffffh
mov es,ax
mov word [es:9010h],7788h
xor ax,ax
mov es,ax
inc bp
cmp word [es:9000h],5566h
jne fail
xor al,al
out 92h,al
mov ax,0ffffh
mov es,ax
inc bp
cmp word [es:9010h],5566h
jne fail
mov ax,2401h
int 15h
jc fail
inc bp
cmp word [es:9010h],7788h
jne fail
mov ax,2400h
int 15h
jc fail
inc bp
cmp word [es:9010h],5566h
jne fail
mov ax,4c00h
int 21h
fail:
mov ax,bp
mov ah,4ch
int 21h
