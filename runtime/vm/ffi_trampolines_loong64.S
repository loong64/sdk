// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// FFI trampolines for the simulator and the interpreter.
// This is not written as VM stub because we need it be executable in contexts
// where we cannot JIT. (Alternatively, we could start requiring the VM snapshot
// to be provided in every mode.)

#if defined(__loongarch64) && (defined(SIMULATOR_FFI) || (defined(DART_DYNAMIC_MODULES) && !defined(DART_PRECOMPILED_RUNTIME)))

.text

#if defined(DART_DYNAMIC_MODULES) && !defined(DART_PRECOMPILED_RUNTIME)

.globl FfiCallTrampoline
.type FfiCallTrampoline, %function
FfiCallTrampoline:
  addi.d $sp, $sp, -16
  st.d $fp, $sp, 0
  st.d $ra, $sp, 8

  // Spill callee-saved registers to save args pointer.
  addi.d $sp, $sp, -16
  st.d $s0, $sp, 0
  st.d $s1, $sp, 8
  ori $s0, $a0, 0         // FfiCallArguments* args

  // Copy top frame from Dart stack to C stack.
  ld.d $t0, $s0, 0         // FfiCallArguments.stack_area
  ld.d $t1, $s0, 8         // FfiCallArguments.stack_area_end
.Lcopy1:
  addi.d $t1, $t1, -16
  ld.d $t2, $t1, 0
  ld.d $t3, $t1, 8
  addi.d $sp, $sp, -16
  st.d $t2, $sp, 0
  st.d $t3, $sp, 8
  blt $t0, $t1, .Lcopy1

  // Load the ABI argument registers.
  ld.d $a0, $s0, 16        // FfiCallArguments.cpu_registers[0]
  ld.d $a1, $s0, 24        // FfiCallArguments.cpu_registers[1]
  ld.d $a2, $s0, 32
  ld.d $a3, $s0, 40
  ld.d $a4, $s0, 48
  ld.d $a5, $s0, 56
  ld.d $a6, $s0, 64
  ld.d $a7, $s0, 72
  fld.d $fa0, $s0, 272     // FfiCallArguments.fpu_registers[0]
  fld.d $fa1, $s0, 280
  fld.d $fa2, $s0, 288
  fld.d $fa3, $s0, 296
  fld.d $fa4, $s0, 304
  fld.d $fa5, $s0, 312
  fld.d $fa6, $s0, 320
  fld.d $fa7, $s0, 328

  // Call target.
  ld.d $t0, $s0, 528       // FfiCallArguments.target
  jirl $ra, $t0, 0

  // Save the ABI result registers.
  st.d $a0, $s0, 16        // FfiCallArguments.cpu_registers[0]
  st.d $a1, $s0, 24
  fst.d $fa0, $s0, 272     // FfiCallArguments.fpu_registers[0]
  fst.d $fa1, $s0, 280

  addi.d $sp, $sp, 16
  ld.d $s0, $sp, 0
  ld.d $s1, $sp, 8
  addi.d $sp, $sp, 16
  ld.d $fp, $sp, 0
  ld.d $ra, $sp, 8
  addi.d $sp, $sp, 16
  jirl $zero, $ra, 0
.size FfiCallTrampoline,.-FfiCallTrampoline

#endif  // defined(DART_DYNAMIC_MODULES) && !defined(DART_PRECOMPILED_RUNTIME)

#if defined(SIMULATOR_FFI)

.globl SimulatorFfiCalloutTrampoline
.type SimulatorFfiCalloutTrampoline, %function
SimulatorFfiCalloutTrampoline:
  addi.d $sp, $sp, -16
  st.d $fp, $sp, 0
  st.d $ra, $sp, 8

  // Spill callee-saved registers to save context pointer.
  addi.d $sp, $sp, -16
  st.d $s0, $sp, 0
  st.d $s1, $sp, 8
  ori $s0, $a0, 0

  // Copy top frame from Dart stack to C stack.
  ld.d $t0, $s0, 16        // CalloutContext.simulator_stack_pointer (CSP)
  ld.d $t1, $s0, 24        // CalloutContext.simulator_frame_pointer
  addi.d $t1, $t1, 15      // Round up the frame pointer, since the
  bstrins.d $t1, $zero, 3, 0  // Dart frame pointer is not 16-byte aligned.
.Lcopy2:
  addi.d $t1, $t1, -16
  ld.d $t2, $t1, 0
  ld.d $t3, $t1, 8
  addi.d $sp, $sp, -16
  st.d $t2, $sp, 0
  st.d $t3, $sp, 8
  blt $t0, $t1, .Lcopy2

  // Load the ABI argument registers.
  ld.d $a0, $s0, 32        // CalloutContext.integer_arguments[0]
  ld.d $a1, $s0, 40
  ld.d $a2, $s0, 48
  ld.d $a3, $s0, 56
  ld.d $a4, $s0, 64
  ld.d $a5, $s0, 72
  ld.d $a6, $s0, 80
  ld.d $a7, $s0, 88
  fld.d $fa0, $s0, 96      // CalloutContext.double_arguments[0]
  fld.d $fa1, $s0, 104
  fld.d $fa2, $s0, 112
  fld.d $fa3, $s0, 120
  fld.d $fa4, $s0, 128
  fld.d $fa5, $s0, 136
  fld.d $fa6, $s0, 144
  fld.d $fa7, $s0, 152

  // Call target.
  ld.d $t0, $s0, 168       // CalloutContext.target
  jirl $ra, $t0, 0

  // Save the ABI result registers.
  st.d $a0, $s0, 32        // CalloutContext.integer_arguments[0]
  st.d $a1, $s0, 40
  fst.d $fa0, $s0, 96      // CalloutContext.double_arguments[0]
  fst.d $fa1, $s0, 104

  addi.d $sp, $sp, 16
  ld.d $s0, $sp, 0
  ld.d $s1, $sp, 8
  addi.d $sp, $sp, 16
  ld.d $fp, $sp, 0
  ld.d $ra, $sp, 8
  addi.d $sp, $sp, 16
  jirl $zero, $ra, 0
.size SimulatorFfiCalloutTrampoline,.-SimulatorFfiCalloutTrampoline

.globl SimulatorFfiCallbackTrampoline
.type SimulatorFfiCallbackTrampoline, %function
SimulatorFfiCallbackTrampoline:
.Lstart:
#if defined(__Fuchsia__)
  .rept 483  // FfiCallbackMetadata::NumCallbackTrampolinesPerPage()
#else
  .rept 488  // FfiCallbackMetadata::NumCallbackTrampolinesPerPage()
#endif
  pcaddu12i $t1, 0
  b .Lbody
  .endr
.Lbody:
  ori $t3, $sp, 0
  addi.d $sp, $sp, -16
  st.d $fp, $sp, 0
  st.d $ra, $sp, 8

  addi.d $sp, $sp, -144  // Allocate CallbackContext

  // Save ABI argument registers.
  st.d $a0, $sp, 0        // CallbackContext.integer_arguments[0]
  st.d $a1, $sp, 8
  st.d $a2, $sp, 16
  st.d $a3, $sp, 24
  st.d $a4, $sp, 32
  st.d $a5, $sp, 40
  st.d $a6, $sp, 48
  st.d $a7, $sp, 56
  fst.d $fa0, $sp, 64     // CallbackContext.double_arguments[0]
  fst.d $fa1, $sp, 72
  fst.d $fa2, $sp, 80
  fst.d $fa3, $sp, 88
  fst.d $fa4, $sp, 96
  fst.d $fa5, $sp, 104
  fst.d $fa6, $sp, 112
  fst.d $fa7, $sp, 120
  st.d $t3, $sp, 136      // CallbackContext.sp

  // Pass arguments registers and thunk address to the runtime.
  ori $a0, $sp, 0          // CallbackContext*
  ori $a1, $t1, 0          // I.e., which callback.

  pcaddu12i $a2, 0
  // Align to page and add RW offset
  bstrins.d $a2, $zero, 11, 0   // ~(0x1000 - 1) for 4KB pages
  addi.d $a2, $a2, 0x2000       // RW page (2 * kPageSize)
  ld.d $a2, $a2, 8              // FfiCallbackMetadata::kDoRedirectedFfiCallback
  jirl $ra, $a2, 0

  // Load ABI result registers.
  ld.d $a0, $sp, 0        // CallbackContext.integer_arguments[0]
  ld.d $a1, $sp, 8
  fld.d $fa0, $sp, 64     // CallbackContext.double_arguments[0]
  fld.d $fa1, $sp, 72

  addi.d $sp, $sp, 144
  ld.d $fp, $sp, 0
  ld.d $ra, $sp, 8
  addi.d $sp, $sp, 16
  jirl $zero, $ra, 0
.size SimulatorFfiCallbackTrampoline,.-SimulatorFfiCallbackTrampoline

.globl SimulatorFfiCallbackTrampolineEnd
.type SimulatorFfiCallbackTrampolineEnd, %function
SimulatorFfiCallbackTrampolineEnd:
  break 0

#endif  // defined(SIMULATOR_FFI)

#endif  // defined(__loongarch64) && (defined(SIMULATOR_FFI) || ...)