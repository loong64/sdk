// Copyright (c) 2021, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_LOONG64)

#define SHOULD_NOT_INCLUDE_RUNTIME

#include "vm/compiler/assembler/assembler.h"
#include "vm/compiler/asm_intrinsifier.h"

namespace dart {
namespace compiler {

#define __ assembler->

void AsmIntrinsifier::AbstractType_equality(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::AbstractType_getHashCode(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::AllocateOneByteString(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::AllocateTwoByteString(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Bigint_absAdd(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Bigint_absSub(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Bigint_lsh(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Bigint_mulAdd(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Bigint_rsh(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Bigint_sqrAdd(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_add(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_div(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_equal(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_getIsInfinite(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_getIsNaN(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_getIsNegative(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_greaterEqualThan(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_greaterThan(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_lessEqualThan(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_lessThan(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_mul(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_mulFromInteger(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_sub(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::DoubleFromInteger(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Integer_equal(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Integer_greaterEqualThan(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Integer_greaterThan(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Integer_lessEqualThan(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Integer_lessThan(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Integer_shl(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Montgomery_mulMod(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Object_getHash(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::ObjectEquals(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::ObjectHaveSameRuntimeType(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::ObjectRuntimeType(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::OneByteString_equality(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::OneByteString_getHashCode(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Smi_bitLength(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::String_getHashCode(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::String_identityHash(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::StringBaseCharAt(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::StringBaseIsEmpty(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::StringBaseSubstringMatches(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Timeline_getNextTaskId(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::TwoByteString_equality(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Type_equality(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::WriteIntoOneByteString(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::WriteIntoTwoByteString(Assembler* assembler, Label* normal_ir_body) {
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::StringEquality(Assembler* assembler,
                                     Register obj1,
                                     Register obj2,
                                     Register temp1,
                                     Register temp2,
                                     Register result,
                                     Label* normal_ir_body,
                                     intptr_t string_cid) {
  __ Bind(normal_ir_body);
}


void AsmIntrinsifier::Bigint_estimateQuotientDigit(Assembler* assembler,
                                                   Label* normal_ir_body) {
  // Fall back to the normal (non-intrinsic) implementation for LoongArch.
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Integer_equalToInteger(Assembler* assembler,
                                             Label* normal_ir_body) {
  // Fall back to the normal (non-intrinsic) implementation for LoongArch.
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::OneByteString_substringUnchecked(Assembler* assembler,
                                                        Label* normal_ir_body) {
  // Fall back to the normal (non-intrinsic) implementation for LoongArch.
  __ Bind(normal_ir_body);
}
#undef __

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64
