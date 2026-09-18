.section .plugin_MENU,"ax",%progbits
.balign 4
.arm

.extern g_MENUBridgeHandlerReturn

.global PLUGIN_MENU_BridgeHandlerTrampoline
.type   PLUGIN_MENU_BridgeHandlerTrampoline, %function
PLUGIN_MENU_BridgeHandlerTrampoline:
    push    {r4-r9, lr}
    mrc     p15, 0, r4, c13, c0, 3
    ldr     r12, =g_MENUBridgeHandlerReturn
    ldr     pc, [r12]

.ltorg
