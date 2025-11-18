//===-- ListExtensions.cpp - List enabled extensions ----------------------===//
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
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/RISCVISAInfo.h"
#include <string>

using namespace llvm;

static cl::SubCommand
    ListSubCmd("list", "Show extensions of an arch string or CPU name");

static cl::opt<bool> IsRV64("rv64", cl::desc("Is 64-bit RISC-V"),
                            cl::init(false), cl::sub(ListSubCmd));

static cl::opt<std::string> MArch("march", cl::desc("RISC-V arch string"),
                                  cl::init(""), cl::sub(ListSubCmd));

static cl::opt<std::string> MCpu("mcpu", cl::desc("RISC-V CPU name"),
                                 cl::init(""), cl::sub(ListSubCmd));

static cl::opt<bool>
    EnableExperimental("menable-experimental-extensions",
                       cl::desc("Show experimental extensions"),
                       cl::init(false), cl::sub(ListSubCmd));

static Error entry() {
  LLVMInitializeRISCVTargetInfo();
  LLVMInitializeRISCVTargetMC();

  Triple TheTriple(IsRV64 ? "riscv64" : "riscv32");
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TheTriple, Error);
  assert(TheTarget && "Fail to lookup the RISC-V target");

  // Populate features from -march.
  std::vector<std::string> EnabledFeatures;
  if (!MArch.empty()) {
    auto RVISAInfoOrErr =
        RISCVISAInfo::parseArchString(MArch, EnableExperimental);
    if (!RVISAInfoOrErr)
      return RVISAInfoOrErr.takeError();
    EnabledFeatures = (*RVISAInfoOrErr)->toFeatures();
  }

  std::unique_ptr<MCSubtargetInfo> STI(TheTarget->createMCSubtargetInfo(
      TheTriple, MCpu, join(EnabledFeatures, ",")));
  if (!STI)
    return createStringError(inconvertibleErrorCode(),
                             "unable to create subtarget info\n");

  StringMap<StringRef> AllFeaturesDesc;
  for (const auto &KV : STI->getAllProcessorFeatures())
    AllFeaturesDesc[KV.Key] = KV.Desc;

  if (!MArch.getNumOccurrences() && !MCpu.getNumOccurrences()) {
    // In the absent of `-march` and `-mcpu`, we simply print out
    // all supported extensions (and profiles).
    RISCVISAInfo::printSupportedExtensions(AllFeaturesDesc);
    return Error::success();
  }

  std::set<StringRef> EnabledFeatureNames;
  for (const auto &Feature : STI->getEnabledProcessorFeatures())
    EnabledFeatureNames.insert(Feature.Key);
  RISCVISAInfo::printEnabledExtensions(IsRV64, EnabledFeatureNames,
                                       AllFeaturesDesc);

  return Error::success();
}

static RISCVISAUtils::CommandRegistration Command(&ListSubCmd, entry);
