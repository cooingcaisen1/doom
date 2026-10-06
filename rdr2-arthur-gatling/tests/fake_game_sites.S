# Stand-in for the RDR2.exe code the mod looks for. Each site starts with the exact
# bytes of its sheets/game_hooks.json pattern, then hands over to a C++ stand-in.
    .intel_syntax noprefix
    .text

    .globl site_getNativeAddress
site_getNativeAddress:                 # 48 8B 15 ? ? ? ? 4C 8B C9 49 F7 D1
    mov rdx, qword ptr [rip + fake_native_table]
    .byte 0x4C, 0x8B, 0xC9              # mov r9, rcx (encoding the game uses)
    not r9
    mov rcx, r9
    not rcx
    jmp fake_get_native_address

    .globl site_currentScriptThread
site_currentScriptThread:              # 48 39 1D ? ? ? ? 75 ? 48 8D 05 ? ? ? ?
    cmp qword ptr [rip + fake_current_thread], rbx
    jne 1f
    lea rax, [rip + fake_current_thread]
1:  ret

    .globl site_scriptHandlerManager
site_scriptHandlerManager:             # 48 8D 0D ? ? ? ? E8 ? ? ? ? 84 C0 75 ? 8B 8B ? ? ? ?
    lea rcx, [rip + fake_script_handler_mgr]
    call fake_noop
    test al, al
    jne 1f
    mov ecx, dword ptr [rbx + 0x100]
1:  ret

    .globl site_isInSession
site_isInSession:                      # 80 3D ? ? ? ? ? 74 ? 48 8B 0D ? ? ? ? E8 ? ? ? ? 0F B6 40 ?
    cmp byte ptr [rip + fake_in_session], 0
    je 1f
    mov rcx, qword ptr [rip + fake_native_table]
    call fake_noop
    movzx eax, byte ptr [rax + 8]
1:  ret

    .globl site_updateSingleScripts
site_updateSingleScripts:              # 48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 45 33 F6 BD ? ? ? ?
    mov qword ptr [rsp + 8], rbx
    mov qword ptr [rsp + 16], rbp
    mov qword ptr [rsp + 24], rsi
    push rdi
    push r14
    push r15
    sub rsp, 0x20
    .byte 0x45, 0x33, 0xF6              # xor r14d, r14d
    mov ebp, 0x12345678
    call fake_update_single_scripts
    add rsp, 0x20
    pop r15
    pop r14
    pop rdi
    mov rbx, qword ptr [rsp + 8]
    mov rbp, qword ptr [rsp + 16]
    mov rsi, qword ptr [rsp + 24]
    ret

    .globl site_shutdownLoadingScreen
site_shutdownLoadingScreen:            # 8A 05 ? ? ? ? 84 C0 75 ? C6 05 ? ? ? ? ?
    mov al, byte ptr [rip + fake_loading_flag]
    test al, al
    jne 1f
    mov byte ptr [rip + fake_loading_flag], 1
1:  jmp fake_shutdown_loading_screen

    .globl site_initScriptThread
site_initScriptThread:                 # 48 89 5C 24 ? 57 48 83 EC ? 83 89 ? ? ? ? ? 33 FF 83 A1
    mov qword ptr [rsp + 8], rbx
    push rdi
    sub rsp, 0x20
    or dword ptr [rcx + 0x100], 0
    .byte 0x33, 0xFF                    # xor edi, edi
    and dword ptr [rcx + 0x104], -1
    call fake_init_script_thread
    add rsp, 0x20
    pop rdi
    mov rbx, qword ptr [rsp + 8]
    ret

    .globl site_tickScriptThread
site_tickScriptThread:                 # 48 89 5C 24 ? 57 48 83 EC ? 80 B9 ? ? ? ? ? 8B FA 48 8B D9 74 ? 8B 41
    mov qword ptr [rsp + 8], rbx
    push rdi
    sub rsp, 0x20
    cmp byte ptr [rcx + 0x100], 0
    .byte 0x8B, 0xFA                    # mov edi, edx
    .byte 0x48, 0x8B, 0xD9              # mov rbx, rcx
    je 1f
1:  mov eax, dword ptr [rcx + 8]
    mov rcx, rbx
    mov edx, edi
    call fake_tick_script_thread
    add rsp, 0x20
    pop rdi
    mov rbx, qword ptr [rsp + 8]
    ret

    .globl site_sysAlloc
site_sysAlloc:                         # E8 ? ? ? ? 48 85 C0 74 ? 48 83 20 ? 0F 57 C0 0F 11 40 ? 0F 11 40 ? 83 60
    call fake_sys_alloc
    test rax, rax
    je 1f
    and qword ptr [rax], 0
    xorps xmm0, xmm0
    movups xmmword ptr [rax + 8], xmm0
    movups xmmword ptr [rax + 24], xmm0
    and dword ptr [rax + 40], 0
1:  ret
