[ORG 0x7C00]
[BITS 16]
global _start

_start:
    cli
    cld
    xor ax, ax
    mov es, ax
    mov gs, ax
    mov ds, ax
    mov ss, ax
    mov fs, ax
    mov sp, 0x7C00
    mov bp, 0x7C00

    push dx
    mov al, 'B'
    mov dx, 0x3F8
    out dx, al
    pop dx

    mov [StartDiskNumber], dl

.TestLBA:
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [StartDiskNumber]
    int 0x13
    jc .CHSRead
    cmp bx, 0xAA55
    jne .CHSRead
    test cx, 1
    jz .CHSRead
    test cx, 2
    jz .CHSRead
    jmp .LBARead

.CHSRead:
    push dx
    mov dx, 0x3F8
    mov al, 'C'
    out dx, al
    pop dx

    xor ax, ax
    mov es, ax
    mov ds, ax
    mov bx, 0x7E00

    mov ah, 0x02
    mov al, 0x05
    mov ch, 0x00
    mov cl, 0x02
    mov dh, 0x00
    mov dl, [StartDiskNumber]
    int 0x13
    jc .ReadError
    jmp .JumpToStart2

.LBARead:
    push dx
    mov dx, 0x3F8
    mov al, 'L'
    out dx, al
    pop dx

    xor ax, ax
    mov ds, ax
    mov es, ax

    mov ah, 0x42
    mov dl, [StartDiskNumber]
    mov si, ReadLBAInStartFuncDAP
    int 0x13
    jc .CHSRead
    jmp .JumpToStart2

.JumpToStart2:
    push dx
    mov dx, 0x3F8
    mov al, 'J'
    out dx, al
    pop dx

    jmp 0x0000:0x7E00

.ReadError:
    mov dx, 0x3F8
    mov al, 'E'
    out dx, al
    mov al, 0xA
    out dx, al
    mov al, ah
    call PrintHexU8
.Hang:
    cli
    hlt
    jmp $

align 4
ReadLBAInStartFuncDAP:
    db 0x10
    db 0x00
    db 0x05
    dw 0x7E00
    dw 0x0000
    dq 0x01

StartDiskNumber: db 0x00

PrintHexU8:
    push ax
    shr al, 4
    call PrintNibble
    pop ax
    push ax
    and al, 0x0F
    call PrintNibble
    pop ax
    ret

PrintNibble:
    push ax
    push dx
    cmp al, 10
    jb .Digit
    add al, 'A'-10
    jmp .Out
.Digit:
    add al, '0'
    jmp .Out
.Out:
    mov dx, 0x3F8
    out dx, al
    pop dx
    pop ax
    ret

times 510-($-$$) db 0
db 0x55
db 0xAA

_start2:
    cli
    cld
    xor ax, ax
    mov es, ax
    mov gs, ax
    mov ds, ax
    mov ss, ax
    mov fs, ax

    mov [BootDiskNumberInStart2], dl

    push dx
    mov dx, 0x3F8
    mov al, '2'
    out dx, al
    pop dx

    cmp word [0x8400], 0xFDFD
    jne .Hang

    push dx
    mov dx, 0x3F8
    mov al, 'F'
    out dx, al
    pop dx

    mov eax, [0x8402]
    mov [KernelSector], eax
    mov ebx, [0x8402 + 8]
    mov [KernelSize], ebx
    mov eax, [0x8402 + 16]
    mov [KernelNameLen], eax

    push dx
    mov dx, 0x3F8
    mov al, 'K'
    out dx, al
    pop dx

    mov eax, ebx
    test eax, eax
    jnz .HaveSec
    mov eax, 1
.HaveSec:
    mov [KernelSectors], ax

    push dx
    mov dx, 0x3F8
    mov al, 'D'
    out dx, al
    pop dx

    mov al, [KernelSectors]
    mov [ReadLBAInStartFuncDAPStart2+2], al
    mov word [ReadLBAInStartFuncDAPStart2+4], 0x8400
    mov word [ReadLBAInStartFuncDAPStart2+6], 0x0000

    mov eax, [KernelSector]
    mov [ReadLBAInStartFuncDAPStart2+8], eax
    mov dword [ReadLBAInStartFuncDAPStart2+12], 0

    mov ah, 0x42
    mov dl, [BootDiskNumberInStart2]
    mov si, ReadLBAInStartFuncDAPStart2
    int 0x13
    jc .Hang

    push dx
    mov dx, 0x3F8
    mov al, 'L'
    out dx, al
    pop dx

    mov ax, 0x2401
    int 0x15
    call A20_Check
    jnc .A20OK
    in al, 0x92
    or al, 2
    out 0x92, al
    call A20_Check
    jnc .A20OK
    call A20_KBC
    call A20_Check
    jc .Hang
.A20OK:

    push dx
    mov dx, 0x3F8
    mov al, 'A'
    out dx, al
    pop dx

    cli
    cld
    o32 lgdt [GlobalGDTDescriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp SEL_PM_CODE:PModeEntry

.Hang:
    push dx
    mov dx, 0x3F8
    mov al, 'H'
    out dx, al
    pop dx
    cli
    hlt
    jmp $

A20_Check:
    push ds
    push es
    push di
    push si
    push ax

    xor ax, ax
    mov ds, ax
    mov es, ax

    mov di, 0x0500
    mov si, 0x0510

    mov al, [ds:di]
    push ax
    mov al, [es:si]
    push ax

    mov byte [ds:di], 0x00
    mov byte [es:si], 0xFF

    mov al, [ds:di]
    cmp al, [es:si]
    je .off

    pop ax
    mov [es:si], al
    pop ax
    mov [ds:di], al
    pop ax
    pop si
    pop di
    pop es
    pop ds
    clc
    ret

.off:
    pop ax
    mov [es:si], al
    pop ax
    mov [ds:di], al
    pop ax
    pop si
    pop di
    pop es
    pop ds
    stc
    ret

A20_KBC_WaitInput:
    in al, 0x64
    test al, 2
    jnz A20_KBC_WaitInput
    ret

A20_KBC_WaitOutput:
    in al, 0x64
    test al, 1
    jz A20_KBC_WaitOutput
    ret

A20_KBC:
    push ax
    push cx

    call A20_KBC_WaitInput
    mov al, 0xD0
    out 0x64, al

    call A20_KBC_WaitOutput
    in al, 0x60
    mov cl, al

    call A20_KBC_WaitInput
    mov al, 0xD1
    out 0x64, al

    call A20_KBC_WaitInput
    mov al, cl
    or al, 2
    out 0x60, al

    call A20_KBC_WaitInput

    pop cx
    pop ax
    ret

align 4
ReadLBAInStartFuncDAPStart2:
    db 0x10
    db 0x00
    db 0x01
    dw 0x8400
    dw 0x0000
    dq 0x04

KernelSector: dd 0
KernelSize: dd 0
KernelSectors: dw 0
BootDiskNumberInStart2: db 0x00
KernelNameLen: dd 0x0

align 8
GlobalGDTStart:
GlobalGDTNull:      dq 0x0000000000000000
GlobalGDTPModeCode: dq 0x00CF9A000000FFFF
GlobalGDTPModeData: dq 0x00CF92000000FFFF
GlobalGDTLModeCode: dq 0x00209A0000000000
GlobalGDTLModeData: dq 0x0000920000000000
GlobalGDTEnd:

GlobalGDTDescriptor:
    dw GlobalGDTEnd - GlobalGDTStart - 1
    dd GlobalGDTStart

SEL_NULL    equ 0x00
SEL_PM_CODE equ 0x08
SEL_PM_DATA equ 0x10
SEL_LM_CODE equ 0x18
SEL_LM_DATA equ 0x20

[BITS 32]
PModeEntry:
    cli
    cld
    mov ax, SEL_PM_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    push dx
    mov dx, 0x3F8
    mov al, 'P'
    out dx, al
    pop dx

    call BuildPageTables

    mov eax, 0x1000
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    jmp SEL_LM_CODE:LModeEntry

.Hang:
    cli
    hlt
    jmp $

BuildPageTables:
    pushad
    push ds
    push es

    xor ax, ax
    mov ds, ax
    mov es, ax

    mov edi, 0x1000
    xor eax, eax
    mov ecx, 4 * 1024
    rep stosd

    mov edi, 0x1000
    mov eax, 0x2000
    or eax, 0x03
    mov [edi], eax

    mov edi, 0x2000
    mov eax, 0x3000
    or eax, 0x03
    mov [edi], eax

    mov edi, 0x3000
    mov eax, 0x4000
    or eax, 0x03
    mov [edi], eax

    mov edi, 0x4000
    mov eax, 0x0000
    or eax, 0x03
    mov ecx, 512
.PT:
    mov [edi], eax
    add eax, 0x1000
    add edi, 8
    dec ecx
    jnz .PT

    mov edi, 0x3000 + 8
    mov eax, 0x200000
    or eax, 0x83
    mov ecx, 511
.PD:
    mov [edi], eax
    mov dword [edi+4], 0
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .PD

    mov edi, 0x2000 + 8
    mov eax, 0x40000000
    or eax, 0x83
    mov ecx, 511
.PDPT:
    mov [edi], eax
    mov dword [edi+4], 0
    add eax, 0x40000000
    add edi, 8
    dec ecx
    jnz .PDPT

    pop es
    pop ds
    popad
    ret

[BITS 64]
LModeEntry:
    cli
    cld
    mov ax, SEL_LM_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x90000

    push rdx
    mov dx, 0x3F8
    mov al, 'M'
    out dx, al
    pop rdx

    mov eax, [KernelNameLen]
    add rax, 0x8400
    jmp rax

.Hang:
    cli
    hlt
    jmp $

times 2048-($-$$) db 0
