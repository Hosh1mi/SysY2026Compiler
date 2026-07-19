	.text
	.global main
	.p2align 2
main:
	stp x29, x30, [sp, #-16]!
	sub sp, sp, #96
	stp x22, x21, [sp, #16]
	adrp x21, a
	stp x20, x19, [sp, #0]
	stp d8, d9, [sp, #32]
	str d10, [sp, #48]
	add x21, x21, :lo12:a
	movz w20, #0
main_label_while_cond_1:
	cmp w20, #1000
	b.lt main_label_while_body_2
main_label_while_end_3:
	movz w0, #23
	bl _sysy_starttime
	mov w0, wzr
	mov w1, wzr
	movz w2, #1000
	bl __sysy_parallel_for
	movz w0, #1
	mov w1, wzr
	movz w2, #1000
	bl __sysy_parallel_for
	movz w0, #2
	mov w1, wzr
	movz w2, #1000
	bl __sysy_parallel_for
	movz w19, #0
main_label_while_cond_34:
	cmp w19, #1000
	b.lt main_label_while_cond_37.preheader
	movz w22, #0
	movz w7, #0
main_label_while_cond_40:
	cmp w7, #1000
	b.lt main_label_while_cond_43.preheader
main_label_while_end_42:
	movz w0, #93
	bl _sysy_stoptime
	mov w0, w22
	bl putint
	movz w0, #0
main_label_ret:
	b .Lmain_epilogue
main_label_while_body_2:
	mov	x0, x21
	bl getarray
	cmp w0, #1000
	b.eq main_label_if_else_5
	b	.Lmain_epilogue
main_label_if_else_5:
	add w20, w20, #1
	add x21, x21, #4000
	b main_label_while_cond_1
main_label_while_cond_37.preheader:
	adrp x0, c
	add x0, x0, :lo12:c
	uxtw x10, w19
	movz x11, #4000
	madd	x0, x10, x11, x0
	movz w1, #0
main_label_144:
	cmp w1, #997
	b.lt main_label_148
main_label_while_cond_37:
	cmp w1, #1000
	b.lt main_label_while_body_38
main_label_while_end_39:
	add w19, w19, #1
	b main_label_while_cond_34
main_label_while_body_38:
	adrp x9, c
	add x9, x9, :lo12:c
	uxtw x10, w1
	movz x11, #4000
	madd	x9, x10, x11, x9
	add w1, w1, #1
	add x9, x9, w19, uxtw #2
	ldr w9, [x9]
	sub w9, wzr, w9
	str	w9, [x0], #4
	b main_label_while_cond_37
main_label_while_cond_43.preheader:
	adrp x6, c
	add x6, x6, :lo12:c
	uxtw x10, w7
	movz x11, #4000
	madd	x6, x10, x11, x6
	movi	v10.4s, #0
	mov v10.s[0], w22
	mov w5, wzr
main_label_191:
	cmp w5, #993
	b.lt main_label_196
main_label_110:
	cmp w5, #997
	b.lt main_label_114
main_label_120:
	adrp x3, c
	addv s0, v10.4s
	add x3, x3, :lo12:c
	uxtw x10, w7
	movz x11, #4000
	madd	x3, x10, x11, x3
	fmov w4, s0
	add x3, x3, w5, uxtw #2
main_label_169:
	cmp w5, #997
	b.lt main_label_174
main_label_while_cond_43.preheader.1:
	movi	v9.4s, #0
	mov v9.s[0], w4
main_label_222:
	cmp w5, #997
	b.lt main_label_227
main_label_233:
	addv s0, v9.4s
	fmov w22, s0
main_label_while_cond_43:
	cmp w5, #1000
	b.lt main_label_while_body_44
main_label_while_end_45:
	add w7, w7, #1
	b main_label_while_cond_40
main_label_while_body_44:
	ldr	w9, [x3], #4
	add w5, w5, #1
	add w22, w22, w9
	b main_label_while_cond_43
main_label_114:
	ldr	q8, [x6], #16
	add w5, w5, #4
	add v10.4s, v10.4s, v8.4s
	b main_label_110
main_label_148:
	adrp x9, c
	add x9, x9, :lo12:c
	uxtw x10, w1
	movz x11, #4000
	madd	x9, x10, x11, x9
	adrp x10, c
	add x10, x10, :lo12:c
	add x9, x9, w19, uxtw #2
	ldr w9, [x9]
	movz x12, #4000
	sub w9, wzr, w9
	str	w9, [x0], #4
	add w9, w1, #1
	uxtw x11, w9
	madd	x10, x11, x12, x10
	movz x12, #4000
	add	x9, x10, w19, uxtw #2
	ldr w9, [x9]
	adrp x10, c
	add x10, x10, :lo12:c
	sub w9, wzr, w9
	str	w9, [x0], #4
	add w9, w1, #2
	sxtw x11, w9
	madd	x10, x11, x12, x10
	movz x12, #4000
	add	x9, x10, w19, uxtw #2
	ldr w9, [x9]
	adrp x10, c
	add x10, x10, :lo12:c
	sub w9, wzr, w9
	str	w9, [x0], #4
	add w9, w1, #3
	sxtw x11, w9
	madd	x10, x11, x12, x10
	add w1, w1, #4
	add	x9, x10, w19, uxtw #2
	ldr w9, [x9]
	sub w9, wzr, w9
	str	w9, [x0], #4
	b main_label_144
main_label_174:
	ldr w9, [x3]
	add w5, w5, #4
	add w0, w4, w9
	add	x9, x3, #4
	ldr	w1, [x9], #4
	add w1, w0, w1
	ldr	w0, [x9], #4
	add w1, w1, w0
	ldr w0, [x9]
	add	x3, x9, #4
	add w4, w1, w0
	b main_label_169
main_label_196:
	mov x9, x6
	ldr	q8, [x9]
	add	x0, x6, #16
	mov x9, x0
	add w5, w5, #8
	add v9.4s, v10.4s, v8.4s
	ldr	q8, [x9]
	add	x6, x0, #16
	add v10.4s, v9.4s, v8.4s
	b main_label_191
main_label_227:
	ldr	q8, [x3], #16
	add w5, w5, #4
	add v9.4s, v9.4s, v8.4s
	b main_label_222
.Lmain_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp d8, d9, [sp, #32]
	ldr d10, [sp, #48]
	add sp, sp, #96
	ldp x29, x30, [sp], #16
	ret
	.global __sysy_par_body_0
	.p2align 2
__sysy_par_body_0:
	mov w5, w0
__sysy_par_body_0_label_while_cond_6:
	cmp w5, w1
	b.lt __sysy_par_body_0_label_while_cond_9.preheader
__sysy_par_body_0_label_par_ret:
	ret
__sysy_par_body_0_label_while_cond_9.preheader:
	adrp x3, b
	add x3, x3, :lo12:b
	sxtw x10, w5
	movz x11, #4000
	madd	x3, x10, x11, x3
	movz w4, #0
__sysy_par_body_0_label_5:
	cmp w4, #997
	b.lt __sysy_par_body_0_label_9
__sysy_par_body_0_label_while_cond_9:
	cmp w4, #1000
	b.lt __sysy_par_body_0_label_while_body_10
__sysy_par_body_0_label_while_end_11:
	add w5, w5, #1
	b __sysy_par_body_0_label_while_cond_6
__sysy_par_body_0_label_while_body_10:
	adrp x2, a
	add x2, x2, :lo12:a
	uxtw x10, w4
	movz x11, #4000
	madd	x2, x10, x11, x2
	add w4, w4, #1
	add x2, x2, w5, sxtw #2
	ldr w2, [x2]
	str	w2, [x3], #4
	b __sysy_par_body_0_label_while_cond_9
__sysy_par_body_0_label_9:
	adrp x2, a
	add x2, x2, :lo12:a
	uxtw x10, w4
	movz x11, #4000
	madd	x2, x10, x11, x2
	adrp x10, a
	add x10, x10, :lo12:a
	add x2, x2, w5, sxtw #2
	ldr w2, [x2]
	movz x12, #4000
	str	w2, [x3], #4
	add w2, w4, #1
	uxtw x11, w2
	madd	x10, x11, x12, x10
	movz x12, #4000
	add	x2, x10, w5, sxtw #2
	ldr w2, [x2]
	adrp x10, a
	add x10, x10, :lo12:a
	str	w2, [x3], #4
	add w2, w4, #2
	sxtw x11, w2
	madd	x10, x11, x12, x10
	movz x12, #4000
	add	x2, x10, w5, sxtw #2
	ldr w2, [x2]
	adrp x10, a
	add x10, x10, :lo12:a
	str	w2, [x3], #4
	add w2, w4, #3
	sxtw x11, w2
	madd	x10, x11, x12, x10
	add w4, w4, #4
	add	x2, x10, w5, sxtw #2
	ldr w2, [x2]
	str	w2, [x3], #4
	b __sysy_par_body_0_label_5
	.global __sysy_par_body_1
	.p2align 2
__sysy_par_body_1:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	sub sp, sp, #4080
	stp x20, x19, [sp, #0]
	stp x22, x21, [sp, #16]
	stp x24, x23, [sp, #32]
	str x25, [sp, #48]
	stp d8, d9, [sp, #56]
	str d10, [sp, #72]
	mov w25, w0
__sysy_par_body_1_label_while_cond_12:
	cmp w25, w1
	b.lt __sysy_par_body_1_label_56.preheader
__sysy_par_body_1_label_par_ret:
	b .L__sysy_par_body_1_epilogue
__sysy_par_body_1_label_56.preheader:
	movi v10.4s, #0
	movz w24, #0
__sysy_par_body_1_label_65.1:
	cmp w24, #993
	b.lt __sysy_par_body_1_label_68.1
__sysy_par_body_1_label_22:
	cmp w24, #997
	b.lt __sysy_par_body_1_label_25
__sysy_par_body_1_label_29:
	cmp w24, #1000
	b.lt __sysy_par_body_1_label_59
__sysy_par_body_1_label_32:
	adrp x23, a
	add x23, x23, :lo12:a
	sxtw x10, w25
	movz x11, #4000
	adrp x22, b
	madd	x23, x10, x11, x23
	add x22, x22, :lo12:b
	sxtw x10, w25
	movz x11, #4000
	madd	x22, x10, x11, x22
	movz w21, #0
__sysy_par_body_1_label_62:
	cmp w21, #1000
	b.lt __sysy_par_body_1_label_67
__sysy_par_body_1_label_71.loopexit:
	adrp x3, c
	add x3, x3, :lo12:c
	sxtw x10, w25
	movz x11, #4000
	madd	x3, x10, x11, x3
	movz w5, #0
__sysy_par_body_1_label_48:
	cmp w5, #993
	b.lt __sysy_par_body_1_label_52
__sysy_par_body_1_label_4:
	cmp w5, #997
	b.lt __sysy_par_body_1_label_7
__sysy_par_body_1_label_14:
	cmp w5, #1000
	b.lt __sysy_par_body_1_label_74
__sysy_par_body_1_label_17:
	add w25, w25, #1
	b __sysy_par_body_1_label_while_cond_12
__sysy_par_body_1_label_74:
	sub x2, x29, #4000
	add x2, x2, w5, uxtw #2
	ldr w4, [x2]
	adrp x2, c
	add x2, x2, :lo12:c
	sxtw x10, w25
	movz x11, #4000
	madd	x2, x10, x11, x2
	add x2, x2, w5, uxtw #2
	str w4, [x2]
	add w5, w5, #1
	b __sysy_par_body_1_label_14
__sysy_par_body_1_label_67:
	adrp x19, b
	ldr w2, [x23]
	add x19, x19, :lo12:b
	uxtw x10, w21
	movz x11, #4000
	adrp x9, a
	ldr w20, [x22]
	madd	x19, x10, x11, x19
	add x9, x9, :lo12:a
	uxtw x10, w21
	movz x11, #4000
	madd	x9, x10, x11, x9
	and w7, w2, #1
	movz w6, #0
__sysy_par_body_1_label_68:
	cmp w6, #1000
	b.lt __sysy_par_body_1_label_79
__sysy_par_body_1_label_65:
	add w21, w21, #1
	add x23, x23, #4
	add x22, x22, #4
	b __sysy_par_body_1_label_62
__sysy_par_body_1_label_79:
	ldr w2, [x19]
	sub x5, x29, #4000
	add x5, x5, w6, uxtw #2
	ldr w4, [x5]
	and w2, w2, #1
	and w3, w7, w2
	ldr w2, [x9]
	cmp w3, #0
	add w6, w6, #1
	add x19, x19, #4
	madd	w2, w20, w2, w4
	add x9, x9, #4
	csel w2, w2, w4, eq
	str w2, [x5]
	b __sysy_par_body_1_label_68
__sysy_par_body_1_label_59:
	sub x2, x29, #4000
	add x2, x2, w24, uxtw #2
	str wzr, [x2]
	add w24, w24, #1
	b __sysy_par_body_1_label_29
__sysy_par_body_1_label_7:
	sub x2, x29, #4000
	add x2, x2, w5, uxtw #2
	ldr	q8, [x2]
	add w4, w5, #4
	mov w5, w4
	str	q8, [x3], #16
	b __sysy_par_body_1_label_4
__sysy_par_body_1_label_25:
	sub x2, x29, #4000
	add w3, w24, #4
	add x2, x2, w24, uxtw #2
	str	q10, [x2]
	mov w24, w3
	b __sysy_par_body_1_label_22
__sysy_par_body_1_label_52:
	add w2, w5, #4
	sub x4, x29, #4000
	sub x10, x29, #4000
	add x4, x4, w5, uxtw #2
	add	x2, x10, w2, sxtw #2
	ldr	q9, [x4]
	ldr	q8, [x2]
	mov x4, x3
	add x3, x3, #16
	add w5, w5, #8
	str	q9, [x4]
	str	q8, [x3], #16
	b __sysy_par_body_1_label_48
__sysy_par_body_1_label_68.1:
	add w2, w24, #4
	sub x3, x29, #4000
	sub x10, x29, #4000
	add x3, x3, w24, uxtw #2
	add	x2, x10, w2, sxtw #2
	add w24, w24, #8
	str	q10, [x3]
	str	q10, [x2]
	b __sysy_par_body_1_label_65.1
.L__sysy_par_body_1_epilogue:
	ldp x20, x19, [sp, #0]
	ldp x22, x21, [sp, #16]
	ldp x24, x23, [sp, #32]
	ldr x25, [sp, #48]
	ldp d8, d9, [sp, #56]
	ldr d10, [sp, #72]
	add sp, sp, #4080
	ldp x29, x30, [sp], #16
	ret
	.global __sysy_par_body_2
	.p2align 2
__sysy_par_body_2:
	sub sp, sp, #32
	stp x20, x19, [sp, #0]
	str d8, [sp, #16]
	mov w20, w0
__sysy_par_body_2_label_while_cond_23:
	cmp w20, w1
	b.lt __sysy_par_body_2_label_while_cond_26.preheader
__sysy_par_body_2_label_par_ret:
	b .L__sysy_par_body_2_epilogue
__sysy_par_body_2_label_while_cond_26.preheader:
	adrp x19, c
	add x19, x19, :lo12:c
	sxtw x10, w20
	movz x11, #4000
	madd	x19, x10, x11, x19
	movz w7, #65535
	movk w7, #32767, lsl #16
	mov x9, x19
	movz w6, #0
__sysy_par_body_2_label_while_cond_26:
	cmp w6, #1000
	b.lt __sysy_par_body_2_label_while_body_27
__sysy_par_body_2_label_while_cond_31.loopexit:
	dup v8.4s, w7
	movz w5, #0
__sysy_par_body_2_label_23:
	cmp w5, #993
	b.lt __sysy_par_body_2_label_27
__sysy_par_body_2_label_6:
	cmp w5, #997
	b.lt __sysy_par_body_2_label_9
__sysy_par_body_2_label_13:
	cmp w5, #1000
	b.lt __sysy_par_body_2_label_while_body_32
__sysy_par_body_2_label_16:
	add w20, w20, #1
	b __sysy_par_body_2_label_while_cond_23
__sysy_par_body_2_label_while_body_32:
	adrp x2, c
	add x2, x2, :lo12:c
	sxtw x10, w20
	movz x11, #4000
	madd	x2, x10, x11, x2
	add x2, x2, w5, uxtw #2
	str w7, [x2]
	add w5, w5, #1
	b __sysy_par_body_2_label_13
__sysy_par_body_2_label_while_body_27:
	ldr	w2, [x9], #4
	add w6, w6, #1
	cmp w2, w7
	csel w7, w2, w7, lt
	b __sysy_par_body_2_label_while_cond_26
__sysy_par_body_2_label_9:
	add w5, w5, #4
	str	q8, [x19], #16
	b __sysy_par_body_2_label_6
__sysy_par_body_2_label_27:
	add	x3, x19, #16
	mov x4, x19
	mov x2, x3
	add w5, w5, #8
	str	q8, [x4]
	str	q8, [x2]
	add	x19, x3, #16
	b __sysy_par_body_2_label_23
.L__sysy_par_body_2_epilogue:
	ldp x20, x19, [sp, #0]
	ldr d8, [sp, #16]
	add sp, sp, #32
	ret
	.bss
	.global a
	.p2align 4
a:
	.zero 4000000

	.global b
	.p2align 4
b:
	.zero 4000000

	.global c
	.p2align 4
c:
	.zero 4000000


	.text
	.align 2
	.global __sysy_par_dispatch
__sysy_par_dispatch:
	cmp w0, #0
	b.eq .Lsysy_disp_0
	cmp w0, #1
	b.eq .Lsysy_disp_1
	cmp w0, #2
	b.eq .Lsysy_disp_2
	ret
.Lsysy_disp_0:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_0
.Lsysy_disp_1:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_1
.Lsysy_disp_2:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_2

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
	.global	__aarch64_ldadd4_rel
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
	mov	x1, x22
	mov	w0, 1
	bl	__aarch64_ldadd4_rel
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
	add	w23, w21, w23, asr 1
	mov	x1, x19
	mov	w0, 1
	str	w22, [x19, 136]
	str	w23, [x19, 140]
	str	w20, [x19, 144]
	ldr	w20, [x1, 132]!
	add	w20, w20, w0
	bl	__aarch64_ldadd4_rel
	mov	w0, w22
	mov	w2, w23
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
