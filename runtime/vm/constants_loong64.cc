// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "platform/globals.h"  // NOLINT

#if defined(TARGET_ARCH_LOONG64)

#include "vm/constants.h"  // NOLINT

namespace dart {

const char* const cpu_reg_names[kNumberOfCpuRegisters] = {
    "zero", "ra",  "tp",  "sp",  "a0",   "a1", "a2", "tmp",
    "tmp2", "pp",  "a6",  "a7",  "t0",   "t1", "t2", "t3",
    "t4",   "t5",  "t6",  "wbs", "dt",   "u0", "fp", "null",
    "thr",  "s2",  "s3",  "s4",  "s5",   "s6", "s7", "s8",
};

const char* const cpu_reg_abi_names[kNumberOfCpuRegisters] = {
    "zero", "ra", "tp", "sp", "a0", "a1", "a2", "a3",
    "a4",   "a5", "a6", "a7", "t0", "t1", "t2", "t3",
    "t4",   "t5", "t6", "t7", "t8", "u0", "fp", "s0",
    "s1",   "s2", "s3", "s4", "s5", "s6", "s7", "s8",
};

const char* const fpu_reg_names[kNumberOfFpuRegisters] = {
    "fa0",  "fa1",  "fa2",  "fa3",  "fa4",  "fa5",  "fa6", "fa7",
    "ft0",  "ft1",  "ft2",  "ft3",  "ft4",  "ft5",  "ft6", "ft7",
    "ft8",  "ft9",  "ft10", "ft11", "ft12", "ft13", "ft14", "ft15",
    "fs0",  "fs1",  "fs2",  "fs3",  "fs4",  "fs5",  "fs6", "fs7",
};

const char* const vector_reg_names[kNumberOfVectorRegisters] = {
    "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",  "v8",  "v9",  "v10",
    "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21",
    "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
};

const Register CallingConventions::ArgumentRegisters[] = {
    // A3/A4/A5 are assigned to TMP/TMP2/PP. This assignment is important for
    // reducing code size. To minimize distruption to the rest of the compiler,
    // we tell the register allocator and marshaller use T3/T4/T5 for FFI calls,
    // so they can make use of general moves that assume the availability of
    // TMP/TMP2/PP, and only move them to A3/A4/A5 at the last momement in
    // FfiCallInstr and CCallInstr (and NativeEntryInstr in the opposite
    // direction).
    A0, A1, A2, T3, T4, T5, A6, A7,
};

const FpuRegister CallingConventions::FpuArgumentRegisters[] = {
    FA0, FA1, FA2, FA3, FA4, FA5, FA6, FA7,
};

}  // namespace dart

#endif  // defined(TARGET_ARCH_LOONG64)
