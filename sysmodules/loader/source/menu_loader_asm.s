.section .plugin_MENU,"ax",%progbits
.balign 4
.arm

.extern g_MENUPatchCodeCallReturn
.extern g_MENUHomeReturn
.extern g_MENUCreateCodeSetReturn
.extern g_MENUCreateProcessReturn
.extern g_MENUPlgldrCloseBranchTarget
.extern g_MENUPlgldrInitCallReturn
.extern PLUGIN_MENU_PatchCodeCallHandler
.extern PLUGIN_MENU_DispatchHome
.extern PLUGIN_MENU_PreCreateCodeSet
.extern PLUGIN_MENU_PostCreateProcess
.extern PLUGIN_MENU_PlgldrCloseHandler
.extern PLUGIN_MENU_PlgldrInitHandler

.global PLUGIN_MENU_PatchCodeCallHook
.type   PLUGIN_MENU_PatchCodeCallHook, %function
PLUGIN_MENU_PatchCodeCallHook:
    push    {r4, lr}
    sub     sp, sp, #24
    ldr     r4, [sp, #32]
    str     r4, [sp, #0]
    ldr     r4, [sp, #36]
    str     r4, [sp, #4]
    ldr     r4, [sp, #40]
    str     r4, [sp, #8]
    ldr     r4, [sp, #44]
    str     r4, [sp, #12]
    ldr     r4, [sp, #48]
    str     r4, [sp, #16]
    ldr     r4, [sp, #52]
    str     r4, [sp, #20]
    bl      PLUGIN_MENU_PatchCodeCallHandler
    add     sp, sp, #24
    pop     {r4, lr}
    mov     r1, #0x10000000
    ldr     r12, =g_MENUPatchCodeCallReturn
    ldr     pc, [r12]

.balign 4
.global PLUGIN_MENU_HomeHook
.type   PLUGIN_MENU_HomeHook, %function
PLUGIN_MENU_HomeHook:
    ldr     r2, =0xE3A00000
    str     r2, [r7, r3]
    add     r3, r7, r3
    ldr     r2, =0xE12FFF1E
    str     r2, [r3, #4]
    push    {r0-r12, lr}
    bl      PLUGIN_MENU_DispatchHome
    pop     {r0-r12, lr}
    ldr     r12, =g_MENUHomeReturn
    ldr     pc, [r12]

.balign 4
.global PLUGIN_MENU_CreateCodeSetHook
.type   PLUGIN_MENU_CreateCodeSetHook, %function
PLUGIN_MENU_CreateCodeSetHook:
    push    {r0-r4, lr}
    mov     r0, r1
    bl      PLUGIN_MENU_PreCreateCodeSet
    pop     {r0-r4, lr}
    push    {r0}
    ldr     r0, [sp, #4]
    svc     0x73
    ldr     r2, [sp]
    str     r1, [r2]
    add     sp, sp, #4
    cmp     r0, #0
    ldr     r12, =g_MENUCreateCodeSetReturn
    ldr     pc, [r12]

.balign 4
.global PLUGIN_MENU_CreateProcessHook
.type   PLUGIN_MENU_CreateProcessHook, %function
PLUGIN_MENU_CreateProcessHook:
    push    {r0}
    svc     0x75
    ldr     r2, [sp]
    str     r1, [r2]
    add     sp, sp, #4
    push    {r0-r4, lr}
    mov     r1, r2
    bl      PLUGIN_MENU_PostCreateProcess
    pop     {r0-r4, lr}
    mov     r5, r0
    ldr     r12, =g_MENUCreateProcessReturn
    ldr     pc, [r12]

.balign 4
.global PLUGIN_MENU_PlgldrCloseHook
.type   PLUGIN_MENU_PlgldrCloseHook, %function
PLUGIN_MENU_PlgldrCloseHook:
    push    {r4, lr}
    bl      PLUGIN_MENU_PlgldrCloseHandler
    pop     {r4, lr}
    ldr     r12, =g_MENUPlgldrCloseBranchTarget
    ldr     pc, [r12]

.balign 4
.global PLUGIN_MENU_PlgldrInitCallHook
.type   PLUGIN_MENU_PlgldrInitCallHook, %function
PLUGIN_MENU_PlgldrInitCallHook:
    push    {r4, lr}
    bl      PLUGIN_MENU_PlgldrInitHandler
    pop     {r4, lr}
    cmp     r0, #0
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
