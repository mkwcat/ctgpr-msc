// main.S - LZMA loader init
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#define	r0 0
#define	r1 1
#define	sp 1
#define	r2 2
#define	toc 2
#define	r3 3
#define	r4 4
#define	r5 5
#define	r6 6
#define	r7 7
#define	r8 8
#define	r9 9
#define	r10 10
#define	r11 11
#define	r12 12
#define	r13 13
#define	r14 14
#define	r15 15
#define	r16 16
#define	r17 17
#define	r18 18
#define	r19 19
#define	r20 20
#define	r21 21
#define	r22 22
#define	r23 23
#define	r24 24
#define	r25 25
#define	r26 26
#define	r27 27
#define	r28 28
#define	r29 29
#define	r30 30
#define	r31 31

#define SRR0 26
#define SRR1 27

	.text
	.section .stub
	.global _start
_start:
	lis     r3, rvlStartup@h
	andis.  r3, r3, 0x7FFF
	ori     r3, r3, rvlStartup@l
	mtspr   SRR0, r3
	li      r4, 0
	mtspr   SRR1, r4
	rfi

rvlStartup:
	lis     sp, _stack_end@h
	ori     sp, sp, _stack_end@l
	bl      __InitBATS
	bl      __InitCache
	bl      load
	b       0

.size _start, . - _start

.section .bss
.global _stack
.global _stack_end
_stack:
.space 0x1000
_stack_end:
