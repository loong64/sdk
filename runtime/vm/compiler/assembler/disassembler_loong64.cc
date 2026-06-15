// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_LOONG64.
#if defined(TARGET_ARCH_LOONG64)

#include "vm/compiler/assembler/disassembler.h"

#include "platform/assert.h"
#include "vm/instructions.h"

namespace dart {

#if !defined(PRODUCT) || defined(FORCE_INCLUDE_DISASSEMBLER)

class LOONG64Disassembler {
 public:
  explicit LOONG64Disassembler(char* buffer,
                               size_t buffer_size)
      : buffer_(buffer), buffer_size_(buffer_size), buffer_pos_(0) {}

  intptr_t Disassemble(uword pc) {
    uint32_t parcel = LoadUnaligned(reinterpret_cast<uint32_t*>(pc));
    Instr instr(parcel);
    intptr_t offset = snprintf(buffer_ + buffer_pos_,
                               buffer_size_ - buffer_pos_,
                               "0x%08x", parcel);
    buffer_pos_ += offset;
    return 4;
  }

 private:
  char* buffer_;
  size_t buffer_size_;
  size_t buffer_pos_;
  DISALLOW_COPY_AND_ASSIGN(LOONG64Disassembler);
};

intptr_t Disassembler::DecodeAt(uword pc,
                                 char* buffer,
                                 size_t buffer_size) {
  LOONG64Disassembler disasm(buffer, buffer_size);
  return disasm.Disassemble(pc);
}

void Disassembler::SetExtensions(ExtensionSet extensions) {
  // LOONG64 does not use extensions.
}

ExtensionSet Disassembler::extensions_ = ExtensionSet();

#endif  // !defined(PRODUCT) || defined(FORCE_INCLUDE_DISASSEMBLER)

}  // namespace dart

#endif  // defined TARGET_ARCH_LOONG64