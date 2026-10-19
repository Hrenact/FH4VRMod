option casemap:none
.code
PUBLIC TestCameraTarget
TestCameraTarget PROC FRAME
    ; Exact first 16 bytes of the observed FH4 camera-update entry.
    mov rax, rsp
    mov [rax+8], rbx
    mov [rax+10h], rsi
    mov [rax+18h], rdi
    push rbp
    .pushreg rbp
    .endprolog
    ; Deterministic test body; no game code is executed here.
    mov rax, [rcx]
    xor rax, [rcx+8]
    mov [rdx], rax
    pop rbp
    ret
TestCameraTarget ENDP
END
