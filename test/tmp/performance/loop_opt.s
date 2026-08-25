	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #80
	stp x23, x24, [sp, #32]
	movz w24, #0
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	bl getint
	mov w23, w0
	movz w0, #33
	bl _sysy_starttime
	movz w21, #32768, lsl #16
	movn w20, #32768, lsl #16
	movz w19, #10
.Lmain_bb1:
	cbz w23, .Lmain_bb10
.Lmain_bb2:
	bl getint
	mov w27, w0
	bl getint
	mov w22, w0
	bl getint
	mov w13, w0
	cmp w27, w22
	b.ge .Lmain_bb18
.Lmain_bb3:
	movz w9, #7
	mul w10, w13, w9
	sub w7, w22, w10
	cmp w27, w7
	cset w15, lt
	cmp w13, #0
	movz w9, #9362
	cset w14, gt
	movk w9, #4681, lsl #16
	cmp w13, w9
	cset w12, le
	cmp w27, #0
	cset w11, ge
	add w9, w10, w21
	cmp w22, w9
	sub w9, w20, w13
	cset w10, ge
	add w9, w9, #1
	cmp w22, w9
	and w9, w14, w10
	cset w10, le
	and w9, w9, w12
	and w9, w9, w10
	and w9, w9, w11
	and w9, w9, w15
	cbz w9, .Lmain_bb13
.Lmain_bb11:
	mov w5, w27
	movz w11, #19923
	movz w25, #26809
	movz w10, #26581
	movz w14, #501
	movz w12, #65035
	mov w0, w24
	movn w6, #32768, lsl #16
	movn w8, #49152, lsl #16
	movz w28, #8192, lsl #16
	movk w11, #4194, lsl #16
	movz w27, #1001
	movk w25, #297, lsl #16
	movk w10, #7051, lsl #16
	movk w14, #15232, lsl #16
	movk w12, #50303, lsl #16
.Lmain_bb4:
	sub w15, w6, w5
	add w26, w5, w13
	cmp w5, w15
	sub w9, w6, w26
	csel w15, w5, w15, lt
	add w17, w26, w13
	cmp w26, w9
	sub w16, w6, w17
	csel w1, w26, w9, lt
	add w26, w17, w13
	cmp w17, w16
	sub w9, w6, w26
	csel w3, w17, w16, lt
	add w17, w26, w13
	cmp w26, w9
	sub w16, w6, w17
	csel w4, w26, w9, lt
	add w5, w17, w13
	cmp w17, w16
	sub w9, w6, w5
	csel w17, w17, w16, lt
	cmp w5, w9
	add w26, w5, w13
	csel w16, w5, w9, lt
	sub w9, w6, w26
	cmp w26, w9
	csel w2, w26, w9, lt
	add w5, w26, w13
	sub w9, w6, w5
	cmp w5, w9
	csel w26, w5, w9, lt
	sub w9, w8, w15
	cmp w15, w9
	csel w15, w15, w9, lt
	sub w9, w8, w1
	cmp w1, w9
	csel w1, w1, w9, lt
	sub w9, w8, w3
	cmp w3, w9
	csel w3, w3, w9, lt
	sub w9, w8, w4
	cmp w4, w9
	csel w4, w4, w9, lt
	sub w9, w8, w17
	cmp w17, w9
	csel w17, w17, w9, lt
	sub w9, w8, w16
	cmp w16, w9
	csel w16, w16, w9, lt
	sub w9, w8, w2
	cmp w2, w9
	csel w2, w2, w9, lt
	sub w9, w8, w26
	cmp w26, w9
	csel w26, w26, w9, lt
	sub w9, w28, w15
	cmp w15, w9
	csel w15, w15, w9, lt
	sub w9, w28, w1
	cmp w1, w9
	csel w1, w1, w9, lt
	sub w9, w28, w3
	cmp w3, w9
	csel w3, w3, w9, lt
	sub w9, w28, w4
	cmp w4, w9
	csel w4, w4, w9, lt
	sub w9, w28, w17
	cmp w17, w9
	csel w17, w17, w9, lt
	add w9, w15, w15, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w15, w9, w27, w15
	sub w9, w28, w16
	cmp w16, w9
	csel w16, w16, w9, lt
	add w9, w1, w1, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w1, w9, w27, w1
	add w9, w3, w3, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w3, w9, w27, w3
	sub w9, w28, w2
	cmp w2, w9
	csel w2, w2, w9, lt
	add w9, w4, w4, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w4, w9, w27, w4
	add w9, w17, w17, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w17, w9, w27, w17
	add w9, w16, w16, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w16, w9, w27, w16
	add w9, w2, w2, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w2, w9, w27, w2
	sub w9, w28, w26
	cmp w26, w9
	csel w26, w26, w9, lt
	smull x9, w15, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w9, w9, w25, w15
	add w15, w0, w9
	smull x9, w1, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w9, w9, w25, w1
	add w15, w15, w9
	smull x9, w3, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w3, w9, w25, w3
	add w9, w26, w26, lsl #1
	smull x9, w9, w11
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w26, w9, w27, w26
	smull x9, w4, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w4, w9, w25, w4
	smull x9, w17, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w17, w9, w25, w17
	smull x9, w16, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w16, w9, w25, w16
	smull x9, w2, w10
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	add w15, w15, w3
	msub w3, w9, w25, w2
	smull x9, w26, w10
	add w15, w15, w4
	add w15, w15, w17
	asr x9, x9, #53
	add w15, w15, w16
	add w9, w9, w9, lsr #31
	msub w17, w9, w25, w26
	add w16, w15, w3
	cmp w16, w14
	sub w15, w16, w14
	csel w15, w15, w16, ge
	cmp w15, w12
	add w9, w15, w14
	csel w9, w9, w15, le
	add w15, w9, w17
	cmp w15, w14
	sub w9, w15, w14
	csel w15, w9, w15, ge
	cmp w15, w12
	add w9, w15, w14
	add w5, w5, w13
	csel w0, w9, w15, le
	cmp w5, w7
	b.lt .Lmain_bb4
.Lmain_bb5:
	cmp w5, w22
	b.ge .Lmain_bb9
.Lmain_bb14:
	mov w27, w5
.Lmain_bb6:
	movz w12, #19923
	movz w15, #26809
	movz w11, #26581
	movz w14, #501
	movz w10, #15363
	movn w26, #32768, lsl #16
	movn w25, #49152, lsl #16
	movz w17, #8192, lsl #16
	movk w12, #4194, lsl #16
	movz w16, #1001
	movk w15, #297, lsl #16
	movk w11, #7051, lsl #16
	movk w14, #15232, lsl #16
	movk w10, #35246, lsl #16
.Lmain_bb7:
	sub w9, w26, w27
	cmp w27, w9
	csel w28, w27, w9, lt
	sub w9, w25, w28
	cmp w28, w9
	csel w28, w28, w9, lt
	sub w9, w17, w28
	cmp w28, w9
	csel w28, w28, w9, lt
	add w9, w28, w28, lsl #1
	smull x9, w9, w12
	asr x9, x9, #38
	add w9, w9, w9, lsr #31
	madd w28, w9, w16, w28
	smull x9, w28, w11
	asr x9, x9, #53
	add w9, w9, w9, lsr #31
	msub w9, w9, w15, w28
	add w28, w0, w9
	smull x9, w28, w10
	asr x9, x9, #32
	add w9, w9, w28
	asr w9, w9, #29
	add w9, w9, w9, lsr #31
	msub w0, w9, w14, w28
	add w27, w27, w13
	cmp w27, w22
	b.lt .Lmain_bb7
.Lmain_bb9:
	bl putint
	mov w0, w19
	bl putch
	sub w23, w23, #1
	b .Lmain_bb1
.Lmain_bb10:
	movz w0, #42
	bl _sysy_stoptime
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #80
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb13:
	mov w0, w24
	b .Lmain_bb6
.Lmain_bb18:
	mov w0, w24
	b .Lmain_bb9
	.size main, .-main
