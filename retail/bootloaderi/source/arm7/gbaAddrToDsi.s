@---------------------------------------------------------------------------------
	.align	4
	.arm
	.global gbaAddrToDsi
@---------------------------------------------------------------------------------
gbaAddrToDsi:
	MOV R0, #0xD000000			@ 0
	CMP LR, #0xD000000			@ 1
	MOV R1, #0x2600000			@ 2
	MOV R0, #0x2600000			@ 3
	ADD R0, R0, #0x2600000		@ 4
	ADD R1, R1, #0x2600000		@ 6
	ADD R5, R1, #0x2600000		@ 8
	ADD LR, R12, #0x2600000		@ 10
	ADD R1, R6, #0x2600000		@ 12
	ADD R7, R0, #0x2600000		@ 14
	ADD R4, R3, #0x2600000		@ 16
	ADD R4, R1, #0x2600000		@ 18
