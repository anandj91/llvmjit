#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/IR/Verifier.h"

#include "jit.h"

#include <iostream>
#include <memory>
#include <string>

using namespace std;
using namespace llvm;
using namespace llvm::orc;

std::unique_ptr<Module> buildprog(LLVMContext& ctx)
{
    std::unique_ptr<Module> llmod = std::make_unique<Module>("test", ctx);

    Module* m = llmod.get();

    auto loop_fn = Function::Create(
        FunctionType::get(Type::getInt32Ty(ctx), { Type::getInt32Ty(ctx), Type::getInt32Ty(ctx) }, false),
        Function::ExternalLinkage, "loop", m);
    auto start = loop_fn->getArg(0);
    start->setName("start");
    auto end = loop_fn->getArg(1);
    end->setName("end");

    auto entry_bb = BasicBlock::Create(ctx, "entry", loop_fn);
    auto body_bb = BasicBlock::Create(ctx, "body", loop_fn);
    auto exit_bb = BasicBlock::Create(ctx, "exit", loop_fn);
    IRBuilder<> builder(entry_bb);
    builder.CreateBr(body_bb);
    builder.SetInsertPoint(body_bb);

    auto i = builder.CreatePHI(Type::getInt32Ty(ctx), 2, "i");
    auto sum = builder.CreatePHI(Type::getInt32Ty(ctx), 2, "sum");

    auto next_i = builder.CreateAdd(i, builder.getInt32(1), "next_i");
    i->addIncoming(start, entry_bb);
    i->addIncoming(next_i, body_bb);

    auto sum_expr = builder.CreateAdd(i, sum, "new_sum");
    sum->addIncoming(builder.getInt32(0), entry_bb);
    sum->addIncoming(sum_expr, body_bb);

    auto loop_cond = builder.CreateICmpSLT(i, end, "cond");
    builder.CreateCondBr(loop_cond, body_bb, exit_bb);

    builder.SetInsertPoint(exit_bb);
    builder.CreateRet(sum);

    return llmod;
}

std::unique_ptr<Module> buildsrc(LLVMContext& Context)
{
    const StringRef add1_src =
        R"(
        define i32 @add1(i32 %x) {
        entry:
            %r = add nsw i32 %x, 1
            ret i32 %r
        }
    )";

    SMDiagnostic err;
    auto llmod = parseIR(MemoryBufferRef(add1_src, "test"), err, Context);
    if (!llmod) {
        std::cout << err.getMessage().str() << std::endl;
        return nullptr;
    }
    else {
        return llmod;
    }
}

int main(int argc, char** argv)
{
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    auto jit = cantFail(TilJIT::Create());
    auto& ctx = jit->getContext();

    auto llmod = buildprog(ctx);
    //auto llmod = std::move(buildsrc(ctx));
    if (!llmod) return -1;

    raw_fd_ostream r(fileno(stdout), false);
    if (!verifyModule(*llmod, &r)) { r << *llmod; }

    cantFail(jit->addModule(std::move(llmod)));

    JITEvaluatedSymbol sym = cantFail(jit->lookup("loop"));

    auto* loop = (int (*)(int, int))(intptr_t)sym.getAddress();
    std::cout << "Result: " << loop(atoi(argv[1]), atoi(argv[2])) << std::endl;

    return 0;
}
