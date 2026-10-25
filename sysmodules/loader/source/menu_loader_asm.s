.section .plugin_MENU,"ax",%progbits
.balign 4
.arm

.extern g_MENUPatchCodeResume
.extern g_MENUHomeReturn
.extern g_MENUHomeBranchTarget
.extern g_MENUCreateCodeSetReturn
.extern g_MENUCreateProcessReturn
.extern g_MENUPlgldrCloseBranchTarget
.extern g_MENUPlgldrInitCallReturn
.extern PLUGIN_MENU_DispatchHome
.extern PLUGIN_MENU_PreCreateCodeSet
.extern PLUGIN_MENU_PostCreateProcess
.extern PLUGIN_MENU_PlgldrCloseHandler
.extern PLUGIN_MENU_PlgldrInitHandler

.balign 4
@ original host patchCode() body, skipping only two overwritten prologue instructions
.global PLUGIN_MENU_PatchCodeOriginal
.type   PLUGIN_MENU_PatchCodeOriginal, %function
PLUGIN_MENU_PatchCodeOriginal:
    push    {r4-r11, lr}       @ host prologue instruction 1
    mov     r9, r2             @ host prologue instruction 2
    ldr     r12, =g_MENUPatchCodeResume
    ldr     pc, [r12]          @ resume host patchCode() at entry+8

.balign 4
@ hook: verified second HOME whitelist store and its following BEQ
.global PLUGIN_MENU_HomeHook
.type   PLUGIN_MENU_HomeHook, %function
PLUGIN_MENU_HomeHook:
    str     r2, [r3, #4]              @ replay: both whitelist writes precede callbacks
    push    {r0-r12, lr}
    mrs     r12, cpsr                 @ stock TST result controls the overwritten BEQ
    push    {r12}
    sub     sp, sp, #4                @ keep the callback stack 8-byte aligned
    bl      PLUGIN_MENU_DispatchHome
    add     sp, sp, #4
    pop     {r12}
    msr     cpsr_f, r12
    pop     {r0-r12, lr}
    beq     1f
    ldr     r12, =g_MENUHomeReturn     @ original fallthrough initializes r9/r8
    ldr     pc, [r12]
1:
    ldr     r12, =g_MENUHomeBranchTarget @ original disabled-setting exit
    ldr     pc, [r12]

.balign 4
@ hook: marker menu_loader_createcodeset_call, matches BL / CMP / BGE
.global PLUGIN_MENU_CreateCodeSetHook
.type   PLUGIN_MENU_CreateCodeSetHook, %function
PLUGIN_MENU_CreateCodeSetHook:
    push    {r0-r4, lr}
    mov     r0, r1
    bl      PLUGIN_MENU_PreCreateCodeSet
    pop     {r0-r4, lr}
    @ replay replaced svcCreateCodeSet call
    push    {r0}
    ldr     r0, [sp, #4]
    svc     0x73
    ldr     r2, [sp]
    str     r1, [r2]
    add     sp, sp, #4
    cmp     r0, #0                    @ replay replaced CMP
    ldr     r12, =g_MENUCreateCodeSetReturn
    ldr     pc, [r12]

.balign 4
@ hook: marker menu_loader_createprocess_call, matches BL / MOV / LDR / BL
.global PLUGIN_MENU_CreateProcessHook
.type   PLUGIN_MENU_CreateProcessHook, %function
PLUGIN_MENU_CreateProcessHook:
    @ replay replaced svcCreateProcess call
    push    {r0}
    svc     0x75
    ldr     r2, [sp]
    str     r1, [r2]
    add     sp, sp, #4
    push    {r0-r4, lr}
    mov     r1, r2
    bl      PLUGIN_MENU_PostCreateProcess
    pop     {r0-r4, lr}
    mov     r5, r0                    @ replay replaced MOV
    ldr     r12, =g_MENUCreateProcessReturn
    ldr     pc, [r12]

.balign 4
@ hook: marker menu_loader_plgldr_exit_call, confirms call to symbol svcCloseHandle
.global PLUGIN_MENU_PlgldrCloseHook
.type   PLUGIN_MENU_PlgldrCloseHook, %function
PLUGIN_MENU_PlgldrCloseHook:
    push    {r4, lr}
    bl      PLUGIN_MENU_PlgldrCloseHandler  @ original svcCloseHandle runs inside handler
    pop     {r4, lr}
    ldr     r12, =g_MENUPlgldrCloseBranchTarget
    ldr     pc, [r12]                  @ replay replaced B

.balign 4
@ hook: marker menu_loader_plgldr_init_call, confirms call to symbol plgldrInit
.global PLUGIN_MENU_PlgldrInitCallHook
.type   PLUGIN_MENU_PlgldrInitCallHook, %function
PLUGIN_MENU_PlgldrInitCallHook:
    push    {r4, lr}
    bl      PLUGIN_MENU_PlgldrInitHandler @ original plgldrInit runs inside handler
    pop     {r4, lr}
    cmp     r0, #0                    @ replay replaced CMP
    ldr     r12, =g_MENUPlgldrInitCallReturn
    ldr     pc, [r12]

.balign 4
.global PLUGIN_MENU_svcQueryMemory
.type   PLUGIN_MENU_svcQueryMemory, %function
PLUGIN_MENU_svcQueryMemory:
    push    {r0, r1, r4, r5, r6}
    svc     0x02
    ldr     r6, [sp]
    str     r1, [r6]
    str     r2, [r6, #4]
    str     r3, [r6, #8]
    str     r4, [r6, #12]
    ldr     r6, [sp, #4]
    str     r5, [r6]
    add     sp, sp, #8
    pop     {r4, r5, r6}
    bx      lr

.balign 4
.global PLUGIN_MENU_svcFlushEntireDataCache
.type   PLUGIN_MENU_svcFlushEntireDataCache, %function
PLUGIN_MENU_svcFlushEntireDataCache:
    svc     0x92
    bx      lr

.balign 4
.global PLUGIN_MENU_svcInvalidateEntireInstructionCache
.type   PLUGIN_MENU_svcInvalidateEntireInstructionCache, %function
PLUGIN_MENU_svcInvalidateEntireInstructionCache:
    svc     0x94
    bx      lr

.balign 4
.global PLUGIN_MENU_svcMapProcessMemoryEx
.type   PLUGIN_MENU_svcMapProcessMemoryEx, %function
PLUGIN_MENU_svcMapProcessMemoryEx:
    push    {r4, r5, r6}
    ldr     r4, [sp, #12]
    ldr     r5, [sp, #16]
    mov     r6, r0
    mov     r0, #0xFFFFFFF2
    svc     0xA0
    pop     {r4, r5, r6}
    bx      lr

.balign 4
.global PLUGIN_MENU_svcUnmapProcessMemoryEx
.type   PLUGIN_MENU_svcUnmapProcessMemoryEx, %function
PLUGIN_MENU_svcUnmapProcessMemoryEx:
    svc     0xA1
    bx      lr

.balign 4
.global PLUGIN_MENU_svcControlMemoryUnsafe
.type   PLUGIN_MENU_svcControlMemoryUnsafe, %function
PLUGIN_MENU_svcControlMemoryUnsafe:
    str     r4, [sp, #-4]!
    ldr     r4, [sp, #4]
    svc     0xA3
    ldr     r4, [sp], #4
    bx      lr


.balign 4
.global PLUGIN_MENU_svcCreateThread
.type   PLUGIN_MENU_svcCreateThread, %function
PLUGIN_MENU_svcCreateThread:
    push    {r0, r4}
    ldr     r0, [sp, #8]
    ldr     r4, [sp, #12]
    svc     0x08
    pop     {r2}
    str     r1, [r2]
    pop     {r4}
    bx      lr

.balign 4
.global PLUGIN_MENU_svcGetThreadPriority
.type   PLUGIN_MENU_svcGetThreadPriority, %function
PLUGIN_MENU_svcGetThreadPriority:
    push    {r0}
    svc     0x0B
    pop     {r3}
    str     r1, [r3]
    bx      lr

.balign 4
.global PLUGIN_MENU_svcCreateEvent
.type   PLUGIN_MENU_svcCreateEvent, %function
PLUGIN_MENU_svcCreateEvent:
    push    {r0}
    svc     0x17
    pop     {r2}
    str     r1, [r2]
    bx      lr

.balign 4
.global PLUGIN_MENU_svcSignalEvent
.type   PLUGIN_MENU_svcSignalEvent, %function
PLUGIN_MENU_svcSignalEvent:
    svc     0x18
    bx      lr

.balign 4
.global PLUGIN_MENU_svcWaitSynchronization
.type   PLUGIN_MENU_svcWaitSynchronization, %function
PLUGIN_MENU_svcWaitSynchronization:
    svc     0x24
    bx      lr

.ltorg
