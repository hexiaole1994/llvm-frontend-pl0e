#ifndef LLVM_EXECUTIONENGINE_ORC_PL0JIT_H
#define LLVM_EXECUTIONENGINE_ORC_PL0JIT_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

namespace llvm {
namespace orc {

class Pl0JIT
{
private:
std::unique_ptr<TargetMachine> TM;
const DataLayout DL;
RTDyldObjectLinkingLayer ObjectLayer;
IRCompileLayer<decltype(ObjectLayer),SimpleCompiler> CompileLayer;
public:
using ModuleHandle = decltype(CompileLayer)::ModuleHandleT;


Pl0JIT():TM(EngineBuilder().selectTarget()),DL(TM->createDataLayout()),ObjectLayer([](){return std::make_shared<SectionMemoryManager>();}),CompileLayer(ObjectLayer,SimpleCompiler(*TM))
{
	llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
}
TargetMachine& getTargetMachine(){ return *TM;}

ModuleHandle addModule(std::unique_ptr<Module> M)
{
	auto Resolver = createLambdaResolver(
	[&](const std::string& Name)
	{
		auto Sym = CompileLayer.findSymbol(Name,false);
		if(!Sym)
			return JITSymbol(nullptr);
		
		return Sym;
	}
	,
	[](const std::string& Name)
	{
		auto SymAddr = RTDyldMemoryManager::getSymbolAddressInProcess(Name);
		if(!SymAddr)
			return JITSymbol(nullptr);

		return JITSymbol(SymAddr,JITSymbolFlags::Exported);
	}
	);
	return cantFail(CompileLayer.addModule(std::move(M),std::move(Resolver)));
}

JITSymbol findSymbol(const std::string Name)
{
	std::string MangledName;
	raw_string_ostream MangledNameStream(MangledName);
	Mangler::getNameWithPrefix(MangledNameStream,Name,DL);
	return CompileLayer.findSymbol(MangledNameStream.str(),true);
}
JITTargetAddress getSymbolAddress(const std::string Name)
{
	return cantFail(findSymbol(Name).getAddress());
}

void removeModule(ModuleHandle H)
{
	cantFail(CompileLayer.removeModule(H));
}

};

}//end of namespace orc
}//end of namespace llvm

#endif
