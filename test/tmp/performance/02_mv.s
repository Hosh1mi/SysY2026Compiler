	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #64
	str x25, [sp, #48]
	movz w25, #0
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	bl getint
	mov w19, w0
	mov w21, w25
.Lmain_bb1:
	cmp w21, w19
	b.ge .Lmain_bb2
.Lmain_bb9:
	adrp x9, A
	add x10, x9, :lo12:A
	movz w9, #8040
	smaddl x9, w21, w9, x10
	mov x23, x9
	mov w22, w25
.Lmain_bb10:
	cmp w22, w19
	b.ge .Lmain_bb12
.Lmain_bb11:
	bl getint
	str w0, [x23], #4
	add w22, w22, #1
	b .Lmain_bb10
.Lmain_bb2:
	adrp x9, B
	add x21, x9, :lo12:B
	mov x23, x21
	mov w22, w25
.Lmain_bb3:
	cmp w22, w19
	b.ge .Lmain_bb5
.Lmain_bb4:
	bl getint
	str w0, [x23], #4
	add w22, w22, #1
	b .Lmain_bb3
.Lmain_bb5:
	movz w0, #59
	bl _sysy_starttime
	adrp x10, A
	adrp x9, C
	add x23, x10, :lo12:A
	add x22, x9, :lo12:C
	movz w24, #1
	movz w20, #0
.Lmain_bb6:
	cmp w25, #50
	b.ge .Lmain_bb8
.Lmain_bb7:
	adrp x12, __sysy_par_ctx_1_0
	adrp x11, __sysy_par_ctx_1_1
	adrp x10, __sysy_par_ctx_1_2
	adrp x9, __sysy_par_ctx_1_3
	str x22, [x12, :lo12:__sysy_par_ctx_1_0]
	mov w0, w24
	str w19, [x11, :lo12:__sysy_par_ctx_1_1]
	mov w1, w20
	str x23, [x10, :lo12:__sysy_par_ctx_1_2]
	mov w2, w19
	str x21, [x9, :lo12:__sysy_par_ctx_1_3]
	bl __sysy_parallel_for
	adrp x12, __sysy_par_ctx_0_0
	adrp x11, __sysy_par_ctx_0_1
	adrp x10, __sysy_par_ctx_0_2
	adrp x9, __sysy_par_ctx_0_3
	str x21, [x12, :lo12:__sysy_par_ctx_0_0]
	mov w0, w20
	str w19, [x11, :lo12:__sysy_par_ctx_0_1]
	mov w1, w20
	str x23, [x10, :lo12:__sysy_par_ctx_0_2]
	mov w2, w19
	str x22, [x9, :lo12:__sysy_par_ctx_0_3]
	bl __sysy_parallel_for
	add w25, w25, #1
	b .Lmain_bb6
.Lmain_bb8:
	movz w0, #67
	bl _sysy_stoptime
	mov w0, w19
	mov x1, x22
	bl putarray
	ldp x24, x25, [sp, #40]
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #64
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb12:
	add w21, w21, #1
	b .Lmain_bb1
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x12, __sysy_par_ctx_0_0
	adrp x11, __sysy_par_ctx_0_1
	adrp x10, __sysy_par_ctx_0_2
	adrp x9, __sysy_par_ctx_0_3
	ldr x7, [x12, :lo12:__sysy_par_ctx_0_0]
	ldr w8, [x11, :lo12:__sysy_par_ctx_0_1]
	ldr x17, [x10, :lo12:__sysy_par_ctx_0_2]
	ldr x16, [x9, :lo12:__sysy_par_ctx_0_3]
	mov w3, w0
	mov w6, w1
	movz w5, #0
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb7:
	cmp w11, w8
	add w3, w3, #1
	b.lt .L__sysy_par_body_0_bb19
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb8:
	cmp w12, w8
	b.lt .L__sysy_par_body_0_bb9
.L__sysy_par_body_0_bb1:
	cmp w3, w6
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	add x15, x7, w3, sxtw #2
	movz w9, #0
	str w9, [x15]
	movz w9, #8040
	ldr w12, [x15]
	smaddl x9, w3, w9, x17
	mov x1, x16
	sub w2, w8, #1
	mov x13, x9
	mov w11, w5
	orr w4, wzr, #0x80000001
.L__sysy_par_body_0_bb4:
	cmp w11, w2
	cset w10, lt
	cmp w8, w4
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_0_bb5
.L__sysy_par_body_0_bb12:
	ldr w10, [x13]
	cbz w10, .L__sysy_par_body_0_bb14
.L__sysy_par_body_0_bb13:
	ldr w9, [x1]
	madd w12, w10, w9, w12
.L__sysy_par_body_0_bb14:
	ldr w10, [x13, #4]
	add x14, x13, #4
	add x13, x1, #4
	cbz w10, .L__sysy_par_body_0_bb16
.L__sysy_par_body_0_bb15:
	ldr w9, [x1, #4]
	madd w12, w10, w9, w12
.L__sysy_par_body_0_bb16:
	add x10, x14, #4
	add x1, x13, #4
	add w11, w11, #2
	mov x13, x10
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb2:
	ret
.L__sysy_par_body_0_bb5:
	ldr w9, [x15]
	cmp w12, w9
	b.eq .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb6:
	str w12, [x15]
	b .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb19:
	mov x14, x1
	mov w12, w11
	b .L__sysy_par_body_0_bb8
.L__sysy_par_body_0_bb9:
	ldr w11, [x13]
	cbz w11, .L__sysy_par_body_0_bb11
.L__sysy_par_body_0_bb10:
	ldr w10, [x15]
	ldr w9, [x14]
	madd w9, w11, w9, w10
	str w9, [x15]
.L__sysy_par_body_0_bb11:
	add w12, w12, #1
	add x13, x13, #4
	add x14, x14, #4
	b .L__sysy_par_body_0_bb8
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.p2align 2
	.global __sysy_par_body_1
	.type __sysy_par_body_1, %function
__sysy_par_body_1:
	adrp x12, __sysy_par_ctx_1_0
	adrp x11, __sysy_par_ctx_1_1
	adrp x10, __sysy_par_ctx_1_2
	adrp x9, __sysy_par_ctx_1_3
	ldr x7, [x12, :lo12:__sysy_par_ctx_1_0]
	ldr w8, [x11, :lo12:__sysy_par_ctx_1_1]
	ldr x17, [x10, :lo12:__sysy_par_ctx_1_2]
	ldr x16, [x9, :lo12:__sysy_par_ctx_1_3]
	mov w3, w0
	mov w6, w1
	movz w5, #0
	b .L__sysy_par_body_1_bb1
.L__sysy_par_body_1_bb7:
	cmp w11, w8
	add w3, w3, #1
	b.lt .L__sysy_par_body_1_bb19
	b .L__sysy_par_body_1_bb1
.L__sysy_par_body_1_bb8:
	cmp w12, w8
	b.lt .L__sysy_par_body_1_bb9
.L__sysy_par_body_1_bb1:
	cmp w3, w6
	b.ge .L__sysy_par_body_1_bb2
.L__sysy_par_body_1_bb3:
	add x15, x7, w3, sxtw #2
	movz w9, #0
	str w9, [x15]
	movz w9, #8040
	ldr w12, [x15]
	smaddl x9, w3, w9, x17
	mov x1, x16
	sub w2, w8, #1
	mov x13, x9
	mov w11, w5
	orr w4, wzr, #0x80000001
.L__sysy_par_body_1_bb4:
	cmp w11, w2
	cset w10, lt
	cmp w8, w4
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_1_bb5
.L__sysy_par_body_1_bb12:
	ldr w10, [x13]
	cbz w10, .L__sysy_par_body_1_bb14
.L__sysy_par_body_1_bb13:
	ldr w9, [x1]
	madd w12, w10, w9, w12
.L__sysy_par_body_1_bb14:
	ldr w10, [x13, #4]
	add x14, x13, #4
	add x13, x1, #4
	cbz w10, .L__sysy_par_body_1_bb16
.L__sysy_par_body_1_bb15:
	ldr w9, [x1, #4]
	madd w12, w10, w9, w12
.L__sysy_par_body_1_bb16:
	add x10, x14, #4
	add x1, x13, #4
	add w11, w11, #2
	mov x13, x10
	b .L__sysy_par_body_1_bb4
.L__sysy_par_body_1_bb2:
	ret
.L__sysy_par_body_1_bb5:
	ldr w9, [x15]
	cmp w12, w9
	b.eq .L__sysy_par_body_1_bb7
.L__sysy_par_body_1_bb6:
	str w12, [x15]
	b .L__sysy_par_body_1_bb7
.L__sysy_par_body_1_bb19:
	mov x14, x1
	mov w12, w11
	b .L__sysy_par_body_1_bb8
.L__sysy_par_body_1_bb9:
	ldr w11, [x13]
	cbz w11, .L__sysy_par_body_1_bb11
.L__sysy_par_body_1_bb10:
	ldr w10, [x15]
	ldr w9, [x14]
	madd w9, w11, w9, w10
	str w9, [x15]
.L__sysy_par_body_1_bb11:
	add w12, w12, #1
	add x13, x13, #4
	add x14, x14, #4
	b .L__sysy_par_body_1_bb8
	.size __sysy_par_body_1, .-__sysy_par_body_1
	.bss
	.global A
	.p2align 4
A:
	.zero 16160400
	.global B
	.p2align 4
B:
	.zero 8040
	.global C
	.p2align 4
C:
	.zero 8040
	.global __sysy_par_ctx_0_0
	.p2align 3
__sysy_par_ctx_0_0:
	.zero 8
	.global __sysy_par_ctx_0_1
	.p2align 2
__sysy_par_ctx_0_1:
	.zero 4
	.global __sysy_par_ctx_0_2
	.p2align 3
__sysy_par_ctx_0_2:
	.zero 8
	.global __sysy_par_ctx_0_3
	.p2align 3
__sysy_par_ctx_0_3:
	.zero 8
	.global __sysy_par_ctx_1_0
	.p2align 3
__sysy_par_ctx_1_0:
	.zero 8
	.global __sysy_par_ctx_1_1
	.p2align 2
__sysy_par_ctx_1_1:
	.zero 4
	.global __sysy_par_ctx_1_2
	.p2align 3
__sysy_par_ctx_1_2:
	.zero 8
	.global __sysy_par_ctx_1_3
	.p2align 3
__sysy_par_ctx_1_3:
	.zero 8

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
