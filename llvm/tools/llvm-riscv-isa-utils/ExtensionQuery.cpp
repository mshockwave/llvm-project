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
#include "llvm/Support/Error.h"

using namespace llvm;

static cl::SubCommand QuerySubCmd("query", "Query the detail of an extension");

static Error entry() { return Error::success(); }

static RISCVISAUtils::CommandRegistration Command(&QuerySubCmd, entry);
