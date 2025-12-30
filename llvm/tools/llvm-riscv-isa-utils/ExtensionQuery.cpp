//===-- ExtensionQuery.cpp - Query the details of an extension ------------===//
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
#include "RISCVISAUtils.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/RISCVISAInfo.h"

using namespace llvm;

static cl::SubCommand QuerySubCmd("query", "Query the detail of an extension");

static cl::opt<std::string>
    ExtOrProfileName(cl::Positional, cl::desc("<extension or profile name>"),
                     cl::Required, cl::sub(QuerySubCmd));

static cl::opt<std::string> ImpliesExt("implies", cl::sub(QuerySubCmd));

static StringRef getExtDescription(StringRef ExtName) {
  static StringMap<StringRef> AllFeaturesDesc;
  if (AllFeaturesDesc.empty()) {
    // Initialize it.
    LLVMInitializeRISCVTargetInfo();
    LLVMInitializeRISCVTargetMC();

    // We just need a dummy MCSubtarget to get all the feature descriptions,
    // so it doesn't matter which triple / CPU / enabled features we use.
    Triple TheTriple("riscv64");
    std::string Error;
    const Target *TheTarget = TargetRegistry::lookupTarget(TheTriple, Error);
    assert(TheTarget && "Fail to lookup the RISC-V target");

    std::unique_ptr<MCSubtargetInfo> STI(TheTarget->createMCSubtargetInfo(
        TheTriple, /*CPU=*/"", /*Feature=*/""));
    for (const auto &KV : STI->getAllProcessorFeatures())
      AllFeaturesDesc[KV.Key] = KV.Desc;
  }

  std::string ExtFeatureName =
      RISCVISAInfo::getTargetFeatureForExtension(ExtName);
  return AllFeaturesDesc.lookup(ExtFeatureName);
}

namespace {
struct ImpliedExtsEntry {
  StringLiteral Name;
  const char *ImpliedExt;
};
} // namespace

#define GET_IMPLIED_EXTENSIONS
#include "llvm/TargetParser/RISCVTargetParserDef.inc"

// Return: {start index, end index} or {0, 0} if it doesn't exist.
static std::pair<unsigned, unsigned> getImpliedExtensions(StringRef ExtName) {
  // Ext name -> [begin Idx, end Idx)
  static StringMap<std::pair<unsigned, unsigned>> ImpliedExtMap;
  // Initialize the map.
  if (ImpliedExtMap.empty()) {
    for (const auto &[Idx, E] : enumerate(ImpliedExts)) {
      if (!ImpliedExtMap.count(E.Name))
        // A new entry.
        ImpliedExtMap[E.Name].first = Idx;

      // Continuously update the end index to be current index + 1.
      ImpliedExtMap[E.Name].second = Idx + 1;
    }
  }

  return ImpliedExtMap.lookup(ExtName);
}

static void lookupImpliedExtensions(StringRef ExtName,
                                    SmallVectorImpl<StringRef> &Result) {
  auto [StartIdx, EndIdx] = getImpliedExtensions(ExtName);
  while (StartIdx < EndIdx) {
    const auto &Entry = ImpliedExts[StartIdx++];
    Result.push_back(Entry.ImpliedExt);
  }
}

static bool isExtensionReachableFrom(StringRef FromExt, StringRef ToExt,
                                     SmallVectorImpl<StringRef> &Trace) {
  assert(!getExtDescription(FromExt).empty() &&
         !getExtDescription(ToExt).empty() && "Unknown extension names");

  if (FromExt == ToExt) {
    Trace.push_back(FromExt);
    return true;
  }

  // {Extension Name, Current Idx, End Idx}
  SmallVector<std::tuple<StringRef, unsigned, const unsigned>> Worklist;
  StringSet<> Visited;

  auto [StartIdx, EndIdx] = getImpliedExtensions(FromExt);
  Worklist.emplace_back(FromExt, StartIdx, EndIdx);
  Visited.insert(FromExt);

  while (!Worklist.empty()) {
    auto &Top = Worklist.back();
    unsigned &NextIdx = get<1>(Top);
    if (NextIdx >= get<2>(Top)) {
      // No more children, pop off the stack.
      Worklist.pop_back();
      continue;
    }

    // Get the next child.
    const auto &ImpliedEntry = ImpliedExts[NextIdx++];
    StringRef ExtName = ImpliedEntry.ImpliedExt;
    if (ExtName == ToExt) {
      // Found it, copy the entire worklist to trace.
      for (const auto &WE : Worklist)
        Trace.push_back(get<0>(WE));
      Trace.push_back(ToExt);
      return true;
    }

    if (!Visited.insert(ExtName).second)
      // Already visit this child before.
      continue;
    std::tie(StartIdx, EndIdx) = getImpliedExtensions(ExtName);
    if (StartIdx < EndIdx)
      Worklist.emplace_back(ExtName, StartIdx, EndIdx);
  }

  return false;
}

static Error printDependencyChain(StringRef InputName,
                                  StringRef ImpliesExtName) {
  std::string ToExtName = ImpliesExtName.lower();
  if (!RISCVISAInfo::isSupportedExtension(ToExtName))
    return createStringError(inconvertibleErrorCode(),
                             "Unrecognized extension name '" +
                                 Twine(ImpliesExtName) + "'");

  std::string QueryName = StringRef(InputName).lower();

  SmallVector<StringRef> Trace;
  // QueryName might be an extension or a profile.
  if (RISCVISAInfo::isSupportedExtension(QueryName)) {
    // It's an extension.
    isExtensionReachableFrom(QueryName, ToExtName, Trace);
  } else {
    StringMap<std::unique_ptr<RISCVISAInfo>> AllProfiles;
    RISCVISAInfo::getSupportedProfiles(AllProfiles);
    auto ItProfile = AllProfiles.find(QueryName);
    if (ItProfile == AllProfiles.end())
      return createStringError(inconvertibleErrorCode(),
                               "Unrecognized query name '" + Twine(InputName) +
                                   "'");
    const auto &ProfileInfo = ItProfile->getValue();
    for (const auto &[FromExtName, _] : ProfileInfo->getExtensions()) {
      if (isExtensionReachableFrom(FromExtName, ToExtName, Trace)) {
        Trace.insert(Trace.begin(), QueryName);
        break;
      }
      Trace.clear();
    }
  }

  if (!Trace.empty()) {
    ListSeparator LS(" -> ");
    for (StringRef Ext : Trace)
      outs() << LS << "'" << Ext << "'";
    outs() << "\n";
  } else {
    outs() << "Does not imply\n";
  }

  return Error::success();
}

namespace {
struct ExportExtension {
  StringRef Name;
  const RISCVISAUtils::ExtensionVersion &Version;
  StringRef Description;
  SmallVector<StringRef, 4> ImpliedExtensions;

  raw_ostream &printJSON(raw_ostream &OS) const;
};

struct ExportProfile {
  StringRef Name;
  const RISCVISAUtils::OrderedExtensionMap &Extensions;

  raw_ostream &printJSON(raw_ostream &OS) const;
};
} // namespace

raw_ostream &ExportExtension::printJSON(raw_ostream &OS) const {
  json::OStream JOS(OS, /*IndentSize=*/2);
  JOS.objectBegin();
  JOS.attribute("name", Name);
  JOS.attributeObject("version", [&, this] {
    JOS.attribute("major", Version.Major);
    JOS.attribute("minor", Version.Minor);
  });
  JOS.attribute("description", Description);
  JOS.attributeArray("implied", [&, this] {
    for (StringRef Ext : ImpliedExtensions)
      JOS.value(Ext);
  });
  JOS.objectEnd();

  return OS;
}
raw_ostream &operator<<(raw_ostream &OS, const ExportExtension &Ext) {
  if (RISCVISAUtils::shouldPrintAsJSON())
    return Ext.printJSON(OS);

  OS << "Feature Name: '" << Ext.Name << "'\n";
  OS << "Version: " << Ext.Version.Major << "." << Ext.Version.Minor << "\n";
  OS << "Description: " << Ext.Description << "\n";
  if (!Ext.ImpliedExtensions.empty()) {
    OS << "Implied Extensions:\n";
    for (StringRef IE : Ext.ImpliedExtensions)
      OS.indent(2) << "- '" << IE << "'\n";
  }

  return OS;
}

raw_ostream &ExportProfile::printJSON(raw_ostream &OS) const {
  json::OStream JOS(OS, /*IndentSize=*/2);
  JOS.objectBegin();
  JOS.attribute("name", Name);
  JOS.attributeArray("extensions", [&, this] {
    for (const auto &[ExtName, _] : Extensions)
      JOS.value(ExtName);
  });
  JOS.objectEnd();
  return OS;
}
raw_ostream &operator<<(raw_ostream &OS, const ExportProfile &Profile) {
  if (RISCVISAUtils::shouldPrintAsJSON())
    return Profile.printJSON(OS);

  OS << "Profile: '" << Profile.Name << "'\n";
  OS << "Extensions:\n";
  for (const auto &[ExtName, Version] : Profile.Extensions)
    OS.indent(2) << "- '" << ExtName << "' " << Version.Major << "."
                 << Version.Minor << "\n";

  return OS;
}

static Error entry() {
  if (ImpliesExt.getNumOccurrences()) {
    if (RISCVISAUtils::shouldPrintAsJSON())
      return createStringError(inconvertibleErrorCode(),
                               "--implies cannot be used with --json");
    return printDependencyChain(ExtOrProfileName, ImpliesExt);
  }

  std::string QueryName = StringRef(ExtOrProfileName).lower();

  // Try to see if the query name is an extension.
  if (RISCVISAInfo::isSupportedExtension(QueryName)) {
    RISCVISAUtils::OrderedExtensionMap ExtensionMap;
    RISCVISAInfo::getSupportedExtensions(ExtensionMap);

    std::string FeatureName =
        RISCVISAInfo::getTargetFeatureForExtension(QueryName);

    ExportExtension Ext{FeatureName,
                        ExtensionMap.at(QueryName),
                        getExtDescription(QueryName),
                        {}};
    lookupImpliedExtensions(QueryName, Ext.ImpliedExtensions);

    outs() << Ext;
    return Error::success();
  }

  // Try to see if the quey name is a profile.
  StringMap<std::unique_ptr<RISCVISAInfo>> AllProfiles;
  RISCVISAInfo::getSupportedProfiles(AllProfiles);
  auto ItProfile = AllProfiles.find(QueryName);
  if (ItProfile == AllProfiles.end())
    return createStringError(inconvertibleErrorCode(),
                             "Unrecognized query name '" +
                                 Twine(ExtOrProfileName) + "'");

  const auto &ProfileInfo = ItProfile->getValue();
  ExportProfile Profile{QueryName, ProfileInfo->getExtensions()};

  outs() << Profile;
  return Error::success();
}

static RISCVISAUtils::CommandRegistration Command(&QuerySubCmd, entry);
