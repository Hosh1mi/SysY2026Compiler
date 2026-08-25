	.arch armv8-a
	.text
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #64
	stp x19, x20, [sp]
	adrp x9, cnt
	stp x21, x22, [sp, #16]
	stp x23, x24, [sp, #32]
	str x25, [sp, #48]
	ldr w23, [x9, :lo12:cnt]
	movz w22, #0
	bl getint
	adrp x9, keys
	add x24, x9, :lo12:keys
	mov w20, w0
	mov x0, x24
	bl getarray
	adrp x9, values
	add x25, x9, :lo12:values
	mov w21, w0
	mov x0, x25
	bl getarray
	adrp x9, requests
	add x9, x9, :lo12:requests
	mov x0, x9
	bl getarray
	mov w19, w0
	movz w0, #81
	bl _sysy_starttime
	sub w14, w21, #1
	mov x16, x25
	mov x15, x24
	mov w13, w23
	mov w12, w22
	orr w11, wzr, #0x80000001
	movz w10, #0
.Lmain_bb1:
	cmp w12, w14
	cset w17, lt
	cmp w21, w11
	cset w9, ge
	and w9, w9, w17
	cbz w9, .Lmain_bb17
.Lmain_bb2:
	ldr w24, [x15]
	sdiv w9, w24, w20
	msub w17, w9, w20, w24
	adrp x9, head
	add x9, x9, :lo12:head
	add x25, x9, w17, sxtw #2
	ldr w23, [x16]
	ldr w17, [x25]
	cbz w17, .Lmain_bb8
.Lmain_bb3:
	cbz w17, .Lmain_bb7
.Lmain_bb4:
	adrp x9, key
	add x9, x9, :lo12:key
	ldr w9, [x9, w17, sxtw #2]
	cmp w9, w24
	b.eq .Lmain_bb5
.Lmain_bb6:
	adrp x9, next
	add x9, x9, :lo12:next
	ldr w17, [x9, w17, sxtw #2]
	b .Lmain_bb3
.Lmain_bb5:
	adrp x9, nextvalue
	add x9, x9, :lo12:nextvalue
	add x22, x9, w17, sxtw #2
	ldr w17, [x22]
	adrp x9, nextvalue
	add w24, w13, #1
	add x9, x9, :lo12:nextvalue
	str w17, [x9, w24, sxtw #2]
	adrp x9, value
	str w24, [x22]
	add x9, x9, :lo12:value
	str w23, [x9, w24, sxtw #2]
.Lmain_bb9:
	ldr w24, [x15, #4]
	sdiv w9, w24, w20
	msub w17, w9, w20, w24
	adrp x9, head
	add x9, x9, :lo12:head
	ldr w22, [x16, #4]
	add x25, x9, w17, sxtw #2
	ldr w17, [x25]
	add x15, x15, #4
	add x16, x16, #4
	cbz w17, .Lmain_bb15
.Lmain_bb10:
	cbz w17, .Lmain_bb14
.Lmain_bb11:
	adrp x9, key
	add x9, x9, :lo12:key
	ldr w9, [x9, w17, sxtw #2]
	cmp w9, w24
	b.eq .Lmain_bb12
.Lmain_bb13:
	adrp x9, next
	add x9, x9, :lo12:next
	ldr w17, [x9, w17, sxtw #2]
	b .Lmain_bb10
.Lmain_bb7:
	ldr w17, [x25]
	adrp x9, next
	add w22, w13, #1
	add x9, x9, :lo12:next
	str w17, [x9, w22, sxtw #2]
	adrp x9, key
	str w22, [x25]
	add x17, x9, :lo12:key
	str w24, [x17, w22, sxtw #2]
	adrp x9, value
	add x17, x9, :lo12:value
	adrp x9, nextvalue
	str w23, [x17, w22, sxtw #2]
	add x9, x9, :lo12:nextvalue
	str w10, [x9, w22, sxtw #2]
	b .Lmain_bb9
.Lmain_bb8:
	add w22, w13, #1
	adrp x9, key
	str w22, [x25]
	add x17, x9, :lo12:key
	str w24, [x17, w22, sxtw #2]
	adrp x9, value
	add x17, x9, :lo12:value
	str w23, [x17, w22, sxtw #2]
	adrp x9, next
	add x17, x9, :lo12:next
	adrp x9, nextvalue
	str w10, [x17, w22, sxtw #2]
	add x9, x9, :lo12:nextvalue
	str w10, [x9, w22, sxtw #2]
	b .Lmain_bb9
.Lmain_bb12:
	adrp x9, nextvalue
	add x9, x9, :lo12:nextvalue
	add x23, x9, w17, sxtw #2
	ldr w17, [x23]
	adrp x9, nextvalue
	add w24, w13, #2
	add x9, x9, :lo12:nextvalue
	str w17, [x9, w24, sxtw #2]
	adrp x9, value
	str w24, [x23]
	add x9, x9, :lo12:value
	str w22, [x9, w24, sxtw #2]
.Lmain_bb16:
	add w13, w13, #2
	add w12, w12, #2
	add x15, x15, #4
	add x16, x16, #4
	b .Lmain_bb1
.Lmain_bb14:
	ldr w17, [x25]
	adrp x9, next
	add w23, w13, #2
	add x9, x9, :lo12:next
	str w17, [x9, w23, sxtw #2]
	adrp x9, key
	str w23, [x25]
	add x17, x9, :lo12:key
	str w24, [x17, w23, sxtw #2]
	adrp x9, value
	add x17, x9, :lo12:value
	adrp x9, nextvalue
	str w22, [x17, w23, sxtw #2]
	add x9, x9, :lo12:nextvalue
	str w10, [x9, w23, sxtw #2]
	b .Lmain_bb16
.Lmain_bb15:
	add w23, w13, #2
	adrp x9, key
	str w23, [x25]
	add x17, x9, :lo12:key
	str w24, [x17, w23, sxtw #2]
	adrp x9, value
	add x17, x9, :lo12:value
	str w22, [x17, w23, sxtw #2]
	adrp x9, next
	add x17, x9, :lo12:next
	adrp x9, nextvalue
	str w10, [x17, w23, sxtw #2]
	add x9, x9, :lo12:nextvalue
	str w10, [x9, w23, sxtw #2]
	b .Lmain_bb16
.Lmain_bb17:
	cmp w12, w21
	b.ge .Lmain_bb32
.Lmain_bb30:
	mov x10, x15
	mov w14, w13
	mov w15, w12
	movz w13, #0
.Lmain_bb18:
	cmp w15, w21
	b.ge .Lmain_bb33
.Lmain_bb19:
	ldr w22, [x10]
	sdiv w9, w22, w20
	msub w11, w9, w20, w22
	adrp x9, head
	add x9, x9, :lo12:head
	add x23, x9, w11, sxtw #2
	ldr w17, [x16]
	ldr w11, [x23]
	cbz w11, .Lmain_bb25
.Lmain_bb20:
	cbz w11, .Lmain_bb24
.Lmain_bb21:
	adrp x9, key
	add x9, x9, :lo12:key
	ldr w9, [x9, w11, sxtw #2]
	cmp w9, w22
	b.eq .Lmain_bb22
.Lmain_bb23:
	adrp x9, next
	add x9, x9, :lo12:next
	ldr w11, [x9, w11, sxtw #2]
	b .Lmain_bb20
.Lmain_bb22:
	adrp x9, nextvalue
	add x9, x9, :lo12:nextvalue
	add x12, x9, w11, sxtw #2
	ldr w11, [x12]
	adrp x9, nextvalue
	add w22, w14, #1
	add x9, x9, :lo12:nextvalue
	str w11, [x9, w22, sxtw #2]
	adrp x9, value
	str w22, [x12]
	add x9, x9, :lo12:value
	str w17, [x9, w22, sxtw #2]
.Lmain_bb26:
	add w14, w14, #1
	add w15, w15, #1
	add x10, x10, #4
	add x16, x16, #4
	b .Lmain_bb18
.Lmain_bb24:
	ldr w11, [x23]
	adrp x9, next
	add w12, w14, #1
	add x9, x9, :lo12:next
	str w11, [x9, w12, sxtw #2]
	adrp x9, key
	str w12, [x23]
	add x11, x9, :lo12:key
	str w22, [x11, w12, sxtw #2]
	adrp x9, value
	add x11, x9, :lo12:value
	adrp x9, nextvalue
	str w17, [x11, w12, sxtw #2]
	add x9, x9, :lo12:nextvalue
	str w13, [x9, w12, sxtw #2]
	b .Lmain_bb26
.Lmain_bb25:
	add w12, w14, #1
	adrp x9, key
	str w12, [x23]
	add x11, x9, :lo12:key
	str w22, [x11, w12, sxtw #2]
	adrp x9, value
	add x11, x9, :lo12:value
	str w17, [x11, w12, sxtw #2]
	adrp x9, next
	add x11, x9, :lo12:next
	adrp x9, nextvalue
	str w13, [x11, w12, sxtw #2]
	add x9, x9, :lo12:nextvalue
	str w13, [x9, w12, sxtw #2]
	b .Lmain_bb26
.Lmain_bb27:
	adrp x9, __sysy_par_ctx_0_0
	movz w1, #0
	str w20, [x9, :lo12:__sysy_par_ctx_0_0]
	mov w0, w1
	mov w2, w19
	bl __sysy_parallel_for
	movz w0, #93
	bl _sysy_stoptime
	adrp x9, ans
	add x9, x9, :lo12:ans
	mov w0, w19
	mov x1, x9
	bl putarray
	adrp x10, hashmod
	adrp x9, cnt
	str w20, [x10, :lo12:hashmod]
	str w21, [x9, :lo12:cnt]
	ldp x24, x25, [sp, #40]
	ldp x22, x23, [sp, #24]
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #64
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb32:
	mov w21, w13
	b .Lmain_bb27
.Lmain_bb33:
	mov w21, w14
	b .Lmain_bb27
	.size main, .-main
	.p2align 2
	.global __sysy_par_body_0
	.type __sysy_par_body_0, %function
__sysy_par_body_0:
	adrp x9, __sysy_par_ctx_0_0
	ldr w13, [x9, :lo12:__sysy_par_ctx_0_0]
	mov w16, w0
	mov w14, w1
	movz w15, #0
.L__sysy_par_body_0_bb1:
	cmp w16, w14
	b.ge .L__sysy_par_body_0_bb2
.L__sysy_par_body_0_bb3:
	adrp x9, requests
	add x9, x9, :lo12:requests
	ldr w17, [x9, w16, sxtw #2]
	sdiv w9, w17, w13
	msub w10, w9, w13, w17
	adrp x9, head
	add x9, x9, :lo12:head
	ldr w12, [x9, w10, sxtw #2]
.L__sysy_par_body_0_bb4:
	cbz w12, .L__sysy_par_body_0_bb15
.L__sysy_par_body_0_bb5:
	adrp x9, key
	add x9, x9, :lo12:key
	ldr w9, [x9, w12, sxtw #2]
	cmp w9, w17
	b.eq .L__sysy_par_body_0_bb6
.L__sysy_par_body_0_bb10:
	adrp x9, next
	add x9, x9, :lo12:next
	ldr w12, [x9, w12, sxtw #2]
	b .L__sysy_par_body_0_bb4
.L__sysy_par_body_0_bb2:
	ret
.L__sysy_par_body_0_bb6:
	cbz w12, .L__sysy_par_body_0_bb13
.L__sysy_par_body_0_bb11:
	mov w11, w15
.L__sysy_par_body_0_bb7:
	adrp x9, value
	add x10, x9, :lo12:value
	ldr w10, [x10, w12, sxtw #2]
	adrp x9, nextvalue
	add x9, x9, :lo12:nextvalue
	ldr w12, [x9, w12, sxtw #2]
	add w11, w11, w10
	cbnz w12, .L__sysy_par_body_0_bb7
.L__sysy_par_body_0_bb8:
	cmp w17, #100
	lsl w10, w11, #1
	add w9, w11, w11, lsl #1
	csel w10, w10, w9, gt
.L__sysy_par_body_0_bb9:
	adrp x9, ans
	add x9, x9, :lo12:ans
	str w10, [x9, w16, sxtw #2]
	add w16, w16, #1
	b .L__sysy_par_body_0_bb1
.L__sysy_par_body_0_bb13:
	mov w11, w15
	b .L__sysy_par_body_0_bb8
.L__sysy_par_body_0_bb15:
	mov w10, w15
	b .L__sysy_par_body_0_bb9
	.size __sysy_par_body_0, .-__sysy_par_body_0
	.data
	.global hashmod
	.p2align 2
hashmod:
	.zero 4
	.global cnt
	.p2align 2
cnt:
	.zero 4
	.bss
	.global head
	.p2align 4
head:
	.zero 40000000
	.global next
	.p2align 4
next:
	.zero 40000000
	.global nextvalue
	.p2align 4
nextvalue:
	.zero 40000000
	.global key
	.p2align 4
key:
	.zero 40000000
	.global value
	.p2align 4
value:
	.zero 40000000
	.global keys
	.p2align 4
keys:
	.zero 40000000
	.global values
	.p2align 4
values:
	.zero 40000000
	.global requests
	.p2align 4
requests:
	.zero 40000000
	.global ans
	.p2align 4
ans:
	.zero 40000000
	.global __sysy_par_ctx_0_0
	.p2align 2
__sysy_par_ctx_0_0:
	.zero 4

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
