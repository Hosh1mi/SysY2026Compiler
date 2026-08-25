	.arch armv8-a
	.text
	.p2align 2
	.global spmv
	.type spmv, %function
spmv:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #64
	stp x19, x20, [sp]
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	stp x25, x26, [sp, #48]
	mov w22, w0
	mov x24, x1
	mov x21, x2
	mov x19, x5
	adrp x9, __sysy_par_ctx_0_0
	movz w25, #0
	str x19, [x9, :lo12:__sysy_par_ctx_0_0]
	mov x20, x3
	mov x26, x4
	mov w0, w25
	mov w1, w25
	mov w2, w22
	bl __sysy_parallel_for
	cmp w22, #0
	b.le .Lspmv_bb8
.Lspmv_bb1:
	add x23, x24, #4
	mov x17, x26
	b .Lspmv_bb2
.Lspmv_bb7:
	add w25, w25, #1
	cmp w25, w22
	add x24, x24, #4
	add x23, x23, #4
	add x17, x17, #4
	b.ge .Lspmv_bb8
.Lspmv_bb2:
	ldr w11, [x24]
	add x14, x21, w11, sxtw #2
	add x15, x20, w11, sxtw #2
.Lspmv_bb3:
	ldr w9, [x23]
	cmp w11, w9
	b.ge .Lspmv_bb5
.Lspmv_bb4:
	ldr w9, [x14], #4
	add x13, x19, w9, sxtw #2
	ldr w10, [x13]
	ldr w9, [x15], #4
	add w12, w10, w9
	str w12, [x13]
	add w11, w11, #1
	b .Lspmv_bb3
.Lspmv_bb5:
	ldr w14, [x24]
	add x15, x21, w14, sxtw #2
	add x16, x20, w14, sxtw #2
.Lspmv_bb6:
	ldr w9, [x23]
	cmp w14, w9
	b.ge .Lspmv_bb7
.Lspmv_bb9:
	ldr w10, [x15], #4
	ldr w9, [x17]
	add x13, x19, w10, sxtw #2
	ldr w11, [x13]
	ldr w10, [x16], #4
	sub w9, w9, #1
	madd w12, w10, w9, w11
	add w14, w14, #1
	str w12, [x13]
	b .Lspmv_bb6
.Lspmv_bb8:
	ldp x25, x26, [sp, #48]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #64
	ldp xzr, x30, [sp], #16
	ret
	.size spmv, .-spmv
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #64
	stp x19, x20, [sp]
	adrp x9, x
	add x20, x9, :lo12:x
	stp x21, x22, [sp, #16]
	movz w19, #0
	stp x23, x24, [sp, #32]
	mov x0, x20
	str x25, [sp, #48]
	bl getarray
	adrp x9, y
	add x22, x9, :lo12:y
	sub w21, w0, #1
	mov x0, x22
	bl getarray
	adrp x9, v
	add x23, x9, :lo12:v
	mov x0, x23
	bl getarray
	adrp x9, a
	add x24, x9, :lo12:a
	mov x0, x24
	bl getarray
	movz w0, #39
	bl _sysy_starttime
	adrp x9, b
	add x25, x9, :lo12:b
.Lmain_bb1:
	cmp w19, #100
	b.ge .Lmain_bb3
.Lmain_bb2:
	mov w0, w21
	mov x1, x20
	mov x2, x22
	mov x3, x23
	mov x4, x24
	mov x5, x25
	bl spmv
	mov w0, w21
	mov x1, x20
	mov x2, x22
	mov x3, x23
	mov x4, x25
	mov x5, x24
	bl spmv
	add w19, w19, #1
	b .Lmain_bb1
.Lmain_bb3:
	movz w0, #47
	bl _sysy_stoptime
	mov w0, w21
	mov x1, x25
	bl putarray
	ldp x24, x25, [sp, #40]
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #64
	ldp xzr, x30, [sp], #16
	ret
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x10, __sysy_par_ctx_0_0
	ldr x17, [x10, :lo12:__sysy_par_ctx_0_0]
	mov w12, w0
	orr w9, wzr, #0x7ffffff0
	cmp w12, w9
	mov w16, w1
	cset w10, le
	add w9, w12, #15
	cmp w9, w16
	movi v16.4s, #0
	cset w9, lt
	and w9, w10, w9
	sub w15, w16, #8
	cbz w9, .L__sysy_par_body_0_bb13
.L__sysy_par_body_0_bb1:
	movz w11, #16
	add x13, x17, w12, sxtw #2
	sub w14, w16, #16
	movk w11, #32768, lsl #16
.L__sysy_par_body_0_bb2:
	cmp w12, w14
	cset w10, le
	cmp w16, w11
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_0_bb12
.L__sysy_par_body_0_bb5:
	add x9, x13, #32
	stp q16, q16, [x13]
	stp q16, q16, [x13, #32]
	add w12, w12, #16
	add x13, x9, #32
	b .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	cmp w10, w15
	b.gt .L__sysy_par_body_0_bb14
.L__sysy_par_body_0_bb4:
	stp q16, q16, [x9]
	add w10, w10, #8
	add x9, x9, #32
	b .L__sysy_par_body_0_bb3
.L__sysy_par_body_0_bb6:
	add x14, x17, w13, sxtw #2
	sub w15, w16, #3
	orr w12, wzr, #0x80000003
	movz w11, #0
.L__sysy_par_body_0_bb7:
	cmp w13, w15
	cset w10, lt
	cmp w16, w12
	cset w9, ge
	and w9, w9, w10
	cbz w9, .L__sysy_par_body_0_bb15
.L__sysy_par_body_0_bb11:
	add x9, x14, #4
	add x9, x9, #4
	stp w11, w11, [x14]
	add x9, x9, #4
	stp w11, w11, [x14, #8]
	add w13, w13, #4
	add x14, x9, #4
	b .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb8:
	cmp w10, w16
	b.ge .L__sysy_par_body_0_bb9
.L__sysy_par_body_0_bb10:
	str w11, [x9], #4
	add w10, w10, #1
	b .L__sysy_par_body_0_bb8
.L__sysy_par_body_0_bb9:
	ret
.L__sysy_par_body_0_bb12:
	mov x9, x13
	mov w10, w12
	b .L__sysy_par_body_0_bb3
.L__sysy_par_body_0_bb13:
	mov w13, w12
	b .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb14:
	mov w13, w10
	b .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb15:
	mov x9, x14
	mov w10, w13
	movz w11, #0
	b .L__sysy_par_body_0_bb8
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.bss
	.global x
	.p2align 4
x:
	.zero 400040
	.global y
	.p2align 4
y:
	.zero 12000000
	.global v
	.p2align 4
v:
	.zero 12000000
	.global a
	.p2align 4
a:
	.zero 400040
	.global b
	.p2align 4
b:
	.zero 400040
	.global __sysy_par_ctx_0_0
	.p2align 3
__sysy_par_ctx_0_0:
	.zero 8

	.text
	.align 2
	.global __sysy_par_dispatch
__sysy_par_dispatch:
	cmp w0, #0
	b.eq .Lsysy_disp_0
	ret
.Lsysy_disp_0:
	mov w0, w1
	mov w1, w2
	b __sysy_par_body_0

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
