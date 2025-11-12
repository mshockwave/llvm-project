set(LLVM_LINK_COMPONENTS
  AllTargetsDescs
  AllTargetsInfos
  MC
  Support
  TargetParser
  )

add_llvm_tool(llvm-riscv-isa-utils
  CommandRegistry.cpp
  ExtensionQuery.cpp
  ListExtensions.cpp
  llvm-riscv-isa-utils.cpp
  )
