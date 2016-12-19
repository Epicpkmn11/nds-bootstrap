@---------------------------------------------------------------------------------
	.section ".init"
@---------------------------------------------------------------------------------
	.global _start
	.align	4
	.arm

.global card_engine_start
.global card_engine_start_sync
.global card_engine_end
.global cardStruct
.global cacheStruct
.global patches_offset
.global commandAddr
.global fileCluster


patches_offset:
	.word	patches
intr_vblank_orig_return:
	.word	0x00000000
intr_fifo_orig_return:
	.word	0x00000000
commandAddr:
	.word	0x00000000
fileCluster:
	.word	0x00000000
cardStruct:
	.word	0x00000000
cacheStruct:
	.word	0x00000000
	
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

card_engine_start:

vblankHandler:
@ Hook the return address, then go back to the original function
	stmdb	sp!, {lr}
	adr 	lr, code_handler_start_vblank
	ldr 	r0,	intr_vblank_orig_return
	bx  	r0

fifoHandler:	
@ Hook the return address, then go back to the original function
	stmdb	sp!, {lr}
	adr 	lr, code_handler_start_fifo
	ldr 	r0,	intr_fifo_orig_return
	bx  	r0
	
code_handler_start_vblank:
	push	{r0-r12} 
	ldr	r3, =myIrqHandlerVBlank
	bl	_blx_r3_stub		@ jump to myIrqHandler
	
	@ exit after return
	b	exit
	
code_handler_start_fifo:
	push	{r0-r12} 
	ldr	r3, =myIrqHandlerFIFO
	bl	_blx_r3_stub		@ jump to myIrqHandler
  
  
    @ exit after return
	b	exit
	
@---------------------------------------------------------------------------------
_blx_r3_stub:
@---------------------------------------------------------------------------------
	bx	r3	
	
@---------------------------------------------------------------------------------

@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

exit:	
	pop   	{r0-r12} 
	pop  	{lr}
	bx  lr

.pool

card_engine_end:

patches:
.word	card_read_arm9
.word	card_pull_out_arm9
.word	vblankHandler
.word	fifoHandler
.word	cardStructArm9
card_read_arm9:
	stmfd   sp!, {r4-r11,lr}
	sub     sp, sp, #4
	ldr		r10, =0x24
	add		r10, r10, r0	
	
	ldr r8,=0x00000fff
	
	loop2:
    subs r8, #1
    bgt loop2
	
	@ new sdk version
	ldr 	r4, cardStructArm9
	ldr 	r5, [R4,#0x18] @Depends on the SDK version
	ldr 	r6, [R4,#0x1C] @Depends on the SDK version
	ldr 	r7, [R4,#0x20] @Depends on the SDK version
	
	@ mrc p15, 0, r0, c1, c0, 0
	@ bic r0, #1
	@ mcr p15, 0, r0, c1, c0, 0
	
	@ new sdk version
	ldr     r8, =0x027FFB08
	str 	r5, [R8,#0x4]
	str 	r6, [R8,#0x8]
	str 	r7, [R8,#0xC]
	
	@ldr r0,= 0x4000208
	@ldr r11,[r0]
	@mov r1, #0
	@str r1, [r0]
	
	ldr r8, =0x02700000
	
	cmp r6, r8
	
	bgt cmd1
	
	b cmd2
	
	cmd1:
	
	ldr 	r5, =0x027FEE04
	ldr		r6, =0x027FFB08
	str     r5, [r6]

	b end
	
	cmd2:
	ldr 	r5, =0x027FEE05
	ldr		r6, =0x027FFB08
	str     r5, [r6]

	top2:
	ldr		r6,=0x027FFB08 @sharedAddr
	ldr     r5, [r6]
	cmp		r5,#0
	bne top2
	
	b endfunc
	
	end:
	
	@REG_DISPCNT = MODE_0_2D | DISPLAY_BG0_ACTIVE;
	@ldr     r4, =0x04000000
	@ldr     r6, =0x10100
	@str     r6, [r4]
	
	@VRAM_A_CR = VRAM_ENABLE | VRAM_A_MAIN_BG;
	@ldr     r4, =0x04000240
	@mov     r6, #0x81
	@str     r6, [r4]
	
	@BG_PALETTE[0] = RGB15(31, 0, 0)
	@ldr     r4, =0x05000000
	@mov     r6, #31
	@str     r6, [r4]
	
	top:
	ldr		r6,=0x027FFB08 @sharedAddr
	ldr     r5, [r6]
	cmp		r5,#0
	bne top
	
	@BG_PALETTE[0] = RGB15(31, 0, 0)
	@ldr     r4, =0x05000000
	@ldr		r6, =0xffffffff
	@str     r6, [r4]
	
	ldr		r6,=0x027FFB14
	ldr		r8,=0x027FFB14
	ldr		r9,=0x027FFB0C
	LDR     R9, [R9]

	@BG_PALETTE[0] = RGB150x027FFB140x027FFB14(31, 0, 0)
	@ldr     r4, =0x05000000
	@ldr		r6, =10 
	@str     r6, [r4]

    ldr r7,=0x027FFB08
    ldr r8, [r7, #0xC] //len
    ldr r9, [r7, #0x8] //dst
	@sub r8, #1
    ldr r7,= 0x027ff800 
	loop:
    ldrb r10, [r7], #1
    strb r10, [r9], #1
    subs r8, #1
    bgt loop
	
	endfunc:
	
	ldr 	r7, cardStructArm9
	
	str 	r9, [R7,#0x1C]
	
	@BG_PALETTE[0] = RGB15(31, 0, 0)
	@ldr     r4, =0x05000000
	@ldr		r6, =0x027FEE04 
	@str     r6, [r4]
	
	@mrc p15, 0, r0, c1, c0, 0
	@orr r0, #1
	@mcr p15, 0, r0, c1, c0, 0
	
	@ldr r0,= 0x4000208
	@str r11, [r0]
	
	add     sp, sp, #4
	ldmfd   sp!, {r4-r11,lr}
	bx      lr
cardStructArm9:
.word	0x00000000	
.pool
card_pull_out_arm9:
	bx      lr
