	.arch armv8-a
	.text
	.p2align 2
	.global run_program
	.type run_program, %function
run_program:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #2096
	stp x19, x20, [sp]
	adrp x10, program_length
	stp x21, x22, [sp, #16]
	adrp x9, input_length
	stp x23, x24, [sp, #32]
	ldr w20, [x10, :lo12:program_length]
	ldr w19, [x9, :lo12:input_length]
	add x22, sp, #48
	movz w23, #0
	mov x0, x22
	movz w21, #1
	movz w2, #2048
	mov w1, w23
	bl memset
	cmp w20, #0
	b.le .Lrun_program_bb31
.Lrun_program_bb26:
	mov w13, w23
	mov w12, w23
	mov w11, w23
	mov w10, w23
	mov w15, w23
	movz w14, #0
	b .Lrun_program_bb1
.Lrun_program_bb24:
	add w15, w15, #1
	cmp w15, w20
	b.ge .Lrun_program_bb25
.Lrun_program_bb1:
	adrp x9, program
	add x9, x9, :lo12:program
	ldr w9, [x9, w15, sxtw #2]
	cmp w9, #62
	b.eq .Lrun_program_bb2
.Lrun_program_bb3:
	cmp w9, #60
	b.eq .Lrun_program_bb4
.Lrun_program_bb5:
	cmp w9, #43
	b.eq .Lrun_program_bb6
.Lrun_program_bb7:
	cmp w9, #45
	b.eq .Lrun_program_bb8
.Lrun_program_bb9:
	cmp w9, #91
	b.ne .Lrun_program_bb14
.Lrun_program_bb10:
	adrp x9, tape
	add x9, x9, :lo12:tape
	ldr w9, [x9, w10, sxtw #2]
	cbnz w9, .Lrun_program_bb13
.Lrun_program_bb11:
	adrp x9, program
	add x9, x9, :lo12:program
	add w16, w15, #1
	add x24, x9, w16, sxtw #2
	mov w23, w21
	mov w17, w15
.Lrun_program_bb12:
	ldr w16, [x24], #4
	cmp w16, #93
	sub w9, w23, #1
	csel w15, w9, w23, eq
	cmp w16, #91
	add w9, w15, #1
	csel w23, w9, w15, eq
	add w17, w17, #1
	cmp w23, #0
	b.gt .Lrun_program_bb12
.Lrun_program_bb29:
	mov w15, w17
	b .Lrun_program_bb24
.Lrun_program_bb25:
	adrp x9, output_length
	str w13, [x9, :lo12:output_length]
	ldp x23, x24, [sp, #32]
	ldp x21, x22, [sp, #16]
	ldp x19, x20, [sp]
	add sp, sp, #2096
	ldp xzr, x30, [sp], #16
	ret
.Lrun_program_bb2:
	add w10, w10, #1
	b .Lrun_program_bb24
.Lrun_program_bb4:
	sub w10, w10, #1
	b .Lrun_program_bb24
.Lrun_program_bb6:
	adrp x9, tape
	add x9, x9, :lo12:tape
	add x16, x9, w10, sxtw #2
	ldr w9, [x16]
	add w9, w9, #1
	str w9, [x16]
	b .Lrun_program_bb24
.Lrun_program_bb8:
	adrp x9, tape
	add x9, x9, :lo12:tape
	add x16, x9, w10, sxtw #2
	ldr w9, [x16]
	sub w9, w9, #1
	str w9, [x16]
	b .Lrun_program_bb24
.Lrun_program_bb13:
	add w9, w12, #1
	str w15, [x22, w12, sxtw #2]
	mov w12, w9
	b .Lrun_program_bb24
.Lrun_program_bb14:
	cmp w9, #93
	b.eq .Lrun_program_bb15
.Lrun_program_bb18:
	cmp w9, #46
	b.eq .Lrun_program_bb19
.Lrun_program_bb20:
	cmp w9, #44
	b.ne .Lrun_program_bb24
.Lrun_program_bb21:
	cmp w11, w19
	b.ge .Lrun_program_bb22
.Lrun_program_bb23:
	adrp x9, input
	add x9, x9, :lo12:input
	ldr w16, [x9, w11, sxtw #2]
	adrp x9, tape
	add x9, x9, :lo12:tape
	str w16, [x9, w10, sxtw #2]
	add w11, w11, #1
	b .Lrun_program_bb24
.Lrun_program_bb15:
	adrp x9, tape
	add x9, x9, :lo12:tape
	ldr w9, [x9, w10, sxtw #2]
	cbz w9, .Lrun_program_bb16
.Lrun_program_bb17:
	sub w9, w12, #1
	ldr w15, [x22, w9, sxtw #2]
	b .Lrun_program_bb24
.Lrun_program_bb16:
	sub w12, w12, #1
	b .Lrun_program_bb24
.Lrun_program_bb19:
	adrp x9, tape
	add x9, x9, :lo12:tape
	ldr w16, [x9, w10, sxtw #2]
	adrp x9, output
	add x17, x9, :lo12:output
	str w16, [x17, w13, sxtw #2]
	add w9, w13, #1
	mov w13, w9
	b .Lrun_program_bb24
.Lrun_program_bb22:
	adrp x9, tape
	add x9, x9, :lo12:tape
	str w14, [x9, w10, sxtw #2]
	b .Lrun_program_bb24
.Lrun_program_bb31:
	mov w13, w23
	b .Lrun_program_bb25
	.size run_program, .-run_program
	.p2align 2
	.global main
	.type main, %function
main:
	stp xzr, x30, [sp, #-16]!
	sub sp, sp, #32
	stp x19, x20, [sp]
	movz w19, #0
	str x21, [sp, #16]
	bl getch
	mov w9, w0
.Lmain_bb1:
	cmp w9, #62
	b.eq .Lmain_bb32
.Lmain_bb2:
	cmp w9, #60
	b.eq .Lmain_bb33
.Lmain_bb3:
	cmp w9, #43
	b.eq .Lmain_bb34
.Lmain_bb4:
	cmp w9, #45
	b.eq .Lmain_bb35
.Lmain_bb5:
	cmp w9, #91
	b.eq .Lmain_bb36
.Lmain_bb6:
	cmp w9, #93
	b.eq .Lmain_bb37
.Lmain_bb7:
	cmp w9, #46
	b.eq .Lmain_bb38
.Lmain_bb8:
	cmp w9, #44
	b.eq .Lmain_bb39
.Lmain_bb9:
	cmp w9, #35
	b.eq .Lmain_bb40
.Lmain_bb31:
	bl getch
	mov w9, w0
	b .Lmain_bb1
.Lmain_bb10:
	cmp w11, #35
	b.eq .Lmain_bb23
.Lmain_bb11:
	adrp x9, program_length
	ldr w10, [x9, :lo12:program_length]
	adrp x9, program
	add x9, x9, :lo12:program
	str w11, [x9, w10, sxtw #2]
	bl getch
	mov w11, w0
.Lmain_bb12:
	cmp w11, #62
	b.eq .Lmain_bb22
.Lmain_bb13:
	cmp w11, #60
	b.eq .Lmain_bb22
.Lmain_bb14:
	cmp w11, #43
	b.eq .Lmain_bb22
.Lmain_bb15:
	cmp w11, #45
	b.eq .Lmain_bb22
.Lmain_bb16:
	cmp w11, #91
	b.eq .Lmain_bb22
.Lmain_bb17:
	cmp w11, #93
	b.eq .Lmain_bb22
.Lmain_bb18:
	cmp w11, #46
	b.eq .Lmain_bb22
.Lmain_bb19:
	cmp w11, #44
	b.eq .Lmain_bb22
.Lmain_bb20:
	cmp w11, #35
	b.eq .Lmain_bb22
.Lmain_bb21:
	bl getch
	mov w11, w0
	b .Lmain_bb12
.Lmain_bb22:
	adrp x9, program_length
	ldr w9, [x9, :lo12:program_length]
	add w10, w9, #1
	adrp x9, program_length
	str w10, [x9, :lo12:program_length]
	b .Lmain_bb10
.Lmain_bb23:
	bl getch
	cmp w0, #105
	b.eq .Lmain_bb24
.Lmain_bb26:
	movz w0, #116
	bl _sysy_starttime
	bl run_program
	movz w0, #118
	bl _sysy_stoptime
	adrp x9, output
	add x9, x9, :lo12:output
	mov x20, x9
.Lmain_bb27:
	adrp x9, output_length
	ldr w9, [x9, :lo12:output_length]
	cmp w19, w9
	b.ge .Lmain_bb29
.Lmain_bb28:
	ldr w0, [x20]
	bl putch
	add w19, w19, #1
	add x20, x20, #4
	b .Lmain_bb27
.Lmain_bb24:
	bl getint
	adrp x9, input_length
	str w0, [x9, :lo12:input_length]
	bl getch
	adrp x9, input
	add x9, x9, :lo12:input
	mov x21, x9
	mov w20, w19
.Lmain_bb25:
	adrp x9, input_length
	ldr w9, [x9, :lo12:input_length]
	cmp w20, w9
	b.ge .Lmain_bb26
.Lmain_bb30:
	bl getch
	str w0, [x21], #4
	add w20, w20, #1
	b .Lmain_bb25
.Lmain_bb29:
	ldp x20, x21, [sp, #8]
	ldr x19, [sp]
	movz w0, #0
	add sp, sp, #32
	ldp xzr, x30, [sp], #16
	ret
.Lmain_bb32:
	mov w11, w9
	b .Lmain_bb10
.Lmain_bb33:
	mov w11, w9
	b .Lmain_bb10
.Lmain_bb34:
	mov w11, w9
	b .Lmain_bb10
.Lmain_bb35:
	mov w11, w9
	b .Lmain_bb10
.Lmain_bb36:
	mov w11, w9
	b .Lmain_bb10
.Lmain_bb37:
	mov w11, w9
	b .Lmain_bb10
.Lmain_bb38:
	mov w11, w9
	b .Lmain_bb10
.Lmain_bb39:
	mov w11, w9
	b .Lmain_bb10
.Lmain_bb40:
	mov w11, w9
	b .Lmain_bb10
	.size main, .-main
	.data
	.global program_length
	.p2align 2
program_length:
	.zero 4
	.global input_length
	.p2align 2
input_length:
	.zero 4
	.global output_length
	.p2align 2
output_length:
	.zero 4
	.bss
	.global program
	.p2align 4
program:
	.zero 262144
	.global tape
	.p2align 4
tape:
	.zero 262144
	.global input
	.p2align 4
input:
	.zero 262144
	.global output
	.p2align 4
output:
	.zero 262144
