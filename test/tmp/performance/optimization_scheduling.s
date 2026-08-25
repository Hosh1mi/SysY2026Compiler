	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #128
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	stp x25, x26, [sp, #48]
	movz w21, #4
	stp x23, x24, [sp, #32]
	movz w22, #3
	stp x27, x28, [sp, #64]
	movz w20, #2
	movz w26, #1
	movz w25, #0
	bl getint
	mov w19, w0
	movz w0, #40
	bl _sysy_starttime
	movz w9, #0
	cmp w19, #0
	csel w2, w9, w19, lt
	cmp w2, #256
	b.hs .Lmain_bb11
.Lmain_bb4:
	sub w17, w19, #3
	mov w16, w22
	mov w15, w20
	mov w13, w26
	orr w14, wzr, #0x80000003
.Lmain_bb5:
	cmp w25, w17
	cset w10, lt
	cmp w19, w14
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb12
.Lmain_bb10:
	add w12, w13, w15
	add w11, w15, w16
	add w9, w21, w12
	add w10, w16, w21
	add w12, w12, w11
	add w11, w11, w10
	add w10, w10, w9
	add w9, w9, w12
	add w12, w12, w11
	add w11, w11, w10
	add w10, w10, w9
	add w9, w9, w12
	add w13, w12, w11
	add w15, w11, w10
	add w16, w10, w9
	add w21, w9, w13
	add w25, w25, #4
	b .Lmain_bb5
.Lmain_bb1:
	cbz w2, .Lmain_bb3
.Lmain_bb2:
	mul w10, w7, w4
	madd w12, w6, w3, w10
	mul w10, w22, w4
	madd w14, w8, w5, w12
	madd w12, w23, w3, w10
	mul w10, w7, w26
	madd w10, w6, w7, w10
	madd w10, w8, w22, w10
	madd w10, w28, w16, w10
	str w10, [sp, #80]
	mul w10, w7, w25
	madd w10, w6, w8, w10
	madd w10, w8, w21, w10
	madd w10, w28, w9, w10
	str w10, [sp, #84]
	mul w10, w7, w24
	madd w10, w6, w28, w10
	mul w11, w26, w4
	madd w10, w8, w20, w10
	madd w13, w27, w3, w11
	madd w10, w28, w0, w10
	mul w11, w16, w4
	madd w13, w25, w5, w13
	madd w15, w28, w1, w14
	str w10, [sp, #88]
	madd w11, w17, w3, w11
	madd w12, w21, w5, w12
	madd w14, w24, w1, w13
	mul w10, w26, w27
	madd w11, w9, w5, w11
	madd w13, w20, w1, w12
	madd w10, w27, w6, w10
	madd w12, w0, w1, w11
	madd w10, w25, w23, w10
	madd w11, w24, w17, w10
	mul w10, w26, w25
	madd w10, w27, w8, w10
	madd w10, w25, w21, w10
	madd w10, w24, w9, w10
	str w10, [sp, #92]
	mul w10, w26, w24
	madd w10, w27, w28, w10
	madd w10, w25, w20, w10
	madd w10, w24, w0, w10
	str w10, [sp, #96]
	mul w10, w22, w27
	madd w10, w23, w6, w10
	madd w10, w21, w23, w10
	madd w10, w20, w17, w10
	str w10, [sp, #100]
	mul w10, w22, w26
	madd w10, w23, w7, w10
	madd w10, w21, w22, w10
	madd w10, w20, w16, w10
	str w10, [sp, #104]
	mul w10, w22, w24
	madd w10, w23, w28, w10
	madd w10, w21, w20, w10
	madd w10, w20, w0, w10
	str w10, [sp, #108]
	mul w10, w16, w27
	madd w10, w17, w6, w10
	madd w10, w9, w23, w10
	madd w10, w0, w17, w10
	str w10, [sp, #112]
	mul w10, w16, w26
	madd w10, w17, w7, w10
	madd w10, w9, w22, w10
	madd w10, w0, w16, w10
	str w10, [sp, #116]
	mul w10, w16, w25
	madd w10, w17, w8, w10
	madd w10, w9, w21, w10
	madd w10, w0, w9, w10
	str w10, [sp, #120]
	movz w10, #1
	and w10, w2, w10
	cmp w10, #0
	cset w10, ne
	cmp w10, #0
	csel w3, w15, w3, ne
	mul w15, w7, w27
	cmp w10, #0
	csel w4, w14, w4, ne
	madd w6, w6, w6, w15
	cmp w10, #0
	csel w5, w13, w5, ne
	cmp w10, #0
	mul w14, w8, w23
	csel w1, w12, w1, ne
	madd w15, w26, w26, w15
	mul w12, w25, w22
	mov w27, w11
	mul w13, w28, w17
	add w26, w6, w14
	mul w11, w24, w16
	add w6, w14, w12
	mul w10, w20, w9
	add w14, w15, w12
	madd w15, w21, w21, w6
	add w12, w13, w11
	ldr w7, [sp, #80]
	ldr w8, [sp, #84]
	ldr w23, [sp, #100]
	ldr w28, [sp, #88]
	ldr w17, [sp, #112]
	ldr w25, [sp, #92]
	ldr w22, [sp, #104]
	ldr w24, [sp, #96]
	ldr w16, [sp, #116]
	ldr w20, [sp, #108]
	ldr w9, [sp, #120]
	add w12, w12, w10
	madd w0, w0, w0, w12
	add w6, w26, w13
	add w26, w14, w11
	add w21, w15, w10
	lsr w2, w2, #1
	b .Lmain_bb1
.Lmain_bb3:
	add w9, w3, w4
	add w9, w9, w5
	add w20, w9, w1
.Lmain_bb9:
	movz w11, #0
	cmp w19, #0
	csel w10, w11, w19, lt
	movz w9, #10
	cmp w10, #32
	lsl w9, w9, w10
	csel w19, w11, w9, hs
	movz w0, #43
	bl _sysy_stoptime
	mov w0, w20
	bl putint
	movz w0, #10
	bl putch
	mov w0, w19
	bl putint
	movz w0, #10
	bl putch
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #128
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb6:
	cmp w9, w19
	b.ge .Lmain_bb8
.Lmain_bb7:
	add w13, w13, w12
	add w12, w12, w11
	add w11, w11, w10
	add w10, w10, w13
	add w9, w9, #1
	b .Lmain_bb6
.Lmain_bb8:
	add w9, w13, w12
	add w9, w9, w11
	add w20, w9, w10
	b .Lmain_bb9
.Lmain_bb11:
	mov w3, w21
	mov w4, w22
	mov w5, w20
	mov w1, w26
	mov w6, w26
	mov w7, w25
	mov w8, w26
	mov w28, w26
	mov w27, w26
	mov w24, w25
	mov w23, w25
	mov w22, w26
	mov w21, w26
	mov w20, w25
	mov w17, w25
	mov w16, w25
	mov w9, w26
	mov w0, w26
	b .Lmain_bb1
.Lmain_bb12:
	mov w9, w25
	mov w10, w21
	mov w11, w16
	mov w12, w15
	b .Lmain_bb6
	.size main, .-main
