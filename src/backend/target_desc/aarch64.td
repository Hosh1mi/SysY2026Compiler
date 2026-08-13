// AArch64 machine-instruction metadata and instruction-selection patterns.
// Defaults: defs=0, operands=0, latency=1, resource=ALU,
// properties=[], immediate=None, remat=0.

// Generic, control-flow, and call-frame instructions.
instruction PHI { mnemonic = "PHI"; defs = 1; latency = 0; resource = None; properties = [pseudo]; }
instruction COPY { mnemonic = "COPY"; defs = 1; operands = 2; }
instruction IMPLICIT_DEF { mnemonic = "IMPLICIT_DEF"; defs = 1; operands = 1; latency = 0; resource = None; properties = [pseudo]; }
instruction ADJCALLSTACKDOWN { mnemonic = "ADJCALLSTACKDOWN"; operands = 1; properties = [side_effects, pseudo]; }
instruction ADJCALLSTACKUP { mnemonic = "ADJCALLSTACKUP"; operands = 1; properties = [side_effects, pseudo]; }
instruction CALL { mnemonic = "bl"; latency = 3; resource = Branch; properties = [call, side_effects]; }
instruction TAILCALL { mnemonic = "b"; resource = Branch; properties = [terminator, branch, call, return, side_effects]; }
instruction RET { mnemonic = "ret"; resource = Branch; properties = [terminator, branch, return, side_effects]; }
instruction B { mnemonic = "b"; operands = 1; resource = Branch; properties = [terminator, branch]; }
instruction Bcc { mnemonic = "b.cond"; operands = 2; resource = Branch; properties = [terminator, branch, uses_flags]; }
instruction CBZ { mnemonic = "cbz"; operands = 2; resource = Branch; properties = [terminator, branch]; }
instruction CBNZ { mnemonic = "cbnz"; operands = 2; resource = Branch; properties = [terminator, branch]; }
instruction TBZ { mnemonic = "tbz"; operands = 3; resource = Branch; properties = [terminator, branch]; }
instruction TBNZ { mnemonic = "tbnz"; operands = 3; resource = Branch; properties = [terminator, branch]; }
instruction CSELW { mnemonic = "csel"; defs = 1; operands = 4; properties = [uses_flags]; }
instruction CSELX { mnemonic = "csel"; defs = 1; operands = 4; properties = [uses_flags]; }
instruction FCSELS { mnemonic = "fcsel"; defs = 1; operands = 4; latency = 2; resource = FPALU; properties = [uses_flags]; }
instruction CSETW { mnemonic = "cset"; defs = 1; operands = 2; properties = [uses_flags]; }

// Constants and addresses.
instruction MOVi32 { mnemonic = "MOVi32"; defs = 1; operands = 2; remat = 1; }
instruction MOVi64 { mnemonic = "MOVi64"; defs = 1; operands = 2; remat = 1; }
instruction MOVZ { mnemonic = "movz"; defs = 1; operands = 3; }
instruction MOVN { mnemonic = "movn"; defs = 1; operands = 3; }
instruction MOVK { mnemonic = "movk"; defs = 1; operands = 4; }
instruction MOVIv4Zero { mnemonic = "movi"; defs = 1; operands = 1; latency = 2; resource = FPALU; remat = 2; }
instruction MOVIv4s { mnemonic = "movi"; defs = 1; operands = 3; latency = 2; resource = FPALU; remat = 2; }
instruction MOVIv4sMsl { mnemonic = "movi"; defs = 1; operands = 3; latency = 2; resource = FPALU; remat = 2; }
instruction MVNIv4s { mnemonic = "mvni"; defs = 1; operands = 3; latency = 2; resource = FPALU; remat = 2; }
instruction MOVIv16b { mnemonic = "movi"; defs = 1; operands = 2; latency = 2; resource = FPALU; remat = 2; }
instruction FMOVv4s { mnemonic = "fmov"; defs = 1; operands = 2; latency = 2; resource = FPALU; remat = 2; }
instruction ADRP { mnemonic = "adrp"; defs = 1; operands = 2; remat = 1; }
instruction ADDlow { mnemonic = "add"; defs = 1; operands = 3; }
instruction LEA_FRAME { mnemonic = "LEA_FRAME"; defs = 1; operands = 2; properties = [pseudo]; remat = 1; }

// Integer and scalar floating-point instructions.
instruction ADDWrr { mnemonic = "add"; defs = 1; operands = 3; properties = [commutable]; }
instruction ADDWri { mnemonic = "add"; defs = 1; operands = 3; immediate = AddSub12; }
instruction ADDWrs { mnemonic = "add"; defs = 1; operands = 4; }
instruction ADDWrsX { mnemonic = "add"; defs = 1; operands = 4; }
instruction ADDWlsl { mnemonic = "add"; defs = 1; operands = 4; }
instruction SUBWrr { mnemonic = "sub"; defs = 1; operands = 3; }
instruction SUBWri { mnemonic = "sub"; defs = 1; operands = 3; immediate = AddSub12; }
instruction NEGW { mnemonic = "neg"; defs = 1; operands = 2; }
instruction CNEGW { mnemonic = "cneg"; defs = 1; operands = 3; properties = [uses_flags]; }
instruction MULWrr { mnemonic = "mul"; defs = 1; operands = 3; latency = 3; resource = MAC; properties = [commutable]; }
instruction MULXrr { mnemonic = "mul"; defs = 1; operands = 3; latency = 3; resource = MAC; properties = [commutable]; }
instruction MADDWrrr { mnemonic = "madd"; defs = 1; operands = 4; latency = 3; resource = MAC; }
instruction MSUBWrrr { mnemonic = "msub"; defs = 1; operands = 4; latency = 3; resource = MAC; }
instruction MSUBXrrr { mnemonic = "msub"; defs = 1; operands = 4; latency = 3; resource = MAC; }
instruction SDIVWrr { mnemonic = "sdiv"; defs = 1; operands = 3; latency = 12; resource = Divide; }
instruction UDIVWrr { mnemonic = "udiv"; defs = 1; operands = 3; latency = 12; resource = Divide; }
instruction SDIVXrr { mnemonic = "sdiv"; defs = 1; operands = 3; latency = 12; resource = Divide; }
instruction UDIVXrr { mnemonic = "udiv"; defs = 1; operands = 3; latency = 12; resource = Divide; }
instruction SMULLXrr { mnemonic = "smull"; defs = 1; operands = 3; latency = 3; resource = MAC; }
instruction SMADDLXrrr { mnemonic = "smaddl"; defs = 1; operands = 4; latency = 3; resource = MAC; }
instruction UMULHXrr { mnemonic = "umulh"; defs = 1; operands = 3; latency = 3; resource = MAC; }
instruction NEGX { mnemonic = "neg"; defs = 1; operands = 2; }
instruction CMPXrr { mnemonic = "cmp"; operands = 2; properties = [sets_flags]; }
instruction CMPXri { mnemonic = "cmp"; operands = 2; properties = [sets_flags]; immediate = AddSub12; }
instruction ANDWrr { mnemonic = "and"; defs = 1; operands = 3; properties = [commutable]; }
instruction ANDWri { mnemonic = "and"; defs = 1; operands = 3; }
instruction ORRWrr { mnemonic = "orr"; defs = 1; operands = 3; properties = [commutable]; }
instruction ORRWri { mnemonic = "orr"; defs = 1; operands = 3; }
instruction EORWrr { mnemonic = "eor"; defs = 1; operands = 3; properties = [commutable]; }
instruction ANDXrr { mnemonic = "and"; defs = 1; operands = 3; properties = [commutable]; }
instruction ORRXrr { mnemonic = "orr"; defs = 1; operands = 3; properties = [commutable]; }
instruction ORRXri { mnemonic = "orr"; defs = 1; operands = 3; }
instruction EORXrr { mnemonic = "eor"; defs = 1; operands = 3; properties = [commutable]; }
instruction LSLWrr { mnemonic = "lsl"; defs = 1; operands = 3; }
instruction LSLWri { mnemonic = "lsl"; defs = 1; operands = 3; immediate = Shift32; }
instruction LSRWrr { mnemonic = "lsr"; defs = 1; operands = 3; }
instruction LSRWri { mnemonic = "lsr"; defs = 1; operands = 3; immediate = Shift32; }
instruction ASRWrr { mnemonic = "asr"; defs = 1; operands = 3; }
instruction ASRWri { mnemonic = "asr"; defs = 1; operands = 3; immediate = Shift32; }
instruction LSLXrr { mnemonic = "lsl"; defs = 1; operands = 3; }
instruction LSRXrr { mnemonic = "lsr"; defs = 1; operands = 3; }
instruction ASRXrr { mnemonic = "asr"; defs = 1; operands = 3; }
instruction LSRXri { mnemonic = "lsr"; defs = 1; operands = 3; immediate = Shift64; }
instruction CMPWrr { mnemonic = "cmp"; operands = 2; properties = [sets_flags]; }
instruction CMPWri { mnemonic = "cmp"; operands = 2; properties = [sets_flags]; immediate = AddSub12; }
instruction TSTWrr { mnemonic = "tst"; operands = 2; properties = [sets_flags]; }
instruction TSTWri { mnemonic = "tst"; operands = 2; properties = [sets_flags]; immediate = Logical32; }
instruction CLZW { mnemonic = "clz"; defs = 1; operands = 2; }
instruction RBITW { mnemonic = "rbit"; defs = 1; operands = 2; }
instruction FADDS { mnemonic = "fadd"; defs = 1; operands = 3; latency = 4; resource = FPALU; properties = [commutable]; }
instruction FSUBS { mnemonic = "fsub"; defs = 1; operands = 3; latency = 4; resource = FPALU; }
instruction FMULS { mnemonic = "fmul"; defs = 1; operands = 3; latency = 4; resource = FPMulDiv; properties = [commutable]; }
instruction FDIVS { mnemonic = "fdiv"; defs = 1; operands = 3; latency = 18; resource = FPMulDiv; }
instruction FNEGS { mnemonic = "fneg"; defs = 1; operands = 2; latency = 2; resource = FPALU; }
instruction FCMPSrr { mnemonic = "fcmp"; operands = 2; latency = 3; resource = FPALU; properties = [sets_flags]; }
instruction FCMPZS { mnemonic = "fcmp"; operands = 1; latency = 3; resource = FPALU; properties = [sets_flags]; }
instruction SCVTFWS { mnemonic = "scvtf"; defs = 1; operands = 2; latency = 4; resource = FPALU; }
instruction FCVTZSW { mnemonic = "fcvtzs"; defs = 1; operands = 2; latency = 4; resource = FPALU; }
instruction FMOVWS { mnemonic = "fmov"; defs = 1; operands = 2; latency = 2; resource = FPALU; }
instruction FMOVSW { mnemonic = "fmov"; defs = 1; operands = 2; latency = 2; resource = FPALU; }

// Loads and stores.
instruction LDRWui { mnemonic = "ldr"; defs = 1; operands = 3; latency = 4; resource = LoadStore; properties = [load]; }
instruction LDRWlo { mnemonic = "ldr"; defs = 1; operands = 3; latency = 4; resource = LoadStore; properties = [load]; }
instruction LDRWro { mnemonic = "ldr"; defs = 1; operands = 5; latency = 4; resource = LoadStore; properties = [load]; }
instruction LDRWpost { mnemonic = "ldr"; defs = 1; operands = 3; latency = 4; resource = LoadStore; properties = [load]; }
instruction STRWui { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }
instruction STRWlo { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }
instruction STRWro { mnemonic = "str"; operands = 5; resource = LoadStore; properties = [store]; }
instruction STRWpost { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }
instruction LDRSui { mnemonic = "ldr"; defs = 1; operands = 3; latency = 4; resource = LoadStore; properties = [load]; }
instruction LDRSlo { mnemonic = "ldr"; defs = 1; operands = 3; latency = 4; resource = LoadStore; properties = [load]; }
instruction LDRSro { mnemonic = "ldr"; defs = 1; operands = 5; latency = 4; resource = LoadStore; properties = [load]; }
instruction LDRSpost { mnemonic = "ldr"; defs = 1; operands = 3; latency = 4; resource = LoadStore; properties = [load]; }
instruction STRSui { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }
instruction STRSlo { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }
instruction STRSro { mnemonic = "str"; operands = 5; resource = LoadStore; properties = [store]; }
instruction STRSpost { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }
instruction LDRDui { mnemonic = "ldr"; defs = 1; operands = 3; latency = 4; resource = LoadStore; properties = [load]; }
instruction STRDui { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }
instruction LDRQui { mnemonic = "ldr"; defs = 1; operands = 3; latency = 5; resource = LoadStore; properties = [load]; }
instruction LDRQlo { mnemonic = "ldr"; defs = 1; operands = 3; latency = 5; resource = LoadStore; properties = [load]; }
instruction LDRQro { mnemonic = "ldr"; defs = 1; operands = 5; latency = 5; resource = LoadStore; properties = [load]; }
instruction LDRQpost { mnemonic = "ldr"; defs = 1; operands = 3; latency = 5; resource = LoadStore; properties = [load]; }
instruction STRQui { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }
instruction STRQlo { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }
instruction STRQro { mnemonic = "str"; operands = 5; resource = LoadStore; properties = [store]; }
instruction STRQpost { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }
instruction LDRXui { mnemonic = "ldr"; defs = 1; operands = 3; latency = 4; resource = LoadStore; properties = [load]; }
instruction LDRXlo { mnemonic = "ldr"; defs = 1; operands = 3; latency = 4; resource = LoadStore; properties = [load]; }
instruction LDRXro { mnemonic = "ldr"; defs = 1; operands = 5; latency = 4; resource = LoadStore; properties = [load]; }
instruction LDRXpost { mnemonic = "ldr"; defs = 1; operands = 3; latency = 4; resource = LoadStore; properties = [load]; }
instruction STRXui { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }
instruction STRXlo { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }
instruction STRXro { mnemonic = "str"; operands = 5; resource = LoadStore; properties = [store]; }
instruction STRXpost { mnemonic = "str"; operands = 3; resource = LoadStore; properties = [store]; }

// Paired memory operations.
instruction LDPWi { mnemonic = "ldp"; defs = 2; operands = 4; latency = 5; resource = LoadStore; properties = [load]; }
instruction STPWi { mnemonic = "stp"; operands = 4; resource = LoadStore; properties = [store]; }
instruction LDPSi { mnemonic = "ldp"; defs = 2; operands = 4; latency = 5; resource = LoadStore; properties = [load]; }
instruction STPSi { mnemonic = "stp"; operands = 4; resource = LoadStore; properties = [store]; }
instruction LDPXi { mnemonic = "ldp"; defs = 2; operands = 4; latency = 5; resource = LoadStore; properties = [load]; }
instruction STPXi { mnemonic = "stp"; operands = 4; resource = LoadStore; properties = [store]; }
instruction LDPDi { mnemonic = "ldp"; defs = 2; operands = 4; latency = 5; resource = LoadStore; properties = [load]; }
instruction STPDi { mnemonic = "stp"; operands = 4; resource = LoadStore; properties = [store]; }
instruction STPXpre { mnemonic = "stp"; operands = 4; resource = LoadStore; properties = [store, side_effects]; }
instruction LDPXpost { mnemonic = "ldp"; defs = 3; operands = 4; latency = 5; resource = LoadStore; properties = [load, side_effects]; }
instruction LDPQi { mnemonic = "ldp"; defs = 2; operands = 4; latency = 5; resource = LoadStore; properties = [load]; }
instruction STPQi { mnemonic = "stp"; operands = 4; resource = LoadStore; properties = [store]; }

// 64-bit address and frame arithmetic.
instruction ADDXrr { mnemonic = "add"; defs = 1; operands = 3; properties = [commutable]; }
instruction ADDXri { mnemonic = "add"; defs = 1; operands = 3; immediate = AddSub12; }
instruction ADDXrs { mnemonic = "add"; defs = 1; operands = 5; }
instruction SUBXrr { mnemonic = "sub"; defs = 1; operands = 3; }
instruction SUBXri { mnemonic = "sub"; defs = 1; operands = 3; immediate = AddSub12; }
instruction LSLXri { mnemonic = "lsl"; defs = 1; operands = 3; immediate = Shift64; }
instruction ASRXri { mnemonic = "asr"; defs = 1; operands = 3; immediate = Shift64; }
instruction COPYXtoW { mnemonic = "mov"; defs = 1; operands = 2; }
instruction MOVXrr { mnemonic = "mov"; defs = 1; operands = 2; }
instruction SUBSPri { mnemonic = "sub"; defs = 1; operands = 3; properties = [side_effects]; immediate = AddSub12; }
instruction ADDSPri { mnemonic = "add"; defs = 1; operands = 3; properties = [side_effects]; immediate = AddSub12; }
instruction SXTW { mnemonic = "sxtw"; defs = 1; operands = 2; }
instruction UXTW { mnemonic = "uxtw"; defs = 1; operands = 2; }

// Vector construction and lane operations.
instruction DUPv4i32 { mnemonic = "dup"; defs = 1; operands = 2; latency = 6; resource = FPALU; }
instruction DUPv4f32 { mnemonic = "dup"; defs = 1; operands = 2; latency = 6; resource = FPALU; }
instruction DUPv4sLane { mnemonic = "dup"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction INSv4i32 { mnemonic = "ins"; defs = 1; operands = 4; latency = 6; resource = FPALU; }
instruction INSv4f32 { mnemonic = "ins"; defs = 1; operands = 4; latency = 6; resource = FPALU; }
instruction EXTRACTv4i32 { mnemonic = "umov"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction EXTRACTv4f32 { mnemonic = "umov"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction ZIP1v4s { mnemonic = "zip1"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction ZIP2v4s { mnemonic = "zip2"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction UZP1v4s { mnemonic = "uzp1"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction UZP2v4s { mnemonic = "uzp2"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction TRN1v4s { mnemonic = "trn1"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction TRN2v4s { mnemonic = "trn2"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction EXTv16b { mnemonic = "ext"; defs = 1; operands = 4; latency = 6; resource = FPALU; }
instruction REV64v4s { mnemonic = "rev64"; defs = 1; operands = 2; latency = 6; resource = FPALU; }

// Vector arithmetic and permutation.
instruction ADDv4i32 { mnemonic = "add"; defs = 1; operands = 3; latency = 6; resource = FPALU; properties = [commutable]; }
instruction SUBv4i32 { mnemonic = "sub"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction MULv4i32 { mnemonic = "mul"; defs = 1; operands = 3; latency = 6; resource = FPMulDiv; properties = [commutable]; }
instruction SMINv4i32 { mnemonic = "smin"; defs = 1; operands = 3; latency = 6; resource = FPALU; properties = [commutable]; }
instruction SMAXv4i32 { mnemonic = "smax"; defs = 1; operands = 3; latency = 6; resource = FPALU; properties = [commutable]; }
instruction NEGv4i32 { mnemonic = "neg"; defs = 1; operands = 2; latency = 6; resource = FPALU; }
instruction SSHLv4i32 { mnemonic = "sshl"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction USHLv4i32 { mnemonic = "ushl"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction SHLiv4i32 { mnemonic = "shl"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction SSHRiv4i32 { mnemonic = "sshr"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction USHRiv4i32 { mnemonic = "ushr"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction MLAv4i32 { mnemonic = "mla"; defs = 1; operands = 4; latency = 6; resource = FPMulDiv; }
instruction MLSv4i32 { mnemonic = "mls"; defs = 1; operands = 4; latency = 6; resource = FPMulDiv; }
instruction ADDv4f32 { mnemonic = "add"; defs = 1; operands = 3; latency = 6; resource = FPALU; properties = [commutable]; }
instruction SUBv4f32 { mnemonic = "sub"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction MULv4f32 { mnemonic = "fmul"; defs = 1; operands = 3; latency = 6; resource = FPMulDiv; properties = [commutable]; }
instruction DIVv4f32 { mnemonic = "fdiv"; defs = 1; operands = 3; latency = 18; resource = FPMulDiv; }
instruction NEGv4f32 { mnemonic = "fneg"; defs = 1; operands = 2; latency = 6; resource = FPALU; }
instruction FMLAv4f32 { mnemonic = "fmla"; defs = 1; operands = 4; latency = 6; resource = FPMulDiv; }
instruction FMLSv4f32 { mnemonic = "fmls"; defs = 1; operands = 4; latency = 6; resource = FPMulDiv; }
instruction ANDv16i8 { mnemonic = "and"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction ORRv16i8 { mnemonic = "orr"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction EORv16i8 { mnemonic = "eor"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction MVNv16i8 { mnemonic = "mvn"; defs = 1; operands = 2; latency = 6; resource = FPALU; }
instruction CMEQv4i32 { mnemonic = "vector-compare"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction CMGTv4i32 { mnemonic = "vector-compare"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction CMGEv4i32 { mnemonic = "vector-compare"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction CMHIv4i32 { mnemonic = "vector-compare"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction CMHSv4i32 { mnemonic = "vector-compare"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction FCMEQv4f32 { mnemonic = "vector-compare"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction FCMGTv4f32 { mnemonic = "vector-compare"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction FCMGEv4f32 { mnemonic = "vector-compare"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction BSLv16i8 { mnemonic = "bsl"; defs = 1; operands = 4; latency = 6; resource = FPALU; }
instruction TBL1v16i8 { mnemonic = "tbl"; defs = 1; operands = 3; latency = 6; resource = FPALU; }
instruction SHUFFLEv16i8 { mnemonic = "tbl"; defs = 1; operands = 4; latency = 6; resource = FPALU; }
instruction ADDVv4i32 { mnemonic = "addv"; defs = 1; operands = 2; latency = 6; resource = FPALU; }

// Late machine pseudos.
instruction FRAME_SETUP { mnemonic = "FRAME_SETUP"; properties = [side_effects, pseudo]; }
instruction FRAME_DESTROY { mnemonic = "FRAME_DESTROY"; properties = [side_effects, pseudo]; }
instruction SPILL_LOAD { mnemonic = "SPILL_LOAD"; defs = 1; operands = 2; latency = 4; resource = LoadStore; properties = [load, pseudo]; }
instruction SPILL_STORE { mnemonic = "SPILL_STORE"; operands = 2; resource = LoadStore; properties = [store, pseudo]; }

// SelectionDAG nodes.  generated nodes are fully handled by patterns;
// hybrid nodes run custom folds before falling back to generated patterns.
dag Invalid { arity = 0; mode = ignore; }
dag EntryToken { arity = 0; mode = ignore; }
dag Argument { arity = 0; mode = custom; }
dag Constant { arity = 0; mode = hybrid; }
dag FPConstant { arity = 0; mode = custom; }
dag GlobalAddress { arity = 0; mode = hybrid; }
dag FrameIndex { arity = 0; mode = generated; }
dag Add { arity = 2; mode = hybrid; }
dag Sub { arity = 2; mode = hybrid; }
dag Mul { arity = 2; mode = hybrid; }
dag SDiv { arity = 2; mode = hybrid; }
dag SRem { arity = 2; mode = custom; }
dag UDiv { arity = 2; mode = hybrid; }
dag URem { arity = 2; mode = custom; }
dag FAdd { arity = 2; mode = hybrid; }
dag FSub { arity = 2; mode = hybrid; }
dag FMul { arity = 2; mode = hybrid; }
dag FDiv { arity = 2; mode = hybrid; }
dag FNeg { arity = 1; mode = generated; }
dag Shl { arity = 2; mode = hybrid; }
dag LShr { arity = 2; mode = hybrid; }
dag AShr { arity = 2; mode = hybrid; }
dag And { arity = 2; mode = hybrid; }
dag Or { arity = 2; mode = hybrid; }
dag Xor { arity = 2; mode = hybrid; }
dag ICmp { arity = 2; mode = custom; }
dag FCmp { arity = 2; mode = custom; }
dag Select { arity = 3; mode = custom; }
dag GEP { arity = variadic; mode = custom; }
dag Load { arity = 2; mode = generated; address = 1; }
dag Store { arity = 3; mode = generated; address = 2; }
dag ZExt { arity = 1; mode = generated; }
dag SExt { arity = 1; mode = generated; }
dag Trunc { arity = 1; mode = generated; }
dag FPToSI { arity = 1; mode = generated; }
dag SIToFP { arity = 1; mode = generated; }
dag Bitcast { arity = 1; mode = generated; }
dag Clz { arity = 1; mode = generated; }
dag Splat { arity = 1; mode = generated; }
dag InsertElement { arity = 3; mode = hybrid; }
dag ExtractElement { arity = 2; mode = hybrid; }
dag ShuffleVector { arity = 2; mode = custom; }
dag Phi { arity = variadic; mode = custom; }
dag Call { arity = variadic; mode = custom; }
dag TailCall { arity = variadic; mode = custom; }
dag Branch { arity = 1; mode = generated; }
dag BranchCond { arity = 2; mode = custom; }
dag Return { arity = variadic; mode = hybrid; }
dag MAdd { arity = 3; mode = generated; }
dag MSub { arity = 3; mode = generated; }
dag FMAdd { arity = 3; mode = generated; }
dag FMSub { arity = 3; mode = generated; }
dag VectorReduceAdd { arity = 1; mode = custom; }
dag SMin { arity = 2; mode = hybrid; }
dag SMax { arity = 2; mode = hybrid; }
dag MulMod { arity = 3; mode = custom; }

// One-instruction patterns.  Higher priorities handle constrained forms
// before their register or copy fallbacks.
pattern ConstantI1 { root = Constant; result = I1; emit = MOVi32; operands = [def, node_integer]; priority = 10; }
pattern ConstantI32 { root = Constant; result = I32; emit = MOVi32; operands = [def, node_integer]; priority = 10; }
pattern ConstantI64 { root = Constant; result = I64; emit = MOVi64; operands = [def, node_integer]; priority = 10; }
pattern ConstantPtr { root = Constant; result = Ptr; emit = MOVi64; operands = [def, node_integer]; priority = 10; }
pattern GlobalAddressDirect { root = GlobalAddress; result = Ptr; emit = ADRP; operands = [def, node_global]; predicate = direct_global; priority = 10; }
pattern FrameIndexPtr { root = FrameIndex; result = Ptr; emit = LEA_FRAME; operands = [def, frame_index]; priority = 10; }

pattern AddI1Imm { root = Add; result = I1; emit = ADDWri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern AddI1 { root = Add; result = I1; emit = ADDWrr; operands = [def, use0, use1]; priority = 10; }
pattern AddI32Imm { root = Add; result = I32; emit = ADDWri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern AddI32 { root = Add; result = I32; emit = ADDWrr; operands = [def, use0, use1]; priority = 10; }
pattern AddI64Imm { root = Add; result = I64; emit = ADDXri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern AddI64 { root = Add; result = I64; emit = ADDXrr; operands = [def, use0, use1]; priority = 10; }
pattern AddV4I32 { root = Add; result = V4I32; emit = ADDv4i32; operands = [def, use0, use1]; priority = 10; }

pattern SubI1Imm { root = Sub; result = I1; emit = SUBWri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern SubI1 { root = Sub; result = I1; emit = SUBWrr; operands = [def, use0, use1]; priority = 10; }
pattern SubI32Imm { root = Sub; result = I32; emit = SUBWri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern SubI32 { root = Sub; result = I32; emit = SUBWrr; operands = [def, use0, use1]; priority = 10; }
pattern SubI64Imm { root = Sub; result = I64; emit = SUBXri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern SubI64 { root = Sub; result = I64; emit = SUBXrr; operands = [def, use0, use1]; priority = 10; }
pattern SubV4I32 { root = Sub; result = V4I32; emit = SUBv4i32; operands = [def, use0, use1]; priority = 10; }

pattern MulI1 { root = Mul; result = I1; emit = MULWrr; operands = [def, use0, use1]; priority = 10; }
pattern MulI32 { root = Mul; result = I32; emit = MULWrr; operands = [def, use0, use1]; priority = 10; }
pattern MulI64 { root = Mul; result = I64; emit = MULXrr; operands = [def, use0, use1]; priority = 10; }
pattern MulV4I32 { root = Mul; result = V4I32; emit = MULv4i32; operands = [def, use0, use1]; priority = 10; }
pattern SDivI1 { root = SDiv; result = I1; emit = SDIVWrr; operands = [def, use0, use1]; priority = 10; }
pattern SDivI32 { root = SDiv; result = I32; emit = SDIVWrr; operands = [def, use0, use1]; priority = 10; }
pattern SDivI64 { root = SDiv; result = I64; emit = SDIVXrr; operands = [def, use0, use1]; priority = 10; }
pattern UDivI1 { root = UDiv; result = I1; emit = UDIVWrr; operands = [def, use0, use1]; priority = 10; }
pattern UDivI32 { root = UDiv; result = I32; emit = UDIVWrr; operands = [def, use0, use1]; priority = 10; }
pattern UDivI64 { root = UDiv; result = I64; emit = UDIVXrr; operands = [def, use0, use1]; priority = 10; }

pattern AndI1 { root = And; result = I1; emit = ANDWrr; operands = [def, use0, use1]; priority = 10; }
pattern AndI32 { root = And; result = I32; emit = ANDWrr; operands = [def, use0, use1]; priority = 10; }
pattern AndI64 { root = And; result = I64; emit = ANDXrr; operands = [def, use0, use1]; priority = 10; }
pattern AndV4I32 { root = And; result = V4I32; emit = ANDv16i8; operands = [def, use0, use1]; priority = 10; }
pattern OrI1 { root = Or; result = I1; emit = ORRWrr; operands = [def, use0, use1]; priority = 10; }
pattern OrI32 { root = Or; result = I32; emit = ORRWrr; operands = [def, use0, use1]; priority = 10; }
pattern OrI64 { root = Or; result = I64; emit = ORRXrr; operands = [def, use0, use1]; priority = 10; }
pattern OrV4I32 { root = Or; result = V4I32; emit = ORRv16i8; operands = [def, use0, use1]; priority = 10; }
pattern XorI1 { root = Xor; result = I1; emit = EORWrr; operands = [def, use0, use1]; priority = 10; }
pattern XorI32 { root = Xor; result = I32; emit = EORWrr; operands = [def, use0, use1]; priority = 10; }
pattern XorI64 { root = Xor; result = I64; emit = EORXrr; operands = [def, use0, use1]; priority = 10; }
pattern XorV4I32 { root = Xor; result = V4I32; emit = EORv16i8; operands = [def, use0, use1]; priority = 10; }

pattern ShlI1Imm { root = Shl; result = I1; emit = LSLWri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern ShlI1 { root = Shl; result = I1; emit = LSLWrr; operands = [def, use0, use1]; priority = 10; }
pattern ShlI32Imm { root = Shl; result = I32; emit = LSLWri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern ShlI32 { root = Shl; result = I32; emit = LSLWrr; operands = [def, use0, use1]; priority = 10; }
pattern ShlI64Imm { root = Shl; result = I64; emit = LSLXri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern ShlI64 { root = Shl; result = I64; emit = LSLXrr; operands = [def, use0, use1]; priority = 10; }
pattern ShlV4I32 { root = Shl; result = V4I32; emit = SSHLv4i32; operands = [def, use0, use1]; priority = 10; }
pattern LShrI1Imm { root = LShr; result = I1; emit = LSRWri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern LShrI1 { root = LShr; result = I1; emit = LSRWrr; operands = [def, use0, use1]; priority = 10; }
pattern LShrI32Imm { root = LShr; result = I32; emit = LSRWri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern LShrI32 { root = LShr; result = I32; emit = LSRWrr; operands = [def, use0, use1]; priority = 10; }
pattern LShrI64Imm { root = LShr; result = I64; emit = LSRXri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern LShrI64 { root = LShr; result = I64; emit = LSRXrr; operands = [def, use0, use1]; priority = 10; }
pattern LShrV4I32 { root = LShr; result = V4I32; emit = USHLv4i32; operands = [def, use0, use1]; priority = 10; }
pattern AShrI1Imm { root = AShr; result = I1; emit = ASRWri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern AShrI1 { root = AShr; result = I1; emit = ASRWrr; operands = [def, use0, use1]; priority = 10; }
pattern AShrI32Imm { root = AShr; result = I32; emit = ASRWri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern AShrI32 { root = AShr; result = I32; emit = ASRWrr; operands = [def, use0, use1]; priority = 10; }
pattern AShrI64Imm { root = AShr; result = I64; emit = ASRXri; operands = [def, use0, imm1]; predicate = target_immediate; priority = 20; }
pattern AShrI64 { root = AShr; result = I64; emit = ASRXrr; operands = [def, use0, use1]; priority = 10; }
pattern AShrV4I32 { root = AShr; result = V4I32; emit = SSHLv4i32; operands = [def, use0, use1]; priority = 10; }

pattern FAddF32 { root = FAdd; result = F32; emit = FADDS; operands = [def, use0, use1]; priority = 10; }
pattern FAddV4F32 { root = FAdd; result = V4F32; emit = ADDv4f32; operands = [def, use0, use1]; priority = 10; }
pattern FSubF32 { root = FSub; result = F32; emit = FSUBS; operands = [def, use0, use1]; priority = 10; }
pattern FSubV4F32 { root = FSub; result = V4F32; emit = SUBv4f32; operands = [def, use0, use1]; priority = 10; }
pattern FMulF32 { root = FMul; result = F32; emit = FMULS; operands = [def, use0, use1]; priority = 10; }
pattern FMulV4F32 { root = FMul; result = V4F32; emit = MULv4f32; operands = [def, use0, use1]; priority = 10; }
pattern FDivF32 { root = FDiv; result = F32; emit = FDIVS; operands = [def, use0, use1]; priority = 10; }
pattern FDivV4F32 { root = FDiv; result = V4F32; emit = DIVv4f32; operands = [def, use0, use1]; priority = 10; }
pattern FNegF32 { root = FNeg; result = F32; emit = FNEGS; operands = [def, use0]; priority = 10; }
pattern FNegV4F32 { root = FNeg; result = V4F32; emit = NEGv4f32; operands = [def, use0]; priority = 10; }

pattern ZExtI64I1 { root = ZExt; result = I64; source = I1; emit = UXTW; operands = [def, use0]; priority = 20; }
pattern ZExtI64I32 { root = ZExt; result = I64; source = I32; emit = UXTW; operands = [def, use0]; priority = 20; }
pattern ZExtI1FromI64 { root = ZExt; result = I1; source = I64; emit = COPYXtoW; operands = [def, use0]; priority = 20; }
pattern ZExtI32FromI64 { root = ZExt; result = I32; source = I64; emit = COPYXtoW; operands = [def, use0]; priority = 20; }
pattern ZExtCopy { root = ZExt; result = Any; source = Any; emit = COPY; operands = [def, use0]; priority = 1; }
pattern SExtI64I1 { root = SExt; result = I64; source = I1; emit = SXTW; operands = [def, use0]; priority = 20; }
pattern SExtI64I32 { root = SExt; result = I64; source = I32; emit = SXTW; operands = [def, use0]; priority = 20; }
pattern SExtI1FromI64 { root = SExt; result = I1; source = I64; emit = COPYXtoW; operands = [def, use0]; priority = 20; }
pattern SExtI32FromI64 { root = SExt; result = I32; source = I64; emit = COPYXtoW; operands = [def, use0]; priority = 20; }
pattern SExtCopy { root = SExt; result = Any; source = Any; emit = COPY; operands = [def, use0]; priority = 1; }
pattern TruncI1FromI64 { root = Trunc; result = I1; source = I64; emit = COPYXtoW; operands = [def, use0]; priority = 20; }
pattern TruncI32FromI64 { root = Trunc; result = I32; source = I64; emit = COPYXtoW; operands = [def, use0]; priority = 20; }
pattern TruncI64I1 { root = Trunc; result = I64; source = I1; emit = UXTW; operands = [def, use0]; priority = 20; }
pattern TruncI64I32 { root = Trunc; result = I64; source = I32; emit = UXTW; operands = [def, use0]; priority = 20; }
pattern TruncCopy { root = Trunc; result = Any; source = Any; emit = COPY; operands = [def, use0]; priority = 1; }
pattern BitcastI1FromI64 { root = Bitcast; result = I1; source = I64; emit = COPYXtoW; operands = [def, use0]; priority = 20; }
pattern BitcastI32FromI64 { root = Bitcast; result = I32; source = I64; emit = COPYXtoW; operands = [def, use0]; priority = 20; }
pattern BitcastI64I1 { root = Bitcast; result = I64; source = I1; emit = UXTW; operands = [def, use0]; priority = 20; }
pattern BitcastI64I32 { root = Bitcast; result = I64; source = I32; emit = UXTW; operands = [def, use0]; priority = 20; }
pattern BitcastCopy { root = Bitcast; result = Any; source = Any; emit = COPY; operands = [def, use0]; priority = 1; }
pattern FPToSII32 { root = FPToSI; result = I32; source = F32; emit = FCVTZSW; operands = [def, use0]; priority = 10; }
pattern SIToFPF32 { root = SIToFP; result = F32; source = I32; emit = SCVTFWS; operands = [def, use0]; priority = 10; }
pattern ClzI1 { root = Clz; result = I1; emit = CLZW; operands = [def, use0]; priority = 10; }
pattern ClzI32 { root = Clz; result = I32; emit = CLZW; operands = [def, use0]; priority = 10; }
pattern SplatV4I32 { root = Splat; result = V4I32; emit = DUPv4i32; operands = [def, use0]; priority = 10; }
pattern SplatV4F32 { root = Splat; result = V4F32; emit = DUPv4f32; operands = [def, use0]; priority = 10; }

pattern LoadI1 { root = Load; result = I1; emit = LDRWui; operands = [def, use1, zero]; predicate = not_direct_global; memory = load; priority = 10; }
pattern LoadI1Global { root = Load; result = I1; emit = LDRWlo; operands = [def, use1, global1]; predicate = direct_global; memory = load; priority = 20; }
pattern LoadI32 { root = Load; result = I32; emit = LDRWui; operands = [def, use1, zero]; predicate = not_direct_global; memory = load; priority = 10; }
pattern LoadI32Global { root = Load; result = I32; emit = LDRWlo; operands = [def, use1, global1]; predicate = direct_global; memory = load; priority = 20; }
pattern LoadI64 { root = Load; result = I64; emit = LDRXui; operands = [def, use1, zero]; predicate = not_direct_global; memory = load; priority = 10; }
pattern LoadI64Global { root = Load; result = I64; emit = LDRXlo; operands = [def, use1, global1]; predicate = direct_global; memory = load; priority = 20; }
pattern LoadF32 { root = Load; result = F32; emit = LDRSui; operands = [def, use1, zero]; predicate = not_direct_global; memory = load; priority = 10; }
pattern LoadF32Global { root = Load; result = F32; emit = LDRSlo; operands = [def, use1, global1]; predicate = direct_global; memory = load; priority = 20; }
pattern LoadPtr { root = Load; result = Ptr; emit = LDRXui; operands = [def, use1, zero]; predicate = not_direct_global; memory = load; priority = 10; }
pattern LoadPtrGlobal { root = Load; result = Ptr; emit = LDRXlo; operands = [def, use1, global1]; predicate = direct_global; memory = load; priority = 20; }
pattern LoadV4I32 { root = Load; result = V4I32; emit = LDRQui; operands = [def, use1, zero]; predicate = not_direct_global; memory = load; priority = 10; }
pattern LoadV4I32Global { root = Load; result = V4I32; emit = LDRQlo; operands = [def, use1, global1]; predicate = direct_global; memory = load; priority = 20; }
pattern LoadV4F32 { root = Load; result = V4F32; emit = LDRQui; operands = [def, use1, zero]; predicate = not_direct_global; memory = load; priority = 10; }
pattern LoadV4F32Global { root = Load; result = V4F32; emit = LDRQlo; operands = [def, use1, global1]; predicate = direct_global; memory = load; priority = 20; }

pattern StoreI1 { root = Store; result = Invalid; source = I1; source_index = 1; emit = STRWui; operands = [use1, use2, zero]; predicate = not_direct_global; memory = store; priority = 10; }
pattern StoreI1Global { root = Store; result = Invalid; source = I1; source_index = 1; emit = STRWlo; operands = [use1, use2, global2]; predicate = direct_global; memory = store; priority = 20; }
pattern StoreI32 { root = Store; result = Invalid; source = I32; source_index = 1; emit = STRWui; operands = [use1, use2, zero]; predicate = not_direct_global; memory = store; priority = 10; }
pattern StoreI32Global { root = Store; result = Invalid; source = I32; source_index = 1; emit = STRWlo; operands = [use1, use2, global2]; predicate = direct_global; memory = store; priority = 20; }
pattern StoreI64 { root = Store; result = Invalid; source = I64; source_index = 1; emit = STRXui; operands = [use1, use2, zero]; predicate = not_direct_global; memory = store; priority = 10; }
pattern StoreI64Global { root = Store; result = Invalid; source = I64; source_index = 1; emit = STRXlo; operands = [use1, use2, global2]; predicate = direct_global; memory = store; priority = 20; }
pattern StoreF32 { root = Store; result = Invalid; source = F32; source_index = 1; emit = STRSui; operands = [use1, use2, zero]; predicate = not_direct_global; memory = store; priority = 10; }
pattern StoreF32Global { root = Store; result = Invalid; source = F32; source_index = 1; emit = STRSlo; operands = [use1, use2, global2]; predicate = direct_global; memory = store; priority = 20; }
pattern StorePtr { root = Store; result = Invalid; source = Ptr; source_index = 1; emit = STRXui; operands = [use1, use2, zero]; predicate = not_direct_global; memory = store; priority = 10; }
pattern StorePtrGlobal { root = Store; result = Invalid; source = Ptr; source_index = 1; emit = STRXlo; operands = [use1, use2, global2]; predicate = direct_global; memory = store; priority = 20; }
pattern StoreV4I32 { root = Store; result = Invalid; source = V4I32; source_index = 1; emit = STRQui; operands = [use1, use2, zero]; predicate = not_direct_global; memory = store; priority = 10; }
pattern StoreV4I32Global { root = Store; result = Invalid; source = V4I32; source_index = 1; emit = STRQlo; operands = [use1, use2, global2]; predicate = direct_global; memory = store; priority = 20; }
pattern StoreV4F32 { root = Store; result = Invalid; source = V4F32; source_index = 1; emit = STRQui; operands = [use1, use2, zero]; predicate = not_direct_global; memory = store; priority = 10; }
pattern StoreV4F32Global { root = Store; result = Invalid; source = V4F32; source_index = 1; emit = STRQlo; operands = [use1, use2, global2]; predicate = direct_global; memory = store; priority = 20; }

pattern InsertV4I32Lane { root = InsertElement; result = V4I32; emit = INSv4i32; operands = [def, use0_tied0, use1, imm2]; predicate = lane; priority = 10; }
pattern InsertV4F32Lane { root = InsertElement; result = V4F32; emit = INSv4f32; operands = [def, use0_tied0, use1, imm2]; predicate = lane; priority = 10; }
pattern ExtractI32Lane { root = ExtractElement; result = I32; emit = EXTRACTv4i32; operands = [def, use0, imm1]; predicate = lane; priority = 10; }
pattern ExtractF32Lane { root = ExtractElement; result = F32; emit = EXTRACTv4f32; operands = [def, use0, imm1]; predicate = lane; priority = 10; }
pattern MAddI32 { root = MAdd; result = I32; emit = MADDWrrr; operands = [def, use0, use1, use2]; priority = 10; }
pattern MSubI32 { root = MSub; result = I32; emit = MSUBWrrr; operands = [def, use0, use1, use2]; priority = 10; }
pattern MAddV4I32 { root = MAdd; result = V4I32; emit = MLAv4i32; operands = [def_early, use2_tied0, use0, use1]; priority = 10; }
pattern MSubV4I32 { root = MSub; result = V4I32; emit = MLSv4i32; operands = [def_early, use2_tied0, use0, use1]; priority = 10; }
pattern FMAddV4F32 { root = FMAdd; result = V4F32; emit = FMLAv4f32; operands = [def, use0, use1, use2_tied0]; priority = 10; }
pattern FMSubV4F32 { root = FMSub; result = V4F32; emit = FMLSv4f32; operands = [def, use0, use1, use2_tied0]; priority = 10; }
pattern SMinV4I32 { root = SMin; result = V4I32; emit = SMINv4i32; operands = [def, use0, use1]; priority = 10; }
pattern SMaxV4I32 { root = SMax; result = V4I32; emit = SMAXv4i32; operands = [def, use0, use1]; priority = 10; }
pattern BranchBlock { root = Branch; result = Invalid; emit = B; operands = [block0]; priority = 10; }
pattern ReturnVoid { root = Return; result = Invalid; emit = RET; operands = []; predicate = operand_count_1; priority = 10; }

// IR comparison predicates used by scalar compare/select lowering.
condition ICMP_EQ { family = integer; code = EQ; }
condition ICMP_NE { family = integer; code = NE; }
condition ICMP_UGT { family = integer; code = HI; }
condition ICMP_UGE { family = integer; code = HS; }
condition ICMP_ULT { family = integer; code = LO; }
condition ICMP_ULE { family = integer; code = LS; }
condition ICMP_SGT { family = integer; code = GT; }
condition ICMP_SGE { family = integer; code = GE; }
condition ICMP_SLT { family = integer; code = LT; }
condition ICMP_SLE { family = integer; code = LE; }

condition FCMP_FALSE { family = floating; code = AL; }
condition FCMP_OEQ { family = floating; code = EQ; }
condition FCMP_OGT { family = floating; code = GT; }
condition FCMP_OGE { family = floating; code = GE; }
condition FCMP_OLT { family = floating; code = MI; }
condition FCMP_OLE { family = floating; code = LS; }
condition FCMP_ONE { family = floating; code = NE; }
condition FCMP_ORD { family = floating; code = VC; }
condition FCMP_UNO { family = floating; code = VS; }
condition FCMP_UEQ { family = floating; code = EQ; }
condition FCMP_UGT { family = floating; code = HI; }
condition FCMP_UGE { family = floating; code = PL; }
condition FCMP_ULT { family = floating; code = LT; }
condition FCMP_ULE { family = floating; code = LE; }
condition FCMP_UNE { family = floating; code = NE; }
condition FCMP_TRUE { family = floating; code = AL; }
