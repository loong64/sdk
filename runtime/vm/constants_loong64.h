// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_CONSTANTS_LOONG64_H_
#define RUNTIME_VM_CONSTANTS_LOONG64_H_

#ifndef RUNTIME_VM_CONSTANTS_H_
#error Do not include constants_loong64.h directly; use constants.h instead.
#endif

#include <sstream>

#include "platform/assert.h"
#include "platform/globals.h"
#include "platform/utils.h"

#include "vm/constants_base.h"
#include "vm/flags.h"

namespace dart {

#if defined(TARGET_ARCH_LOONG64)
typedef uint64_t uintx_t;
typedef int64_t intx_t;
constexpr intx_t kMaxIntX = kMaxInt64;
constexpr uintx_t kMaxUIntX = kMaxUint64;
constexpr intx_t kMinIntX = kMinInt64;
#define XLEN 64
#else
#error This header should only be included for TARGET_ARCH_LOONG64.
#endif

enum Register {
  ZR = 0,
  RA = 1,
  TP = 2,
  SP = 3,
  A0 = 4,
  A1 = 5,
  A2 = 6,   // CODE_REG
  A3 = 7,   // TMP
  A4 = 8,   // TMP2
  A5 = 9,   // PP, untagged
  A6 = 10,
  A7 = 11,
  T0 = 12,  // RA2
  T1 = 13,
  T2 = 14,  // FAR_TMP, EXPECTED_LANDING_PAD
  T3 = 15,
  T4 = 16,
  T5 = 17,
  T6 = 18,
  T7 = 19,
  T8 = 20,
  U0 = 21,
  FP = 22,
  S0 = 23,
  S1 = 24,  // THR
  S2 = 25,
  S3 = 26,
  S4 = 27,  // ARGS_DESC_REG
  S5 = 28,  // IC_DATA_REG
  S6 = 29,
  S7 = 30,  // CALLEE_SAVED_TEMP
  S8 = 31,
  kNumberOfCpuRegisters = 32,
  kNoRegister = -1,

  RA2 = T0,
};

enum FRegister {
  FA0 = 0,
  FA1 = 1,
  FA2 = 2,
  FA3 = 3,
  FA4 = 4,
  FA5 = 5,
  FA6 = 6,
  FA7 = 7,
  FT0 = 8,
  FT1 = 9,
  FT2 = 10,
  FT3 = 11,
  FT4 = 12,
  FT5 = 13,
  FT6 = 14,
  FT7 = 15,
  FT8 = 16,
  FT9 = 17,
  FT10 = 18,
  FT11 = 19,
  FT12 = 20,
  FT13 = 21,
  FT14 = 22,
  FT15 = 23,
  FS0 = 24,
  FS1 = 25,
  FS2 = 26,
  FS3 = 27,
  FS4 = 28,
  FS5 = 29,
  FS6 = 30,
  FS7 = 31,
  kNumberOfFpuRegisters = 32,
  kNoFpuRegister = -1,
};

enum VRegister {
  V0 = 0,
  V1 = 1,
  V2 = 2,
  V3 = 3,
  V4 = 4,
  V5 = 5,
  V6 = 6,
  V7 = 7,
  V8 = 8,
  V9 = 9,
  V10 = 10,
  V11 = 11,
  V12 = 12,
  V13 = 13,
  V14 = 14,
  V15 = 15,
  V16 = 16,
  V17 = 17,
  V18 = 18,
  V19 = 19,
  V20 = 20,
  V21 = 21,
  V22 = 22,
  V23 = 23,
  V24 = 24,
  V25 = 25,
  V26 = 26,
  V27 = 27,
  V28 = 28,
  V29 = 29,
  V30 = 30,
  V31 = 31,
  kNumberOfVectorRegisters = 32,
  kNoVectorRegister = -1,
};

// Register alias for floating point scratch register.
const FRegister FTMP = FT15;

// Architecture independent aliases.
typedef FRegister FpuRegister;
const FpuRegister FpuTMP = FTMP;
const int kFpuRegisterSize = 8;
typedef double fpu_register_t;

extern const char* const cpu_reg_names[kNumberOfCpuRegisters];
extern const char* const cpu_reg_abi_names[kNumberOfCpuRegisters];
extern const char* const fpu_reg_names[kNumberOfFpuRegisters];
extern const char* const vector_reg_names[kNumberOfVectorRegisters];

// Register aliases.
constexpr Register TMP = A3;  // Used as scratch register by assembler.
constexpr Register TMP2 = A4;
constexpr Register FAR_TMP = T2;
constexpr Register PP = A5;  // Caches object pool pointer in generated code.
constexpr Register DISPATCH_TABLE_REG = T8;  // Dispatch table register.
constexpr Register CODE_REG = A2;
// Set when calling Dart functions in JIT mode, used by LazyCompileStub.
constexpr Register FUNCTION_REG = T0;
constexpr Register FPREG = FP;          // Frame pointer register.
constexpr Register SPREG = SP;          // Stack pointer register.
constexpr Register IC_DATA_REG = S5;    // ICData/MegamorphicCache register.
constexpr Register ARGS_DESC_REG = S4;  // Arguments descriptor register.
constexpr Register THR = S1;  // Caches current thread in generated code.
constexpr Register CALLEE_SAVED_TEMP = S7;
constexpr Register WRITE_BARRIER_STATE = T7;
constexpr Register NULL_REG = S0;  // Caches NullObject() value.
#define DART_ASSEMBLER_HAS_NULL_REG 1

// ABI for catch-clause entry point.
constexpr Register kExceptionObjectReg = A0;
constexpr Register kStackTraceObjectReg = A1;

// ABI for write barrier stub.
constexpr Register kWriteBarrierObjectReg = A0;
constexpr Register kWriteBarrierValueReg = A1;
constexpr Register kWriteBarrierSlotReg = A6;

// Common ABI for shared slow path stubs.
struct SharedSlowPathStubABI {
  static constexpr Register kResultReg = A0;
};

// ABI for instantiation stubs.
struct InstantiationABI {
  static constexpr Register kUninstantiatedTypeArgumentsReg = T1;
  static constexpr Register kInstantiatorTypeArgumentsReg = S8;
  static constexpr Register kFunctionTypeArgumentsReg = T3;
  static constexpr Register kResultTypeArgumentsReg = A0;
  static constexpr Register kResultTypeReg = A0;
  static constexpr Register kScratchReg = T4;
};

// Registers in addition to those listed in InstantiationABI used inside the
// implementation of the InstantiateTypeArguments stubs.
struct InstantiateTAVInternalRegs {
  // The set of registers that must be pushed/popped when probing a hash-based
  // cache due to overlap with the registers in InstantiationABI.
  static constexpr intptr_t kSavedRegisters = 0;

  // Additional registers used to probe hash-based caches.
  static constexpr Register kEntryStartReg = S2;
  static constexpr Register kProbeMaskReg = S3;
  static constexpr Register kProbeDistanceReg = S4;
  static constexpr Register kCurrentEntryIndexReg = S5;
};

// Registers in addition to those listed in TypeTestABI used inside the
// implementation of type testing stubs that are _not_ preserved.
struct TTSInternalRegs {
  static constexpr Register kInstanceTypeArgumentsReg = S2;
  static constexpr Register kScratchReg = S3;
  static constexpr Register kSubTypeArgumentReg = S4;
  static constexpr Register kSuperTypeArgumentReg = S5;

  // Must be pushed/popped whenever generic type arguments are being checked as
  // they overlap with registers in TypeTestABI.
  static constexpr intptr_t kSavedTypeArgumentRegisters = 0;

  static constexpr intptr_t kInternalRegisters =
      ((1 << kInstanceTypeArgumentsReg) | (1 << kScratchReg) |
       (1 << kSubTypeArgumentReg) | (1 << kSuperTypeArgumentReg)) &
      ~kSavedTypeArgumentRegisters;
};

// Registers in addition to those listed in TypeTestABI used inside the
// implementation of subtype test cache stubs that are _not_ preserved.
struct STCInternalRegs {
  static constexpr Register kInstanceCidOrSignatureReg = S2;
  static constexpr Register kInstanceInstantiatorTypeArgumentsReg = S3;
  static constexpr Register kInstanceParentFunctionTypeArgumentsReg = S4;
  static constexpr Register kInstanceDelayedFunctionTypeArgumentsReg = S5;
  static constexpr Register kCacheEntriesEndReg = S6;
  static constexpr Register kCacheContentsSizeReg = A6;
  static constexpr Register kProbeDistanceReg = A7;

  static constexpr intptr_t kInternalRegisters =
      (1 << kInstanceCidOrSignatureReg) |
      (1 << kInstanceInstantiatorTypeArgumentsReg) |
      (1 << kInstanceParentFunctionTypeArgumentsReg) |
      (1 << kInstanceDelayedFunctionTypeArgumentsReg) |
      (1 << kCacheEntriesEndReg) | (1 << kCacheContentsSizeReg) |
      (1 << kProbeDistanceReg);
};

// Calling convention when calling TypeTestingStub and SubtypeTestCacheStub.
struct TypeTestABI {
  static constexpr Register kInstanceReg = A0;
  static constexpr Register kDstTypeReg = T1;
  static constexpr Register kInstantiatorTypeArgumentsReg = S8;
  static constexpr Register kFunctionTypeArgumentsReg = T3;
  static constexpr Register kSubtypeTestCacheReg = T4;
  static constexpr Register kScratchReg = T5;

  // For calls to SubtypeNTestCacheStub. Must be distinct from the registers
  // listed above.
  static constexpr Register kSubtypeTestCacheResultReg = T0;
  // For calls to InstanceOfStub.
  static constexpr Register kInstanceOfResultReg = kInstanceReg;

  static constexpr intptr_t kPreservedAbiRegisters =
      (1 << kInstanceReg) | (1 << kDstTypeReg) |
      (1 << kInstantiatorTypeArgumentsReg) | (1 << kFunctionTypeArgumentsReg);

  static constexpr intptr_t kNonPreservedAbiRegisters =
      TTSInternalRegs::kInternalRegisters |
      STCInternalRegs::kInternalRegisters | (1 << kSubtypeTestCacheReg) |
      (1 << kScratchReg) | (1 << kSubtypeTestCacheResultReg) | (1 << CODE_REG);

  static constexpr intptr_t kAbiRegisters =
      kPreservedAbiRegisters | kNonPreservedAbiRegisters;
};

// Calling convention when calling AssertSubtypeStub.
struct AssertSubtypeABI {
  static constexpr Register kSubTypeReg = T1;
  static constexpr Register kSuperTypeReg = S8;
  static constexpr Register kInstantiatorTypeArgumentsReg = T3;
  static constexpr Register kFunctionTypeArgumentsReg = T4;
  static constexpr Register kDstNameReg = T5;

  static constexpr intptr_t kAbiRegisters =
      (1 << kSubTypeReg) | (1 << kSuperTypeReg) |
      (1 << kInstantiatorTypeArgumentsReg) | (1 << kFunctionTypeArgumentsReg) |
      (1 << kDstNameReg);

  // No result register, as AssertSubtype is only run for side effect
  // (throws if the subtype check fails).
};

// ABI for InitStaticFieldStub.
struct InitStaticFieldABI {
  static constexpr Register kFieldReg = S8;
  static constexpr Register kResultReg = A0;
};

// Registers used inside the implementation of InitLateStaticFieldStub.
struct InitLateStaticFieldInternalRegs {
  static constexpr Register kAddressReg = T3;
  static constexpr Register kScratchReg = T4;
};

// ABI for InitInstanceFieldStub.
struct InitInstanceFieldABI {
  static constexpr Register kInstanceReg = T1;
  static constexpr Register kFieldReg = S8;
  static constexpr Register kResultReg = A0;
};

// Registers used inside the implementation of InitLateInstanceFieldStub.
struct InitLateInstanceFieldInternalRegs {
  static constexpr Register kAddressReg = T3;
  static constexpr Register kScratchReg = T4;
};

// ABI for LateInitializationError stubs.
struct LateInitializationErrorABI {
  static constexpr Register kFieldReg = S8;
};

// ABI for FieldAccessError stubs.
struct FieldAccessErrorABI {
  static constexpr Register kFieldReg = S8;
};

// ABI for ThrowStub.
struct ThrowABI {
  static constexpr Register kExceptionReg = A0;
};

// ABI for ReThrowStub.
struct ReThrowABI {
  static constexpr Register kExceptionReg = A0;
  static constexpr Register kStackTraceReg = A1;
};

// ABI for RangeErrorStub.
struct RangeErrorABI {
  static constexpr Register kLengthReg = T1;
  static constexpr Register kIndexReg = S8;
};

// ABI for AllocateObjectStub.
struct AllocateObjectABI {
  static constexpr Register kResultReg = A0;
  static constexpr Register kTypeArgumentsReg = A1;
  static constexpr Register kTagsReg = S8;
};

// ABI for AllocateClosureStub.
struct AllocateClosureABI {
  static constexpr Register kResultReg = AllocateObjectABI::kResultReg;
  static constexpr Register kFunctionReg = T1;
  static constexpr Register kLengthAndFlagsReg = S8;
  static constexpr Register kContextReg = T3;
  static constexpr Register kScratchReg = T4;
};

// ABI for AllocateMintShared*Stub.
struct AllocateMintABI {
  static constexpr Register kResultReg = AllocateObjectABI::kResultReg;
  static constexpr Register kTempReg = S8;
};

// ABI for Allocate{Mint,Double,Float32x4,Float64x2}Stub.
struct AllocateBoxABI {
  static constexpr Register kResultReg = AllocateObjectABI::kResultReg;
  static constexpr Register kTempReg = S8;
};

// ABI for AllocateArrayStub.
struct AllocateArrayABI {
  static constexpr Register kResultReg = AllocateObjectABI::kResultReg;
  static constexpr Register kLengthReg = S8;
  static constexpr Register kTypeArgumentsReg = T1;
};

// ABI for AllocateRecordStub.
struct AllocateRecordABI {
  static constexpr Register kResultReg = AllocateObjectABI::kResultReg;
  static constexpr Register kShapeReg = T1;
  static constexpr Register kTemp1Reg = S8;
  static constexpr Register kTemp2Reg = T3;
};

// ABI for AllocateSmallRecordStub (AllocateRecord2, AllocateRecord2Named,
// AllocateRecord3, AllocateRecord3Named).
struct AllocateSmallRecordABI {
  static constexpr Register kResultReg = AllocateObjectABI::kResultReg;
  static constexpr Register kShapeReg = S8;
  static constexpr Register kValue0Reg = T3;
  static constexpr Register kValue1Reg = T4;
  static constexpr Register kValue2Reg = A1;
  static constexpr Register kTempReg = T1;
};

// ABI for AllocateTypedDataArrayStub.
struct AllocateTypedDataArrayABI {
  static constexpr Register kResultReg = AllocateObjectABI::kResultReg;
  static constexpr Register kLengthReg = S8;
};

// ABI for BoxDoubleStub.
struct BoxDoubleStubABI {
  static constexpr FpuRegister kValueReg = FA0;
  static constexpr Register kTempReg = T1;
  static constexpr Register kResultReg = A0;
};

// ABI for DoubleToIntegerStub.
struct DoubleToIntegerStubABI {
  static constexpr FpuRegister kInputReg = FA0;
  static constexpr Register kRecognizedKindReg = T1;
  static constexpr Register kResultReg = A0;
};

// ABI for CheckedStoreIntoSharedStub.
struct CheckedStoreIntoSharedStubABI {
  static constexpr Register kFieldReg = T1;
  static constexpr Register kValueReg = S8;
  static constexpr Register kResultReg = A0;
};

// ABI for EnsureDeeplyImmutableStub.
struct EnsureDeeplyImmutableStubABI {
  static constexpr Register kValueReg = A0;
  static constexpr Register kTempReg = T1;
};

// ABI for SuspendStub (AwaitStub, AwaitWithTypeCheckStub, YieldAsyncStarStub,
// SuspendSyncStarAtStartStub, SuspendSyncStarAtYieldStub).
struct SuspendStubABI {
  static constexpr Register kArgumentReg = A0;
  static constexpr Register kTypeArgsReg = T0;  // Can be the same as kTempReg
  static constexpr Register kTempReg = T0;
  static constexpr Register kFrameSizeReg = T1;
  static constexpr Register kSuspendStateReg = S8;
  static constexpr Register kFunctionDataReg = T3;
  static constexpr Register kSrcFrameReg = T4;
  static constexpr Register kDstFrameReg = T5;

  // Number of bytes to skip after
  // suspend stub return address in order to resume.
  static constexpr intptr_t kResumePcDistance = 0;
};

// ABI for InitSuspendableFunctionStub (InitAsyncStub, InitAsyncStarStub,
// InitSyncStarStub).
struct InitSuspendableFunctionStubABI {
  static constexpr Register kTypeArgsReg = A0;
};

// ABI for ResumeStub
struct ResumeStubABI {
  static constexpr Register kSuspendStateReg = T1;
  static constexpr Register kTempReg = T0;
  // Registers for the frame copying (the 1st part).
  static constexpr Register kFrameSizeReg = S8;
  static constexpr Register kSrcFrameReg = T3;
  static constexpr Register kDstFrameReg = T4;
  // Registers for control transfer.
  // (the 2nd part, can reuse registers from the 1st part)
  static constexpr Register kResumePcReg = S8;
  // Can also reuse kSuspendStateReg but should not conflict with CODE_REG/PP.
  static constexpr Register kExceptionReg = T3;
  static constexpr Register kStackTraceReg = T4;
};

// ABI for ReturnStub (ReturnAsyncStub, ReturnAsyncNotFutureStub,
// ReturnAsyncStarStub).
struct ReturnStubABI {
  static constexpr Register kSuspendStateReg = T1;
};

// ABI for AsyncExceptionHandlerStub.
struct AsyncExceptionHandlerStubABI {
  static constexpr Register kSuspendStateReg = T1;
};

// ABI for CloneSuspendStateStub.
struct CloneSuspendStateStubABI {
  static constexpr Register kSourceReg = A0;
  static constexpr Register kDestinationReg = A1;
  static constexpr Register kTempReg = T0;
  static constexpr Register kFrameSizeReg = T1;
  static constexpr Register kSrcFrameReg = S8;
  static constexpr Register kDstFrameReg = T3;
};

// ABI for FfiAsyncCallbackSendStub.
struct FfiAsyncCallbackSendStubABI {
  static constexpr Register kArgsReg = A0;
};

// ABI for DispatchTableNullErrorStub and consequently for all dispatch
// table calls (though normal functions will not expect or use this
// register). This ABI is added to distinguish memory corruption errors from
// null errors.
struct DispatchTableNullErrorABI {
  static constexpr Register kClassIdReg = A2;
};

typedef uint32_t RegList;
const RegList kAllCpuRegistersList = 0xFFFFFFFF;
const RegList kAllFpuRegistersList = 0xFFFFFFFF;

#define R(reg) (static_cast<RegList>(1) << (reg))

// C++ ABI call registers.

constexpr RegList kAbiArgumentCpuRegs =
    R(A0) | R(A1) | R(A2) | R(A3) | R(A4) | R(A5) | R(A6) | R(A7);
constexpr RegList kAbiVolatileCpuRegs =
    kAbiArgumentCpuRegs | R(T0) | R(T1) | R(T2) | R(T3) | R(T4) | R(T5) |
    R(T6) | R(T7) | R(T8) | R(RA);
constexpr RegList kAbiPreservedCpuRegs =
    R(FP) | R(S0) | R(S1) | R(S2) | R(S3) | R(S4) | R(S5) | R(S6) | R(S7) |
    R(S8);
constexpr int kAbiPreservedCpuRegCount = 10;

constexpr RegList kReservedCpuRegisters =
    R(ZR) | R(TP) | R(U0) | R(SP) | R(FP) | R(TMP) | R(TMP2) | R(PP) | R(THR) |
    R(RA) | R(WRITE_BARRIER_STATE) | R(NULL_REG) | R(DISPATCH_TABLE_REG) |
    R(FAR_TMP);
constexpr intptr_t kNumberOfReservedCpuRegisters =
    Utils::CountOneBits32(kReservedCpuRegisters);
// CPU registers available to Dart allocator.
constexpr RegList kDartAvailableCpuRegs =
    kAllCpuRegistersList & ~kReservedCpuRegisters;
constexpr int kNumberOfDartAvailableCpuRegs =
    kNumberOfCpuRegisters - kNumberOfReservedCpuRegisters;
constexpr int kRegisterAllocationBias = 4;
// Registers available to Dart that are not preserved by runtime calls.
constexpr RegList kDartVolatileCpuRegs =
    kDartAvailableCpuRegs & ~kAbiPreservedCpuRegs;

constexpr RegList kAbiArgumentFpuRegs =
    R(FA0) | R(FA1) | R(FA2) | R(FA3) | R(FA4) | R(FA5) | R(FA6) | R(FA7);
constexpr RegList kAbiVolatileFpuRegs =
    kAbiArgumentFpuRegs | R(FT0) | R(FT1) | R(FT2) | R(FT3) | R(FT4) | R(FT5) |
    R(FT6) | R(FT7) | R(FT8) | R(FT9) | R(FT10) | R(FT11) | R(FT12) |
    R(FT13) | R(FT14) | R(FT15);
constexpr RegList kAbiPreservedFpuRegs =
    R(FS0) | R(FS1) | R(FS2) | R(FS3) | R(FS4) | R(FS5) | R(FS6) | R(FS7);
constexpr int kAbiPreservedFpuRegCount = 8;
constexpr intptr_t kReservedFpuRegisters = 0;
constexpr intptr_t kNumberOfReservedFpuRegisters = 0;
constexpr RegList kDartVolatileFpuRegs = kAbiVolatileFpuRegs & ~R(FpuTMP);

constexpr int kStoreBufferWrapperSize = 26;

class CallingConventions {
 public:
  static constexpr intptr_t kArgumentRegisters = kAbiArgumentCpuRegs;
  static const Register ArgumentRegisters[];
  static constexpr intptr_t kNumArgRegs = 8;
  static constexpr Register kPointerToReturnStructRegisterCall = A0;
  static constexpr Register kPointerToReturnStructRegisterReturn = A0;

  static const FpuRegister FpuArgumentRegisters[];
  static constexpr intptr_t kFpuArgumentRegisters =
      R(FA0) | R(FA1) | R(FA2) | R(FA3) | R(FA4) | R(FA5) | R(FA6) | R(FA7);
  static constexpr intptr_t kNumFpuArgRegs = 8;

  static constexpr bool kArgumentIntRegXorFpuReg = false;

  static constexpr intptr_t kCalleeSaveCpuRegisters = kAbiPreservedCpuRegs;

  // Whether larger than wordsize arguments are aligned to even registers.
  static constexpr AlignmentStrategy kArgumentRegisterAlignment =
      kAlignedToWordSize;
  static constexpr AlignmentStrategy kArgumentRegisterAlignmentVarArgs =
      kAlignedToWordSizeAndValueSize;

  // How stack arguments are aligned.
  static constexpr AlignmentStrategy kArgumentStackAlignment =
      kAlignedToWordSizeAndValueSize;
  static constexpr AlignmentStrategy kArgumentStackAlignmentVarArgs =
      kArgumentStackAlignment;

  // How fields in compounds are aligned.
  static constexpr AlignmentStrategy kFieldAlignment = kAlignedToValueSize;

  // Whether 1 or 2 byte-sized arguments or return values are passed extended
  // to 4 bytes.
  static constexpr ExtensionStrategy kReturnRegisterExtension = kExtendedTo8;
  static constexpr ExtensionStrategy kArgumentRegisterExtension = kExtendedTo8;
  static constexpr ExtensionStrategy kArgumentStackExtension = kExtendedTo8;

  static constexpr Register kReturnReg = A0;
  static constexpr Register kSecondReturnReg = A1;
  static constexpr FpuRegister kReturnFpuReg = FA0;
  static constexpr FpuRegister kSecondReturnFpuReg = FA1;

  // S0=FP, S1=THR
  static constexpr Register kFfiAnyNonAbiRegister = S2;
  static constexpr Register kFirstNonArgumentRegister = T0;
  static constexpr Register kSecondNonArgumentRegister = T1;
  static constexpr Register kStackPointerRegister = SPREG;

  COMPILE_ASSERT(
      ((R(kFirstNonArgumentRegister) | R(kSecondNonArgumentRegister)) &
       (kArgumentRegisters | R(kPointerToReturnStructRegisterCall))) == 0);
};

// Register based calling convention used for Dart functions.
//
// See |compiler::ComputeCallingConvention| for more details.
struct DartCallingConvention {
  // A2-A5 have conflicting uses. The order should be revisited once we
  // implement Location::MayBeSameAsInput, likely putting A0 first.
  static constexpr Register kCpuRegistersForArgs[] = {A1, A6, A0, A7};
  static constexpr FpuRegister kFpuRegistersForArgs[] = {FA0, FA1, FA2, FA3};
};

// TODO(loong64): Architecture-independent parts of the compiler should use
// compare-and-branch instead of condition codes.
enum Condition {
  kNoCondition = -1,
  EQ = 0,   // equal
  NE = 1,   // not equal
  CS = 2,   // carry set/unsigned higher or same
  CC = 3,   // carry clear/unsigned lower
  MI = 4,   // minus/negative
  PL = 5,   // plus/positive or zero
  VS = 6,   // overflow
  VC = 7,   // no overflow
  HI = 8,   // unsigned higher
  LS = 9,   // unsigned lower or same
  GE = 10,  // signed greater than or equal
  LT = 11,  // signed less than
  GT = 12,  // signed greater than
  LE = 13,  // signed less than or equal
  AL = 14,  // always (unconditional)
  NV = 15,  // special condition (refer to section C1.2.3)
  kNumberOfConditions = 16,

  // Platform-independent variants declared for all platforms
  EQUAL = EQ,
  ZERO = EQUAL,
  NOT_EQUAL = NE,
  NOT_ZERO = NOT_EQUAL,
  LESS = LT,
  LESS_EQUAL = LE,
  GREATER_EQUAL = GE,
  GREATER = GT,
  UNSIGNED_LESS = CC,
  UNSIGNED_LESS_EQUAL = LS,
  UNSIGNED_GREATER = HI,
  UNSIGNED_GREATER_EQUAL = CS,
  OVERFLOW = VS,
  NO_OVERFLOW = VC,

  kInvalidCondition = 16
};

static inline Condition InvertCondition(Condition c) {
  COMPILE_ASSERT((EQ ^ NE) == 1);
  COMPILE_ASSERT((CS ^ CC) == 1);
  COMPILE_ASSERT((MI ^ PL) == 1);
  COMPILE_ASSERT((VS ^ VC) == 1);
  COMPILE_ASSERT((HI ^ LS) == 1);
  COMPILE_ASSERT((GE ^ LT) == 1);
  COMPILE_ASSERT((GT ^ LE) == 1);
  COMPILE_ASSERT((AL ^ NV) == 1);
  ASSERT(c != AL);
  ASSERT(c != kInvalidCondition);
  return static_cast<Condition>(c ^ 1);
}

enum ScaleFactor {
  TIMES_1 = 0,
  TIMES_2 = 1,
  TIMES_4 = 2,
  TIMES_8 = 3,
  TIMES_16 = 4,
// We can't include vm/compiler/runtime_api.h, so just be explicit instead
// of using (dart::)kWordSizeLog2.
#if defined(TARGET_ARCH_IS_64_BIT)
  // Used for Smi-boxed indices.
  TIMES_HALF_WORD_SIZE = kInt64SizeLog2 - 1,
  // Used for unboxed indices.
  TIMES_WORD_SIZE = kInt64SizeLog2,
#elif defined(TARGET_ARCH_IS_32_BIT)
  // Used for Smi-boxed indices.
  TIMES_HALF_WORD_SIZE = kInt32SizeLog2 - 1,
  // Used for unboxed indices.
  TIMES_WORD_SIZE = kInt32SizeLog2,
#else
#error "Unexpected word size"
#endif
#if !defined(DART_COMPRESSED_POINTERS)
  TIMES_COMPRESSED_WORD_SIZE = TIMES_WORD_SIZE,
#else
  TIMES_COMPRESSED_WORD_SIZE = TIMES_HALF_WORD_SIZE,
#endif
  // Used for Smi-boxed indices.
  TIMES_COMPRESSED_HALF_WORD_SIZE = TIMES_COMPRESSED_WORD_SIZE - 1,
};

const uword kBreakInstructionFiller = 0x002a0000; // break 0

inline int32_t SignExtend(int N, int32_t value) {
  return static_cast<int32_t>(static_cast<uint32_t>(value) << (32 - N)) >>
         (32 - N);
}

inline intx_t sign_extend(int8_t x) {
  return static_cast<intx_t>(x);
}
inline intx_t sign_extend(int16_t x) {
  return static_cast<intx_t>(x);
}
inline intx_t sign_extend(int32_t x) {
  return static_cast<intx_t>(x);
}
inline intx_t sign_extend(int64_t x) {
  return static_cast<intx_t>(x);
}
inline intx_t sign_extend(uint8_t x) {
  return static_cast<intx_t>(static_cast<int8_t>(x));
}
inline intx_t sign_extend(uint16_t x) {
  return static_cast<intx_t>(static_cast<int16_t>(x));
}
inline intx_t sign_extend(uint32_t x) {
  return static_cast<intx_t>(static_cast<int32_t>(x));
}
inline intx_t sign_extend(uint64_t x) {
  return static_cast<intx_t>(static_cast<int64_t>(x));
}

// LoongArch register bit positions in instruction encoding:
// rd = bits[4:0], rj = bits[9:5], rk = bits[14:10]
// fd = bits[4:0], fj = bits[9:5], fk = bits[14:10]
// vd = bits[4:0], vj = bits[9:5], vk = bits[14:10]
// Note: LoongArch does not use the RISC-V opcode/funct encoding scheme.
// Instruction encodings are handled directly in assembler_loong64.cc.

// LoongArch opcode values (6-bit primary opcode at bits[31:26])
enum LoongArchOpcode {
  LA_OP_ALU_IMM   = 0,     // 000000: ADDI.W, ADDI.D, SLTI, etc.
  LA_OP_ADDU16ID  = 0x04,  // 000100
  LA_OP_LU12IW    = 0x05,  // 000101
  LA_OP_LU32ID    = 0x06,  // 000110
  LA_OP_PCADDU12I = 0x06,  // 000110: same opcode as LU32ID, distinguished by bit[24]
  LA_OP_PCALAU12I = 0x06,  // 000110: same base, bit[24]=1 for pcalau12i vs lu32id
  LA_OP_PCADDU18I = 0x07,  // 000111
  LA_OP_LS        = 0x0A,  // 001010: LD/ST group
  LA_OP_FP_LS     = 0x0B,  // 001011: FP load/store
  LA_OP_BEQZ      = 0x10,  // 010000
  LA_OP_BNEZ      = 0x11,  // 010001
  LA_OP_JIRL      = 0x13,  // 010011
  LA_OP_B         = 0x14,  // 010100
  LA_OP_BL        = 0x15,  // 010101
  LA_OP_BEQ       = 0x16,  // 010110
  LA_OP_BNE       = 0x17,  // 010111
  LA_OP_BLT       = 0x18,  // 011000
  LA_OP_BGE       = 0x19,  // 011001
  LA_OP_BLTU      = 0x1A,  // 011010
  LA_OP_BGEU      = 0x1B,  // 011011
  LA_OP_BCEQZ     = 0x1C,  // 011100
  LA_OP_BCNEZ     = 0x1D,  // 011101
  LA_OP_LU52ID    = 0x0C,  // 001100
};

// Extract 6-bit opcode from an instruction encoding
inline uint32_t LAOpcode(uint32_t encoding) { return (encoding >> 26) & 0x3f; }
// Encode 6-bit opcode into bits[31:26]
inline uint32_t EncodeLAOpcode(LoongArchOpcode op) { return static_cast<uint32_t>(op) << 26; }

// LoongArch field extraction helpers for instruction decoding
inline uint32_t ExtractRd(uint32_t encoding) { return (encoding >> 0) & 0x1f; }
inline uint32_t ExtractRj(uint32_t encoding) { return (encoding >> 5) & 0x1f; }
inline uint32_t ExtractRk(uint32_t encoding) { return (encoding >> 10) & 0x1f; }
inline int32_t ExtractSi12(uint32_t encoding) {
  uint32_t v = (encoding >> 10) & 0xfff;
  return (v & 0x800) ? static_cast<int32_t>(v | 0xfffff000) : static_cast<int32_t>(v);
}
inline uint32_t ExtractUi12(uint32_t encoding) { return (encoding >> 10) & 0xfff; }
inline int32_t ExtractSi20(uint32_t encoding) {
  uint32_t v = (encoding >> 5) & 0xfffff;
  return (v & 0x80000) ? static_cast<int32_t>(v | 0xfff00000) : static_cast<int32_t>(v);
}

// LoongArch field encoding helpers for instruction decode/encode
inline uint32_t EncodeRd(Register r) { return static_cast<uint32_t>(r) << 0; }
inline uint32_t EncodeRj(Register r) { return static_cast<uint32_t>(r) << 5; }
inline uint32_t EncodeRk(Register r) { return static_cast<uint32_t>(r) << 10; }
inline uint32_t EncodeFRd(FRegister r) { return static_cast<uint32_t>(r) << 0; }
inline uint32_t EncodeFRj(FRegister r) { return static_cast<uint32_t>(r) << 5; }
inline uint32_t EncodeSi12(intx_t imm) {
  ASSERT(Utils::IsInt(12, imm));
  return (static_cast<uint32_t>(imm) & 0xfff) << 10;
}
inline uint32_t EncodeSi20(intx_t imm) {
  ASSERT(Utils::IsInt(20, imm));
  return (static_cast<uint32_t>(imm) & 0xfffff) << 5;
}
inline uint32_t EncodeOffs16(intx_t imm) {
  return (static_cast<uint32_t>(imm) & 0xffff) << 10;
}

// LoongArch instruction decoder class
class Instr {
 public:
  explicit Instr(uint32_t encoding) : encoding_(encoding) {}
  uint32_t encoding() const { return encoding_; }
  size_t length() const { return 4; }

  Register rd() const { return static_cast<Register>(ExtractRd(encoding_)); }
  Register rj() const { return static_cast<Register>(ExtractRj(encoding_)); }
  Register rk() const { return static_cast<Register>(ExtractRk(encoding_)); }
  int32_t si12() const { return ExtractSi12(encoding_); }
  int32_t si20() const { return ExtractSi20(encoding_); }
  uint32_t ui12() const { return ExtractUi12(encoding_); }

  static constexpr uint32_t kBreakPointInstruction = 0x002a0000;
  static constexpr uint32_t kInstrSize = 4;
  static constexpr uint32_t kSimulatorRedirectInstruction = 0x002b0000;

 private:
  const uint32_t encoding_;
};

constexpr intptr_t kPreferredLoopAlignment = 0;

}  // namespace dart

#endif  // RUNTIME_VM_CONSTANTS_LOONG64_H_
