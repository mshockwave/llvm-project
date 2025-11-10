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

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/RISCVISAInfo.h"
#include <string>

using namespace llvm;

static cl::opt<std::string> MTriple("mtriple");
static cl::opt<std::string> MArch("march", cl::init(""));
static cl::opt<std::string> MCPU("mcpu", cl::init(""));

static cl::opt<bool> EnableExperimental("menable-experimental-extensions",
                                        cl::init(true));

static cl::opt<bool> PrintSupportedExts("print-supported-extensions");
static cl::opt<bool> PrintEnabledExts("print-enabled-extensions");

static const Target *getTarget(const Triple &TheTriple, const char *ProgName) {
  // Get the target specific parser.
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TheTriple, Error);
  if (!TheTarget) {
    errs() << ProgName << ": " << Error;
    return nullptr;
  }

  // Return the found target.
  return TheTarget;
}

int main(int argc, char **argv) {
  InitializeAllTargetInfos();
  InitializeAllTargetMCs();

  cl::ParseCommandLineOptions(argc, argv, "TBA");

  Triple TheTriple(MTriple.empty()
                       ? Triple::normalize(sys::getDefaultTargetTriple())
                       : MTriple);
  if (!TheTriple.isRISCV()) {
    WithColor::error() << "'" << TheTriple.getTriple()
                       << "' is not a RISC-V triple\n";
    return 1;
  }

  const Target *TheTarget = getTarget(TheTriple, argv[0]);

  // Populate features from -march.
  std::vector<std::string> EnabledFeatures;
  if (!MArch.empty()) {
    std::unique_ptr<RISCVISAInfo> RVISAInfo =
        cantFail(RISCVISAInfo::parseArchString(MArch, EnableExperimental));
    EnabledFeatures = RVISAInfo->toFeatures();
  }

  std::unique_ptr<MCSubtargetInfo> STI(TheTarget->createMCSubtargetInfo(
      TheTriple, MCPU, join(EnabledFeatures, ",")));
  if (!STI) {
    WithColor::error() << "unable to create subtarget info\n";
    return 1;
  }

  StringMap<StringRef> AllFeaturesDesc;
  for (const auto &KV : STI->getAllProcessorFeatures())
    AllFeaturesDesc[KV.Key] = KV.Desc;

  if (PrintSupportedExts) {
    RISCVISAInfo::printSupportedExtensions(AllFeaturesDesc);
    return 0;
  }

  if (PrintEnabledExts) {
    std::set<StringRef> EnabledFeatureNames;
    for (const auto &Feature : STI->getEnabledProcessorFeatures())
      EnabledFeatureNames.insert(Feature.Key);
    RISCVISAInfo::printEnabledExtensions(TheTriple.isRISCV64(),
                                         EnabledFeatureNames, AllFeaturesDesc);
    return 0;
  }

  return 0;
}
