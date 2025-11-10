set(LLVM_LINK_COMPONENTS
  AllTargetsDescs
  AllTargetsInfos
  MC
  Support
  TargetParser
  )

add_llvm_tool(llvm-riscv-isa-utils
  llvm-riscv-isa-utils.cpp
  )
