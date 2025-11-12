//===-- llvm-riscv-isa-utils.cpp - LLVM RISC-V ISA Info Utilities ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "CommandRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace llvm;

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv, "llvm-riscv-isa-utils");

  for (auto *SC : cl::getRegisteredSubcommands()) {
    if (*SC) {
      // If no subcommand was provided, we need to explicitly check if this is
      // the top-level subcommand.
      if (SC == &cl::SubCommand::getTopLevel()) {
        cl::PrintHelpMessage(false, true);
        return 0;
      }
      if (auto C = RISCVISAUtils::dispatch(SC)) {
        ExitOnError("llvm-isa-utils: ")(C());
        return 0;
      }
    }
  }

  // If all else fails, we still print the usage message.
  cl::PrintHelpMessage(false, true);
  return 0;
}
