	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #80
	stp x21, x22, [sp, #16]
	movz w22, #1
	stp x19, x20, [sp]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	stp x27, x28, [sp, #64]
	bl getint
	mov w19, w0
	bl getint
	mov w20, w0
	movz w0, #13
	bl _sysy_starttime
	adrp x9, __sysy_par_ctx_0_0
	movz w1, #0
	str w19, [x9, :lo12:__sysy_par_ctx_0_0]
	mov w0, w1
	mov w2, w19
	bl __sysy_parallel_for
	cmp w19, #2
	cset w12, gt
	cmp w19, #18
	movz w9, #2
	cset w11, ge
	movk w9, #16384, lsl #16
	cmp w19, w9
	cset w10, le
	and w9, w12, w11
	sub w21, w19, #1
	sub w23, w19, #2
	and w9, w9, w10
	cbnz w9, .Lmain_bb22
.Lmain_bb26:
	movz w16, #2
	mov w14, w22
	mov w17, w22
	movk w16, #32768, lsl #16
.Lmain_bb1:
	cmp w17, w23
	cset w10, lt
	cmp w19, w16
	cset w9, ge
	and w9, w9, w10
	cbz w9, .Lmain_bb13
.Lmain_bb2:
	cmp w19, #2
	b.le .Lmain_bb7
.Lmain_bb27:
	mov w14, w22
	b .Lmain_bb3
.Lmain_bb6:
	add w14, w14, #1
	cmp w14, w21
	b.ge .Lmain_bb7
.Lmain_bb3:
	cmp w19, #2
	b.le .Lmain_bb6
.Lmain_bb4:
	adrp x25, x
	add x27, x25, :lo12:x
	adrp x24, x
	adrp x15, x
	adrp x13, x
	adrp x12, x
	adrp x11, x
	adrp x10, x
	adrp x9, x
	movn w25, #0
	movz w26, #63744
	add x24, x24, :lo12:x
	add x15, x15, :lo12:x
	add x13, x13, :lo12:x
	add x12, x12, :lo12:x
	add x11, x11, :lo12:x
	add x10, x10, :lo12:x
	add x9, x9, :lo12:x
	movk w26, #21, lsl #16
	add w28, w17, w25
	add w8, w17, #1
	smaddl x27, w17, w26, x27
	smaddl x24, w28, w26, x24
	smaddl x15, w8, w26, x15
	smaddl x13, w17, w26, x13
	smaddl x12, w17, w26, x12
	smaddl x11, w17, w26, x11
	smaddl x10, w17, w26, x10
	smaddl x9, w28, w26, x9
	movz w26, #2400
	add w8, w14, w25
	add w28, w14, #1
	smaddl x25, w14, w26, x27
	smaddl x24, w14, w26, x24
	smaddl x15, w14, w26, x15
	smaddl x13, w8, w26, x13
	smaddl x12, w28, w26, x12
	smaddl x11, w14, w26, x11
	smaddl x10, w14, w26, x10
	smaddl x9, w8, w26, x9
	add x25, x25, #4
	add x24, x24, #4
	add x15, x15, #4
	add x6, x13, #4
	add x7, x12, #4
	add x28, x10, #8
	mov x27, x9
	mov x8, x11
	mov w26, w22
.Lmain_bb5:
	ldr w5, [x24], #4
	ldr w9, [x15], #4
	ldr w13, [x6], #4
	ldr w12, [x7], #4
	ldr w11, [x8], #4
	ldr w10, [x28], #4
	add w5, w5, w9
	ldr w9, [x27], #4
	add w13, w5, w13
	add w12, w13, w12
	add w11, w12, w11
	add w10, w11, w10
	add w9, w10, w9
	sdiv w9, w9, w20
	add w26, w26, #1
	str w9, [x25], #4
	cmp w26, w21
	b.lt .Lmain_bb5
	b .Lmain_bb6
.Lmain_bb7:
	add w15, w17, #1
	cmp w19, #2
	b.le .Lmain_bb33
.Lmain_bb30:
	mov w14, w22
	b .Lmain_bb8
.Lmain_bb11:
	add w14, w14, #1
	cmp w14, w21
	b.ge .Lmain_bb12
.Lmain_bb8:
	cmp w19, #2
	b.le .Lmain_bb11
.Lmain_bb9:
	adrp x26, x
	add x27, x26, :lo12:x
	adrp x24, x
	movz w26, #63744
	add x24, x24, :lo12:x
	movk w26, #21, lsl #16
	add w28, w17, #2
	smaddl x24, w28, w26, x24
	adrp x25, x
	adrp x13, x
	adrp x12, x
	adrp x11, x
	adrp x10, x
	adrp x9, x
	add x25, x25, :lo12:x
	add x13, x13, :lo12:x
	add x12, x12, :lo12:x
	add x11, x11, :lo12:x
	add x10, x10, :lo12:x
	add x9, x9, :lo12:x
	smaddl x27, w15, w26, x27
	smaddl x25, w17, w26, x25
	smaddl x13, w15, w26, x13
	smaddl x12, w15, w26, x12
	smaddl x11, w15, w26, x11
	smaddl x10, w15, w26, x10
	smaddl x9, w17, w26, x9
	movn w28, #0
	add w8, w14, w28
	movz w26, #2400
	add w28, w14, #1
	smaddl x27, w14, w26, x27
	smaddl x25, w14, w26, x25
	smaddl x24, w14, w26, x24
	smaddl x13, w8, w26, x13
	smaddl x12, w28, w26, x12
	smaddl x11, w14, w26, x11
	smaddl x10, w14, w26, x10
	smaddl x9, w8, w26, x9
	add x26, x27, #4
	add x25, x25, #4
	add x24, x24, #4
	add x5, x13, #4
	add x6, x12, #4
	add x8, x10, #8
	mov x28, x9
	mov x7, x11
	mov w27, w22
.Lmain_bb10:
	ldr w4, [x25], #4
	ldr w9, [x24], #4
	ldr w13, [x5], #4
	ldr w12, [x6], #4
	ldr w11, [x7], #4
	ldr w10, [x8], #4
	add w4, w4, w9
	ldr w9, [x28], #4
	add w13, w4, w13
	add w12, w13, w12
	add w11, w12, w11
	add w10, w11, w10
	add w9, w10, w9
	sdiv w9, w9, w20
	add w27, w27, #1
	str w9, [x26], #4
	cmp w27, w21
	b.lt .Lmain_bb10
	b .Lmain_bb11
.Lmain_bb12:
	add w17, w17, #2
	b .Lmain_bb1
.Lmain_bb13:
	cmp w17, w21
	b.ge .Lmain_bb41
.Lmain_bb35:
	mov w15, w14
	mov w16, w17
.Lmain_bb14:
	cmp w16, w21
	b.ge .Lmain_bb42
.Lmain_bb15:
	cmp w19, #2
	b.le .Lmain_bb39
.Lmain_bb36:
	mov w15, w22
	b .Lmain_bb16
.Lmain_bb19:
	add w15, w15, #1
	cmp w15, w21
	b.ge .Lmain_bb20
.Lmain_bb16:
	cmp w19, #2
	b.le .Lmain_bb19
.Lmain_bb17:
	adrp x23, x
	add x25, x23, :lo12:x
	adrp x17, x
	adrp x14, x
	adrp x13, x
	adrp x12, x
	adrp x11, x
	adrp x10, x
	adrp x9, x
	movn w23, #0
	movz w24, #63744
	add x17, x17, :lo12:x
	add x14, x14, :lo12:x
	add x13, x13, :lo12:x
	add x12, x12, :lo12:x
	add x11, x11, :lo12:x
	add x10, x10, :lo12:x
	add x9, x9, :lo12:x
	movk w24, #21, lsl #16
	add w26, w16, w23
	add w27, w16, #1
	smaddl x25, w16, w24, x25
	smaddl x17, w26, w24, x17
	smaddl x14, w27, w24, x14
	smaddl x13, w16, w24, x13
	smaddl x12, w16, w24, x12
	smaddl x11, w16, w24, x11
	smaddl x10, w16, w24, x10
	smaddl x9, w26, w24, x9
	movz w24, #2400
	add w27, w15, w23
	add w26, w15, #1
	smaddl x23, w15, w24, x25
	smaddl x17, w15, w24, x17
	smaddl x14, w15, w24, x14
	smaddl x13, w27, w24, x13
	smaddl x12, w26, w24, x12
	smaddl x11, w15, w24, x11
	smaddl x10, w15, w24, x10
	smaddl x9, w27, w24, x9
	add x23, x23, #4
	add x17, x17, #4
	add x7, x14, #4
	add x8, x13, #4
	add x28, x12, #4
	add x26, x10, #8
	mov x25, x9
	mov x27, x11
	mov w24, w22
.Lmain_bb18:
	ldr w14, [x17], #4
	ldr w9, [x7], #4
	ldr w13, [x8], #4
	ldr w12, [x28], #4
	ldr w11, [x27], #4
	ldr w10, [x26], #4
	add w14, w14, w9
	ldr w9, [x25], #4
	add w13, w14, w13
	add w12, w13, w12
	add w11, w12, w11
	add w10, w11, w10
	add w9, w10, w9
	sdiv w9, w9, w20
	add w24, w24, #1
	str w9, [x23], #4
	cmp w24, w21
	b.lt .Lmain_bb18
	b .Lmain_bb19
.Lmain_bb20:
	add w16, w16, #1
	b .Lmain_bb14
.Lmain_bb22:
	sub w26, w19, #3
	movz w27, #0
	lsl w25, w26, #1
	mov w12, w27
	movz w24, #1
.Lmain_bb23:
	cmp w25, w12
	b.lt .Lmain_bb43
.Lmain_bb25:
	sub w9, w12, w26
	cmp w9, #0
	csel w1, w9, w27, gt
	add w22, w12, #1
	cmp w22, w23
	adrp x11, __sysy_par_ctx_1_0
	adrp x10, __sysy_par_ctx_1_1
	adrp x9, __sysy_par_ctx_1_2
	str w12, [x11, :lo12:__sysy_par_ctx_1_0]
	csel w2, w22, w23, lt
	str w23, [x10, :lo12:__sysy_par_ctx_1_1]
	mov w0, w24
	str w20, [x9, :lo12:__sysy_par_ctx_1_2]
	bl __sysy_parallel_for
	mov w12, w22
	b .Lmain_bb23
.Lmain_bb24:
	movz w0, #53
	bl _sysy_stoptime
	adrp x9, x
	add x9, x9, :lo12:x
	mov w0, w19
	mov x1, x9
	bl putarray
	adrp x9, x
	add w10, w19, w19, lsr #31
	add x9, x9, :lo12:x
	asr w11, w10, #1
	mov x10, x9
	movz w9, #63744
	movk w9, #21, lsl #16
	smaddl x10, w11, w9, x10
	movz w9, #2400
	smaddl x9, w11, w9, x10
	mov w0, w19
	mov x1, x9
	bl putarray
	adrp x9, x
	add x10, x9, :lo12:x
	movz w9, #63744
	sub w11, w21, #1
	movk w9, #21, lsl #16
	smaddl x10, w11, w9, x10
	sub w11, w20, #1
	movz w9, #2400
	smaddl x9, w11, w9, x10
	mov w0, w19
	mov x1, x9
	bl putarray
	ldp x27, x28, [sp, #64]
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	movz w0, #0
	add sp, sp, #80
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb33:
	mov w14, w22
	b .Lmain_bb12
.Lmain_bb39:
	mov w15, w22
	b .Lmain_bb20
.Lmain_bb41:
	mov w21, w17
	mov w20, w14
	b .Lmain_bb24
.Lmain_bb42:
	mov w21, w16
	mov w20, w15
	b .Lmain_bb24
.Lmain_bb43:
	mov w20, w21
	b .Lmain_bb24
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x9, __sysy_par_ctx_0_0
	ldr w15, [x9, :lo12:__sysy_par_ctx_0_0]
	mov w8, w0
	mov w16, w1
	movz w17, #0
.L__sysy_par_body_0_bb1:
	cmp w8, w16
	b.ge .L__sysy_par_body_0_bb13
.L__sysy_par_body_0_bb14:
	movz w13, #63744
	mov w14, w17
	movk w13, #21, lsl #16
	movz w12, #2400
.L__sysy_par_body_0_bb2:
	cmp w14, w15
	b.ge .L__sysy_par_body_0_bb12
.L__sysy_par_body_0_bb3:
	adrp x10, x
	adrp x9, y
	add x10, x10, :lo12:x
	add x9, x9, :lo12:y
	smaddl x10, w8, w13, x10
	smaddl x9, w8, w13, x9
	smaddl x10, w14, w12, x10
	smaddl x9, w14, w12, x9
	movi v17.4s, #1
	movi v16.4s, #0
	cmp w15, #15
	sub w7, w15, #8
	b.le .L__sysy_par_body_0_bb16
.L__sysy_par_body_0_bb15:
	mov w11, w17
.L__sysy_par_body_0_bb4:
	cmp w11, w7
	b.gt .L__sysy_par_body_0_bb17
.L__sysy_par_body_0_bb5:
	str q17, [x10]
	str q16, [x9]
	str q17, [x10, #16]
	str q16, [x9, #16]
	add w11, w11, #8
	add x10, x10, #32
	add x9, x9, #32
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb6:
	adrp x10, x
	add x11, x10, :lo12:x
	adrp x9, y
	movz w10, #63744
	add x9, x9, :lo12:y
	movk w10, #21, lsl #16
	smaddl x11, w8, w10, x11
	smaddl x9, w8, w10, x9
	movz w10, #2400
	smaddl x11, w14, w10, x11
	smaddl x9, w14, w10, x9
	add x4, x11, w7, sxtw #2
	add x3, x9, w7, sxtw #2
	mov w11, w7
	sub w2, w15, #3
	orr w5, wzr, #0x80000003
	movz w6, #1
	movz w7, #0
.L__sysy_par_body_0_bb7:
	cmp w11, w2
	cset w10, lt
	cmp w15, w5
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_0_bb18
.L__sysy_par_body_0_bb11:
	str w6, [x4]
	str w7, [x3]
	str w6, [x4, #4]
	str w7, [x3, #4]
	str w6, [x4, #8]
	add x10, x4, #4
	str w7, [x3, #8]
	add x9, x3, #4
	str w6, [x4, #12]
	add x10, x10, #4
	add x9, x9, #4
	str w7, [x3, #12]
	add x10, x10, #4
	add x9, x9, #4
	add w11, w11, #4
	add x4, x10, #4
	add x3, x9, #4
	b .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb8:
	cmp w11, w15
	b.ge .L__sysy_par_body_0_bb10
.L__sysy_par_body_0_bb9:
	str w6, [x10], #4
	add w11, w11, #1
	str w7, [x9], #4
	b .L__sysy_par_body_0_bb8
.L__sysy_par_body_0_bb10:
	add w14, w14, #1
	b .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb12:
	add w8, w8, #1
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb13:
	ret
.L__sysy_par_body_0_bb16:
	mov w7, w17
	b .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb17:
	mov w7, w11
	b .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb18:
	mov x9, x3
	mov x10, x4
	movz w6, #1
	movz w7, #0
	b .L__sysy_par_body_0_bb8
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.p2align 2
	.global __sysy_par_body_1
	.type __sysy_par_body_1, %function
__sysy_par_body_1:
	sub sp, sp, #32
	stp x19, x20, [sp]
	adrp x11, __sysy_par_ctx_1_0
	str x21, [sp, #16]
	adrp x10, __sysy_par_ctx_1_1
	adrp x9, __sysy_par_ctx_1_2
	ldr w17, [x11, :lo12:__sysy_par_ctx_1_0]
	ldr w16, [x10, :lo12:__sysy_par_ctx_1_1]
	ldr w15, [x9, :lo12:__sysy_par_ctx_1_2]
	mov w4, w0
	mov w8, w1
	movz w7, #0
.L__sysy_par_body_1_bb1:
	cmp w4, w8
	b.ge .L__sysy_par_body_1_bb2
.L__sysy_par_body_1_bb3:
	adrp x5, x
	adrp x6, x
	adrp x14, x
	adrp x13, x
	adrp x12, x
	adrp x11, x
	adrp x10, x
	adrp x9, x
	add x2, x5, :lo12:x
	movz w3, #63744
	add x6, x6, :lo12:x
	add x14, x14, :lo12:x
	add x13, x13, :lo12:x
	add x12, x12, :lo12:x
	add x11, x11, :lo12:x
	add x10, x10, :lo12:x
	add x9, x9, :lo12:x
	add w5, w4, #1
	movk w3, #21, lsl #16
	add w1, w4, #2
	smaddl x2, w5, w3, x2
	smaddl x6, w4, w3, x6
	smaddl x14, w1, w3, x14
	smaddl x13, w5, w3, x13
	smaddl x12, w5, w3, x12
	smaddl x11, w5, w3, x11
	smaddl x10, w5, w3, x10
	smaddl x9, w4, w3, x9
	add w3, w17, #1
	add w1, w17, #2
	sub w19, w3, w4
	movz w3, #2400
	sub w0, w17, w4
	sub w1, w1, w4
	smaddl x4, w19, w3, x2
	smaddl x6, w19, w3, x6
	smaddl x14, w19, w3, x14
	smaddl x13, w0, w3, x13
	smaddl x12, w1, w3, x12
	smaddl x11, w19, w3, x11
	smaddl x10, w19, w3, x10
	smaddl x9, w0, w3, x9
	add x4, x4, #4
	add x6, x6, #4
	add x21, x14, #4
	add x20, x13, #4
	add x19, x12, #4
	add x1, x10, #8
	mov x2, x9
	mov x0, x11
	mov w3, w7
.L__sysy_par_body_1_bb4:
	cmp w3, w16
	b.ge .L__sysy_par_body_1_bb6
.L__sysy_par_body_1_bb5:
	ldr w14, [x6], #4
	ldr w9, [x21], #4
	ldr w13, [x20], #4
	ldr w12, [x19], #4
	ldr w11, [x0], #4
	ldr w10, [x1], #4
	add w14, w14, w9
	ldr w9, [x2], #4
	add w13, w14, w13
	add w12, w13, w12
	add w11, w12, w11
	add w10, w11, w10
	add w9, w10, w9
	sdiv w9, w9, w15
	str w9, [x4], #4
	add w3, w3, #1
	b .L__sysy_par_body_1_bb4
.L__sysy_par_body_1_bb2:
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	add sp, sp, #32
	ret
.L__sysy_par_body_1_bb6:
	mov w4, w5
	b .L__sysy_par_body_1_bb1
	.size __sysy_par_body_1, .-__sysy_par_body_1
	.bss
	.global x
	.p2align 4
x:
	.zero 864000000
	.global y
	.p2align 4
y:
	.zero 864000000
	.global __sysy_par_ctx_0_0
	.p2align 2
__sysy_par_ctx_0_0:
	.zero 4
	.global __sysy_par_ctx_1_0
	.p2align 2
__sysy_par_ctx_1_0:
	.zero 4
	.global __sysy_par_ctx_1_1
	.p2align 2
__sysy_par_ctx_1_1:
	.zero 4
	.global __sysy_par_ctx_1_2
	.p2align 2
__sysy_par_ctx_1_2:
	.zero 4

	.text
	.align 2
	.global __sysy_par_dispatch
__sysy_par_dispatch:
	cmp w0, #0
	b.eq .Lsysy_disp_0
	cmp w0, #1
	b.eq .Lsysy_disp_1
	ret
.Lsysy_disp_0:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_0
.Lsysy_disp_1:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_1

	.arch armv8-a
	.file	"par_runtime_only.c"
	.text
	.align	2
	.p2align 4,,11
	.type	__sysy_bind_cpu.part.0, %function
__sysy_bind_cpu.part.0:
.LFB3:
	.cfi_startproc
	adrp	x5, .LANCHOR0
	mov	x1, 0
	add	x5, x5, :lo12:.LANCHOR0
	mov	w4, 0
	b	.L5
	.p2align 2,,3
.L3:
	add	w4, w4, 1
.L2:
	cmp	x1, 1024
	beq	.L15
.L5:
	lsr	x2, x1, 6
	and	w3, w1, 63
	add	x1, x1, 1
	lsl	x6, x2, 3
	ldr	x2, [x5, x2, lsl 3]
	lsr	x2, x2, x3
	tbz	x2, 0, .L2
	cmp	w0, w4
	bne	.L3
	stp	x29, x30, [sp, -144]!
	.cfi_def_cfa_offset 144
	.cfi_offset 29, -144
	.cfi_offset 30, -136
	mov	x4, 1
	lsl	x4, x4, x3
	movi	v0.4s, 0
	add	x2, sp, 16
	mov	x29, sp
	mov	x1, 128
	mov	w0, 0
	stp	q0, q0, [x2]
	stp	q0, q0, [x2, 32]
	stp	q0, q0, [x2, 64]
	stp	q0, q0, [x2, 96]
	ldr	x3, [x2, x6]
	orr	x3, x3, x4
	str	x3, [x2, x6]
	bl	sched_setaffinity
	ldp	x29, x30, [sp], 144
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa_offset 0
	ret
	.p2align 2,,3
.L15:
	ret
	.cfi_endproc
.LFE3:
	.size	__sysy_bind_cpu.part.0, .-__sysy_bind_cpu.part.0
	.align	2
	.p2align 4,,11
	.type	__sysy_worker, %function
__sysy_worker:
.LFB1:
	.cfi_startproc
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -32
	.cfi_offset 20, -24
	adrp	x20, .LANCHOR0
	add	x0, x20, :lo12:.LANCHOR0
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -16
	.cfi_offset 22, -8
	ldr	w0, [x0, 128]
	cbz	w0, .L17
	mov	w0, 3
	bl	__sysy_bind_cpu.part.0
.L17:
	add	x20, x20, :lo12:.LANCHOR0
	mov	w19, 0
	add	x21, x20, 132
	add	x22, x20, 148
	.p2align 3,,7
.L18:
	ldar	w0, [x21]
	cmp	w0, w19
	beq	.L18
.L25:
	ldr	w0, [x20, 136]
	add	w19, w19, 1
	ldr	w1, [x20, 140]
	ldr	w2, [x20, 144]
	bl	__sysy_par_dispatch
	stlr	w19, [x22]
	ldar	w0, [x21]
	cmp	w0, w19
	beq	.L18
	b	.L25
	.cfi_endproc
.LFE1:
	.size	__sysy_worker, .-__sysy_worker
	.align	2
	.p2align 4,,11
	.global	__sysy_parallel_for
	.type	__sysy_parallel_for, %function
__sysy_parallel_for:
.LFB2:
	.cfi_startproc
	stp	x29, x30, [sp, -80]!
	.cfi_def_cfa_offset 80
	.cfi_offset 29, -80
	.cfi_offset 30, -72
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	.cfi_offset 19, -64
	.cfi_offset 20, -56
	mov	w20, w2
	stp	x21, x22, [sp, 32]
	.cfi_offset 21, -48
	.cfi_offset 22, -40
	mov	w21, w1
	mov	w22, w0
	stp	x23, x24, [sp, 48]
	.cfi_offset 23, -32
	.cfi_offset 24, -24
	sub	w23, w2, w1
	cmp	w23, 1
	ble	.L33
	adrp	x19, .LANCHOR0
	add	x24, x19, :lo12:.LANCHOR0
	ldr	w0, [x24, 152]
	cbz	w0, .L28
	ldr	w0, [x24, 156]
.L29:
	cbz	w0, .L33
	add	x19, x19, :lo12:.LANCHOR0
	add	w2, w21, w23, asr 1
	mov	x0, x19
	str	w22, [x19, 136]
	str	w2, [x19, 140]
	str	w20, [x19, 144]
	ldr	w20, [x0, 132]!
	add	w20, w20, 1
	stlr	w20, [x0]
	mov	w0, w22
	mov	w1, w21
	bl	__sysy_par_dispatch
	add	x0, x19, 148
	.p2align 3,,7
.L34:
	ldar	w1, [x0]
	cmp	w1, w20
	bne	.L34
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa_offset 0
	ret
	.p2align 2,,3
.L33:
	.cfi_restore_state
	mov	w2, w20
	mov	w1, w21
	mov	w0, w22
	ldp	x19, x20, [sp, 16]
	ldp	x21, x22, [sp, 32]
	ldp	x23, x24, [sp, 48]
	ldp	x29, x30, [sp], 80
	.cfi_remember_state
	.cfi_restore 30
	.cfi_restore 29
	.cfi_restore 23
	.cfi_restore 24
	.cfi_restore 21
	.cfi_restore 22
	.cfi_restore 19
	.cfi_restore 20
	.cfi_def_cfa_offset 0
	b	__sysy_par_dispatch
	.p2align 2,,3
.L28:
	.cfi_restore_state
	mov	x2, x24
	mov	x1, 128
	mov	w0, 0
	str	x25, [sp, 64]
	.cfi_offset 25, -16
	mov	w25, 1
	str	w25, [x24, 152]
	bl	sched_getaffinity
	cbnz	w0, .L30
	str	w25, [x24, 128]
.L31:
	mov	w0, 2
	bl	__sysy_bind_cpu.part.0
	b	.L32
	.p2align 2,,3
.L30:
	ldr	w0, [x24, 128]
	cbnz	w0, .L31
.L32:
	mov	w2, 3840
	adrp	x0, __sysy_worker
	movk	w2, 0x5, lsl 16
	add	x0, x0, :lo12:__sysy_worker
	adrp	x1, __sysy_wstack+1048576
	mov	x3, 0
	add	x1, x1, :lo12:__sysy_wstack+1048576
	bl	clone
	add	x1, x19, :lo12:.LANCHOR0
	cmp	w0, 0
	cset	w0, gt
	ldr	x25, [sp, 64]
	.cfi_restore 25
	str	w0, [x1, 156]
	b	.L29
	.cfi_endproc
.LFE2:
	.size	__sysy_parallel_for, .-__sysy_parallel_for
	.bss
	.align	4
	.set	.LANCHOR0,. + 0
	.type	__sysy_orig_mask, %object
	.size	__sysy_orig_mask, 128
__sysy_orig_mask:
	.zero	128
	.type	__sysy_orig_mask_valid, %object
	.size	__sysy_orig_mask_valid, 4
__sysy_orig_mask_valid:
	.zero	4
	.type	__sysy_job_seq, %object
	.size	__sysy_job_seq, 4
__sysy_job_seq:
	.zero	4
	.type	__sysy_job_id, %object
	.size	__sysy_job_id, 4
__sysy_job_id:
	.zero	4
	.type	__sysy_job_lo, %object
	.size	__sysy_job_lo, 4
__sysy_job_lo:
	.zero	4
	.type	__sysy_job_hi, %object
	.size	__sysy_job_hi, 4
__sysy_job_hi:
	.zero	4
	.type	__sysy_done_seq, %object
	.size	__sysy_done_seq, 4
__sysy_done_seq:
	.zero	4
	.type	__sysy_worker_started, %object
	.size	__sysy_worker_started, 4
__sysy_worker_started:
	.zero	4
	.type	__sysy_worker_ok, %object
	.size	__sysy_worker_ok, 4
__sysy_worker_ok:
	.zero	4
	.type	__sysy_wstack, %object
	.size	__sysy_wstack, 1048576
__sysy_wstack:
	.zero	1048576
	.ident	"GCC: (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0"
	.section	.note.GNU-stack,"",@progbits
