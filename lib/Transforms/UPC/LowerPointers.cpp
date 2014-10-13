#include <llvm/Pass.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#include <llvm/InitializePasses.h>
#include <llvm/Transforms/UPC.h>
#include <llvm/Config/config.h> // for UPC_IR_RP_ADDRSPACE

using namespace llvm;

namespace {

static const int UPC_PTS_ADDR_SPACE = UPC_IR_RP_ADDRSPACE;

struct LowerUPCPointers : FunctionPass {
  static char ID;
  LowerUPCPointers() : FunctionPass(ID) {
    initializeLowerUPCPointersPass(*PassRegistry::getPassRegistry());
  }
  bool runOnFunction(Function &F) {
    bool result = doInitialization(F);
    for(Function::iterator iter = F.begin(), end = F.end(); iter != end; ++iter) {
      if(runOnBasicBlock(*iter)) {
        result = true;
      }
    }
    return result;
  }
  bool runOnBasicBlock(BasicBlock &BB) {
    bool result = false;
    for(BasicBlock::iterator iter = BB.begin(), end = BB.end(); iter != end;) {
      BasicBlock::iterator tmp = iter;
      ++tmp;
      if(handleInstruction(*iter)) {
        result = true;
      }
      iter = tmp;
    }
    return result;
  }
  bool handleInstruction(Instruction &I) {
    Type * Int64Ty = Type::getInt64Ty(*Ctx);
    Type * Int8PtrTy = Type::getInt8PtrTy(*Ctx);
    if(LoadInst * LI = dyn_cast<LoadInst>(&I)) {
      if(LI->getPointerAddressSpace() == UPC_PTS_ADDR_SPACE) {
        Value * Ptr = LI->getPointerOperand();
        Type * Ty = Ptr->getType()->getPointerElementType();
        Value * Tmp = new AllocaInst(Ty, "ptsload", AllocaInsertPoint);
        Value * Arg = CastInst::Create(Instruction::BitCast, Tmp, Int8PtrTy, "", &I);
        Value * PtrRep = CastInst::Create(Instruction::PtrToInt, Ptr, Int64Ty, "", &I);
        Value * Size = ConstantInt::get(Int64Ty, Layout->getTypeStoreSize(Ty));
        Value * args[] = { PtrRep, Arg, Size };
        if(LI->getOrdering() == SequentiallyConsistent) {
          CallInst::Create(LoadStrictFn, args, "", &I);
        } else {
          CallInst::Create(LoadRelaxedFn, args, "", &I);
        }
        Value * Result = new LoadInst(Tmp, "", &I);
        I.replaceAllUsesWith(Result);
        I.eraseFromParent();
        return true;
      }
    } else if(StoreInst * SI = dyn_cast<StoreInst>(&I)) {
      if(SI->getPointerAddressSpace() == UPC_PTS_ADDR_SPACE) {
        Value * Val = SI->getValueOperand();
        Value * Ptr = SI->getPointerOperand();
        Type * Ty = Val->getType();
        Value * Tmp = new AllocaInst(Ty, "ptsstore", AllocaInsertPoint);
        Value * Arg = CastInst::Create(Instruction::BitCast, Tmp, Int8PtrTy, "", &I);
        Value * PtrRep = CastInst::Create(Instruction::PtrToInt, Ptr, Int64Ty, "", &I);
        Value * Size = ConstantInt::get(Int64Ty, Layout->getTypeStoreSize(Ty));
        new StoreInst(Val, Tmp, false, &I);
        Instruction *Result;
        Value * args[] = { Arg, PtrRep, Size };
        if(SI->getOrdering() == SequentiallyConsistent) {
          Result = CallInst::Create(StoreStrictFn, args, "", &I);
        } else {
          Result = CallInst::Create(StoreRelaxedFn, args, "", &I);
        }
        I.replaceAllUsesWith(Result);
        I.eraseFromParent();
        return true;
      }
    }
    return false;
  }
  
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesCFG();
    AU.addRequired<DataLayout>();
  }

  bool doInitialization(Function &F) {
    Ctx = &F.getContext();
    Layout = &getAnalysis<DataLayout>();

    BasicBlock &Entry = F.getEntryBlock();
    BasicBlock::iterator iter = Entry.begin();
    while(isa<AllocaInst>(*iter)) ++iter;
    AllocaInsertPoint = &*iter;
    return false;
  }
  bool doInitialization(Module &M) {
    Ctx = &M.getContext();
    Type *Int64Ty = Type::getInt64Ty(*Ctx);
    Type *Int8PtrTy = Type::getInt8PtrTy(*Ctx);
    LoadRelaxedFn = M.getOrInsertFunction("upcr_llvm_getn",
                                          Type::getVoidTy(*Ctx),
                                          Int64Ty, Int8PtrTy, Int64Ty, (Type*)0);
    LoadStrictFn = M.getOrInsertFunction("upcr_llvm_getns",
                                         Type::getVoidTy(*Ctx),
                                         Int64Ty, Int8PtrTy, Int64Ty, (Type*)0);
    StoreRelaxedFn = M.getOrInsertFunction("upcr_llvm_putn",
                                          Type::getVoidTy(*Ctx),
                                          Int8PtrTy, Int64Ty, Int64Ty, (Type*)0);
    StoreStrictFn = M.getOrInsertFunction("upcr_llvm_putns",
                                         Type::getVoidTy(*Ctx),
                                         Int8PtrTy, Int64Ty, Int64Ty, (Type*)0);
    return true;
  }
  LLVMContext *Ctx;
  DataLayout *Layout;
  Instruction *AllocaInsertPoint;
  Constant *LoadStrictFn;
  Constant *LoadRelaxedFn;
  Constant *StoreStrictFn;
  Constant *StoreRelaxedFn;
};

}

char LowerUPCPointers::ID = 0;

INITIALIZE_PASS(LowerUPCPointers, "lower-upc-pointers",
                "Pass for lowering UPC pointer accesses",
                false, false)

FunctionPass *llvm::createLowerUPCPointersPass() {
  return new LowerUPCPointers();
}
