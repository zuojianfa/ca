#ifndef __codegen_jit1_h__
#define __codegen_jit1_h__

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/Mangling.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/Target/TargetMachine.h"
#include <vector>
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"

namespace jit_codegen {
using namespace llvm;

class JIT1 {
public:
  static llvm::Expected<std::unique_ptr<JIT1>> create_instance();

  JIT1(std::unique_ptr<orc::ExecutionSession> es,
       orc::JITTargetMachineBuilder builder, DataLayout dl);

  ~JIT1();

  // this function can be called multiple time to register more symbols
  void register_one_imported_symbol(const char *name, void *address);

  // this function can be called multiple time to register more symbols
  void register_imported_symbols(std::vector<std::pair<const char *, void *>> &name_addresses);

  const DataLayout &get_datalayout() const { return _dl; }
  orc::JITDylib &get_main_jitdl() { return _main_jitdl; }
  llvm::Error add_module(orc::ThreadSafeModule tsm, orc::ResourceTrackerSP rt = nullptr);

  llvm::Expected<JITEvaluatedSymbol> find(StringRef name);

private:
  std::unique_ptr<llvm::orc::ExecutionSession> _es;
  DataLayout _dl;
  orc::MangleAndInterner _mangle;
  orc::RTDyldObjectLinkingLayer _obj_layer;
  orc::IRCompileLayer _compile_layer;
  orc::JITDylib &_main_jitdl;
};
}

#endif

