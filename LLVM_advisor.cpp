#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/ADT/ilist.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Constants.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <climits>
#include <llvm/IR/InstIterator.h>
#include "llvm/IR/DebugInfo.h"


#include "common.h"

#define BUFFERSIZE_INT BUFFERSIZE/4 // number of integers, rather than bytes // do not use 24<<20, errors
#define TRACESIZE_INT TRACESIZE/4 // number of integers, rather than bytes

const int AFsize = 24;
std::string AF[AFsize] = {	"print1",
				"print2",
				"print3",
				"print5",
				"takeString",
				"RetKernel",
				"storeLines",
				"dumpLines",
				"atomicAdd", //this is needed for -O0
				"passBasicBlock",
				"callFunc",
			"mystrcpy",
			"mystrcmp",
			"getFuncID",
			"updateCallStack",
			"printCallStack",
			"getContextID",
			"cxtcpy",
			"myholdon",
			"cxtcmp",
			"cxtprint",
			"InitKernel",
			"print4",
			"assert_failPK"
			};

std::string CUDAKN[30];
int KNcnt=0;

using namespace llvm;

static cl::opt<std::string> FunctionName(
    "fname", cl::desc("Function name to instrument"), cl::value_desc("function"));

static cl::opt<std::string> LineRange(
    "line-range", cl::desc("Line number range (e.g., 6-8)"), cl::value_desc("line-range"));


namespace{

	struct my_rand : public ModulePass {
        static char ID;  
		FunctionCallee hook1;
        my_rand() : ModulePass(ID) {}
        virtual bool runOnModule(Module &M){
			
			LLVMContext &C = M.getContext();
			errs() << "\n================== Hello World Pass ==============\n\n";
			
			Type* VoidTy = Type::getVoidTy(C); 
			std::vector<Type*> ArgTypes1 = {};
			FunctionType *FuncType1 = FunctionType::get(VoidTy, ArgTypes1, false);
            hook1 = M.getOrInsertFunction("print", FuncType1);


			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
            {

				errs() << F->getName().str() << "\n";

				if ("addVectors" == F->getName().str() || F->getName().str().find("axpy_kernel1") != std::string::npos || F->getName().str().find("main") != std::string::npos){

					errs() << "Instrumenting Function " << F->getName().str() << "\n";

					for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
					{
						BasicBlock::iterator BI = BB->begin();
						IRBuilder<> builder(&(*BI));
						builder.SetInsertPoint(&*BI);

						DebugLoc dbg;
						if (BI->getDebugLoc())
    						dbg = BI->getDebugLoc();  // If the first instruction has debug info
						else {
							// Optional: Search forward for next instruction with debug info
							BasicBlock::iterator It = BI;
							++It;
							for (; It != BB->end(); ++It) {
								if (It->getDebugLoc()) {
									dbg = It->getDebugLoc();
									break;
								}
							}
						}

						// Now set the debug location before creating the call
						if (dbg)
							builder.SetCurrentDebugLocation(dbg);

						builder.CreateCall(hook1, {});
						
						break;
					}

				}
				
            }

			return true;
		}

	};

	struct our_pass : public ModulePass {
        static char ID;  
		FunctionCallee hook1;
        our_pass() : ModulePass(ID) {}

		bool overlap(int start, int end, int lineStart, int lineEnd){

			if (start > end || lineStart > lineEnd)
				return false; // Invalid range

			if (start > lineEnd || end < lineStart) {
				return false; // No overlap
			}

			return true; // Overlap exists
		}

		void findInstructionsToInstrument(Function *F, std::vector<Instruction*> &instructionsToInstrument, int lineStart, int lineEnd) {	

			std::unordered_map<Instruction*, int> LineStart; // Min line number (predecessor)
			std::unordered_map<Instruction*, int> LineEnd;   // Max line number (successor)
			std::unordered_set<Instruction*> InWorklist;     // Fast O(1) check for presence
			std::queue<Instruction*> Worklist;               // FIFO queue for processing


			// For getting LineStart

			for (Instruction &I : instructions(*F)) {
				if (DILocation *Loc = I.getDebugLoc()) {
					LineStart[&I] = Loc->getLine();
				} else {
					LineStart[&I] = INT_MAX;
					Worklist.push(&I);
					InWorklist.insert(&I);
				}
			}

			while (!Worklist.empty()) {
				Instruction *I = Worklist.front();
				Worklist.pop();
				InWorklist.erase(I);
			
				int oldMin = LineStart[I];
				int newMin = oldMin;
			
				// If not the first instruction, take the previous instruction's line
				if (Instruction *Prev = I->getPrevNode()) {
					newMin = std::min(newMin, LineStart[Prev]);
				} else {
					// First instruction in the block: check predecessors' terminators
					for (BasicBlock *Pred : predecessors(I->getParent())) {
						Instruction *Term = Pred->getTerminator();
						newMin = std::min(newMin, LineStart[Term]);
					}
				}
			
				// If the minimum line changed, propagate to successors
				if (newMin < oldMin) {
					LineStart[I] = newMin;
			
					// Successors:
					if (Instruction *Next = I->getNextNode()) {
						if (InWorklist.insert(Next).second) {
							Worklist.push(Next);
						}
					} else {
						// If it's the last in the block, push the first of each successor
						for (BasicBlock *Succ : successors(I->getParent())) {
							Instruction *First = &Succ->front();
							if (InWorklist.insert(First).second) {
								Worklist.push(First);
							}
						}
					}
				}
			}
			

			// For getting LineEnd

			for (Instruction &I : instructions(*F)) {
				if (DILocation *Loc = I.getDebugLoc()) {
					LineEnd[&I] = Loc->getLine();
				} else {
					LineEnd[&I] = -1;
					Worklist.push(&I);
					InWorklist.insert(&I);
				}
			}

			while (!Worklist.empty()) {
				Instruction *I = Worklist.front();
				Worklist.pop();
				InWorklist.erase(I);
			
				int oldMax = LineEnd[I];
				int newMax = oldMax;
			
				// If not the last instruction, take the next instruction's line
				if (Instruction *Next = I->getNextNode()) {
					newMax = std::max(newMax, LineEnd[Next]);
				} else {
					// Last instruction in the block: check successors' first instructions
					for (BasicBlock *Succ : successors(I->getParent())) {
						Instruction *First = &Succ->front();
						newMax = std::max(newMax, LineEnd[First]);
					}
				}
			
				// If the maximum line changed, propagate to predecessors
				if (newMax > oldMax) {
					LineEnd[I] = newMax;
			
					// Predecessors:
					if (Instruction *Prev = I->getPrevNode()) {
						if (InWorklist.insert(Prev).second) {
							Worklist.push(Prev);
						}
					} else {
						// If it's the first in the block, push all predecessors' terminators
						for (BasicBlock *Pred : predecessors(I->getParent())) {
							Instruction *Term = Pred->getTerminator();
							if (InWorklist.insert(Term).second) {
								Worklist.push(Term);
							}
						}
					}
				}
			}
			
			
			for (Instruction &I : instructions(*F)) {
				if (!I.getDebugLoc()) {
					int start = LineStart[&I];
					int end = LineEnd[&I];
					if (start != INT_MAX && end != -1) {

						outs() << "Instruction: " << I << " → Line Range: [" << start << ", " << end << "]\n";

						if (overlap(start, end, lineStart, lineEnd)) {
							instructionsToInstrument.push_back(&I);
							outs() << "Instruction to instrument: " << I << "\n";
						}

					}
					else {
						outs() << "Instruction Range Not found: " << I << " → Line Range: [" << start << ", " << end << "]\n";
					}
				}
				else {
					DILocation *Loc = I.getDebugLoc();
					int lineNumber = Loc->getLine();

					if (lineNumber >= lineStart && lineNumber <= lineEnd) {
						instructionsToInstrument.push_back(&I);
						outs() << "Instruction to instrument: " << I << "\n";
					}
					
				}
			}
			

			return;
		}

        virtual bool runOnModule(Module &M){

			int lineStart = -1, lineEnd = -1;

			if (!LineRange.empty()) {
				size_t dashPos = LineRange.find('-');
				if (dashPos != std::string::npos) {
					lineStart = std::stoi(LineRange.substr(0, dashPos));
					lineEnd = std::stoi(LineRange.substr(dashPos + 1));
				} else {
					errs() << "Invalid line-range format. Expected format: start-end\n";
					return false;
				}
			}

			std::cout << "Line range: " << lineStart << " to " << lineEnd << std::endl;

			
			LLVMContext &C = M.getContext();
			errs() << "\n================== Our Pass ==============\n\n";
			
			
			std::vector<Instruction*> instructionsToInstrument;


			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
            {

				errs() << F->getName().str() << "\n";

				if (F->getName().str().find(FunctionName) != std::string::npos){

					errs() << "Instrumenting Function " << F->getName().str() << "\n";

					findInstructionsToInstrument(&(*F), instructionsToInstrument, lineStart, lineEnd);
					

				}
				else if (F->getName().str().find("main") != std::string::npos){

					// For printing memory bandwidth result
				}
            }

			return true;
		}

	};


	// mandatory pass  for printing stuff maintains call stack?
	// pass: host pass
	// pass: first pass
	// for memory load/store, function call and return
    struct instru_host : public ModulePass{
        static char ID;  
        FunctionCallee hook1;
        FunctionCallee hook2;
        FunctionCallee hook3;
        FunctionCallee hook4;
        FunctionCallee hookEA;
		FunctionCallee hookRetMain;
		FunctionCallee hookString;
		FunctionCallee hookCMFS;
		FunctionCallee hookDumpTrace;
		FunctionCallee hookFCall;
		FunctionCallee hookMallocEvent;
		bool flag1; // tracks whether a caller name global Variable created or not
		Value* str_fname=NULL; 
		Value* ptr_fname=NULL;

        instru_host() : ModulePass(ID) {}

        virtual bool runOnModule(Module &M)
        {
			LLVMContext &C = M.getContext();
			errs() << "\n================== 1st pass: instru_host ==============\n\n";
			
			
            FunctionCallee hookFunc1; FunctionCallee hookFunc2;
			FunctionCallee hookFunc3; FunctionCallee hookFunc4; FunctionCallee hookFuncEA;
			FunctionCallee hookFuncRetMain;
			FunctionCallee hookFuncString;
			FunctionCallee hookFuncDumpTrace;
			FunctionCallee hookFuncFCall;
			FunctionCallee hookFuncMallocEvent;
			
			Type* VoidTy = Type::getVoidTy(C); 

			std::vector<Type*> ArgTypes1 = {};
			FunctionType *FuncType1 = FunctionType::get(VoidTy, ArgTypes1, false);
            hookFunc1 = M.getOrInsertFunction("print1", FuncType1);
            hookFunc2 = M.getOrInsertFunction("print2", FuncType1);

			std::vector<Type*> ArgTypes2 = {Type::getInt32Ty(C)};
			FunctionType *FuncType2 = FunctionType::get(VoidTy, ArgTypes2, false);
			hookFunc3 = M.getOrInsertFunction("print3", FuncType2);
			
			std::vector<Type*> ArgTypes3 = {Type::getInt64Ty(C)};
			FunctionType *FuncType3 = FunctionType::get(VoidTy, ArgTypes3, false);
			hookFunc4 = M.getOrInsertFunction("print4", FuncType3);

			std::vector<Type*> ArgTypes4 = {Type::getInt8PtrTy(C), Type::getInt32Ty(C)};
			FunctionType *FuncType4 = FunctionType::get(VoidTy, ArgTypes4, false);
			hookFuncEA = M.getOrInsertFunction("printea", FuncType4);
			
			hookFuncRetMain = M.getOrInsertFunction("RetMain", FuncType1);

			std::vector<Type*> ArgTypes5 = {Type::getInt8PtrTy(C), Type::getInt32Ty(C)};
			FunctionType *FuncType5 = FunctionType::get(VoidTy, ArgTypes5, false);
			hookFuncString = M.getOrInsertFunction("takeString", FuncType5);

			std::vector<Type*> ArgTypes6 = {Type::getInt8PtrTy(C), Type::getInt8PtrTy(C), Type::getInt32Ty(C), Type::getInt32Ty(C)};
			FunctionType *FuncType6 = FunctionType::get(VoidTy, ArgTypes6, false);
			hookFuncFCall = M.getOrInsertFunction("callFunc", FuncType6);

			std::vector<Type*> ArgTypes7 = {Type::getInt8PtrTy(C), Type::getInt64Ty(C),  Type::getInt32Ty(C),  Type::getInt32Ty(C)};
			FunctionType *FuncType7 = FunctionType::get(VoidTy, ArgTypes7, false);
			hookFuncMallocEvent = M.getOrInsertFunction("recordMEvent", FuncType7);

			//		Type::getFloatTy(M.getContext())  for float
			hook1 = hookFunc1; hook2 = hookFunc2; hook3 = hookFunc3;
			hook4 = hookFunc4; hookEA = hookFuncEA;
			hookRetMain = hookFuncRetMain;
			hookString = hookFuncString;
			hookFCall = hookFuncFCall;
			hookMallocEvent = hookFuncMallocEvent;

            for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
            {
				// 	errs() << F->getName().str() << "\n";
				flag1 = false;
				for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
				{
					instru_host::runOnBasicBlock(BB);//TODO: for overhead testing
				}
            }



			return false;
        }

    	virtual bool runOnBasicBlock(Function::iterator &BB)
        {
        	LLVMContext &C = BB->getContext();
			Value* ZERO = ConstantInt::get(Type::getInt32Ty(C), 0);
			Value* ONE = ConstantInt::get(Type::getInt32Ty(C), 1);
			Value* TWO = ConstantInt::get(Type::getInt32Ty(C), 2);
			Value* THREE = ConstantInt::get(Type::getInt32Ty(C), 3);

			Function* bb_func = BB->getParent();
			StringRef f_name = bb_func->getName();	
			std::string func_name = f_name.str();	
			std::string bb_name = BB->getName().str();
			// errs() << func_name << " : " << bb_name << "\n" ;
			int cnt = 0;
	
			IRBuilder<> bbbuilder(&(*BB));
			if(!flag1)
			{
				str_fname = bbbuilder.CreateGlobalStringPtr(f_name);
				ptr_fname = bbbuilder.CreatePointerCast(str_fname, Type::getInt8PtrTy(C) );
				flag1=true;
			}

	    	for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
            {
				cnt++;
		    	if (cnt==1 ) 
				{
					if( (bb_name.find("entry") != std::string::npos)  &&  (func_name.find("main") != std::string::npos)  )
					{      // insert AllocaInst into entry block of main function.
						errs() << "I am in main() function's entry block\n";
					}
					
					if( bb_name.find("kcall.end") != std::string::npos  &&  (bb_name.find("1") == std::string::npos) )
					{
						errs() << "I am in kcall.end block \n";
					}	
				}

				if ( auto *op = dyn_cast<ResumeInst>( &(*BI) ) )
				{
									errs() << " [[[\nI am Resume Inst!  :    "  << *BI <<  "\n ]]]\n";
					continue;
				}

				if ( auto *op = dyn_cast<ReturnInst>( &(*BI) ) )
				{
				
				//      errs() << " [[[\nI am ReturnInst !  :    "  << *BI <<  "\n";
				//	errs() << op->getFunction() -> getName().str() <<  "\n";

					StringRef fn = op->getFunction() -> getName();
					std::string fname = fn.str();
				//	if( (fname.find("cudaMalloc") == std::string::npos)  || (fname.find("_") != std::string::npos)  )
					//const char* ffname;
					//ffname = fname.c_str();

					IRBuilder<> builder(op);
					Value* args[] = { ptr_fname, THREE};
				//	CallInst *MI = builder.CreateCall(hookString, args);
					
					if (fname=="main")
					{
						errs() << "!!!!!!!!! main\n";
						builder.SetInsertPoint(&*BI);
						builder.CreateCall(hookRetMain, {});
					}
				//       errs() << " ]]]\n\n" ;
					continue;
				}

				if ( auto *op = dyn_cast<CallInst>( &(*BI) ) )
				{
					Function* f = op->getCalledFunction();
					if (!f) //this check is important for: %call =  call deferenceble bla bla...
						continue;	
					
						StringRef callee = op->getCalledFunction() -> getName();
					std::string calleeName = callee.str();

					if( (calleeName.find("llvm.dbg") != std::string::npos)  || (calleeName.find("llvm.stack") != std::string::npos)  
					|| (calleeName.find("llvm.lifetime") != std::string::npos) || (calleeName.find("llvm.memcpy") != std::string::npos)
					|| (calleeName.find("dim3") != std::string::npos) || (calleeName.find("basic_ostream") != std::string::npos) 
					|| (calleeName.find("_ZNS") != std::string::npos) 
					|| (calleeName.find("cudaLaunch") != std::string::npos) || (calleeName.find("cudaSetupArgument") != std::string::npos )
						)
						continue;

					StringRef caller = op->getFunction() -> getName();
					std::string callerName = caller.str();
				
					if( (callerName.find("__cuda_register_global") != std::string::npos)  || (callerName.find("__cuda_module_") != std::string::npos) )
						continue;

			//		errs() << " [[[\nI am an CallInst !  :    "  << *BI <<  "\n";
			//		errs() << "from : " << op->getFunction() -> getName().str() <<  "\n";
			//		errs() << op->getCalledFunction() -> getName().str() <<  "\n";

					IRBuilder<> builder(op);

					Value *str_caller, *ptr_caller;
					str_caller = builder.CreateGlobalStringPtr(callerName); // pass caller name
					ptr_caller = builder.CreatePointerCast(str_caller, Type::getInt8PtrTy(C) );
					Value *args[] = {ptr_caller, ONE};
				//	CallInst *MI = builder.CreateCall(hookString, args);

					Value *Str, *strPtr;
					Str = builder.CreateGlobalStringPtr(callee); // pass callee name
					strPtr = builder.CreatePointerCast(Str, Type::getInt8PtrTy(C) );
					Value *args2[] = {strPtr, TWO};
				//	builder.CreateCall(hookString, args2);

					const DebugLoc &loc = BI->getDebugLoc();
					if (loc)
					{
						int l =  loc.getLine();
						int cl =  loc.getCol();
						//builder.CreateCall(hook3, builder.getInt32(l) );
						builder.CreateCall(hookFCall, {ptr_caller, strPtr, builder.getInt32(l), builder.getInt32(cl) } );
					}
		
					if (calleeName=="main")
					{
						errs() << "!!!!!!!!! main\n";
						builder.SetInsertPoint(&*BI);
						builder.CreateCall(hookRetMain, {});
						
					}
		
					if( (calleeName.find("cudaMalloc") != std::string::npos)  && (calleeName.find("_") == std::string::npos)  )
					{
			//			errs() <<  op->getNumArgOperands() << "\n";
						Value* arg1 = op->getArgOperand(0);
						Value* ptr1 = builder.CreatePointerCast(arg1, Type::getInt8PtrTy(C) );
		//TODO
		//		do something to ptr1
		//				arg1 ->dump(); errs() << "\n";
		//				ptr1 ->dump(); errs() << "\n";
		//				ptr2 ->dump(); errs() << "\n";
		//				builder.CreateCall(hookEA, {arg1});
						builder.SetInsertPoint( &(*BB), ++builder.GetInsertPoint() ); // insert after op
						builder.CreateCall(hookEA, {ptr1,TWO});
						Value* arg2 = op->getArgOperand(1);
		//				arg2 ->dump(); errs() << "\n";
						builder.CreateCall(hook4, {arg2}); //Value* argsxy[] = {arg2};

						if (loc)
						{	
							int l =  loc.getLine();
												int cl =  loc.getCol();
							builder.CreateCall(hookMallocEvent, {ptr1, arg2,  builder.getInt32(l), builder.getInt32(cl) } );
						}

						//errs() << " ]]]\n\n" ;
					} 
					
					continue; // CHANGED!!
				}

				if ( auto *op = dyn_cast<BinaryOperator>( &(*BI) ) )
				{
					errs()  << " [[[\nI am an binary operator!  :    "  << *BI <<  "\n";
	/*				IRBuilder<> builder(op);
					builder.SetInsertPoint( &(*BB), ++builder.GetInsertPoint() );
				//	Value *TWO = ConstantInt::get(Type::getInt32Ty(C), 2);
				//	Value *Two = ConstantFP::get(Type::getFloatTy(C), 2.0);
					
					Value* args[] = {op};
					builder.CreateCall(hook3, args);			
	*/
					errs() << " ]]]\n\n" ;
					continue; // CHANGED!
				}

				if ( auto *op = dyn_cast<LoadInst>( &(*BI) ) )
				{
					errs()  << " [[[\nI am a load!  :    "  << *BI <<  "\n";

					IRBuilder<> builder(op); //insert before op
					
					builder.SetInsertPoint(&*BI);
					builder.CreateCall(hook1, {});

					builder.SetInsertPoint( &(*BB), ++builder.GetInsertPoint() ); // insert after op
					
					
					// Instruction *newInst = CallInst::Create(hook1,"");
					// newInst->insertBefore( &(*BI));			

					const DebugLoc &loc = BI->getDebugLoc();
					if (loc)
					{
						int l =  loc.getLine();
						//                  errs() <<"getLine  : " << loc.getLine() << "\n";
			//			errs() << "\nprint:\n";
			//			loc.print(errs());
				//		errs() << "\ndump: \n";
				//		loc.dump();
					//	errs() << "getScope : " << loc.getScope() << "\n";
					//	errs() << Scope->getFilename() << "\n";

						Value* L = ConstantInt::get(Type::getInt32Ty(C), l);
						Value* args[] = {L};
						//builder.CreateCall(hook3,args);
					}
					LoadInst *CI = dyn_cast<LoadInst>(BI);
					Value* addr = op->getPointerOperand();//TODO
	//				addr->print( errs(), true);
	//				addr->printAsOperand( errs(), true);
					Value *voidptr = builder.CreatePointerCast(addr, Type::getInt8PtrTy(C) );
	//				addr->dump(); errs() << "\n";
	//				voidptr->dump(); errs() << "\n";
					CallInst *MI = builder.CreateCall(hookEA, {voidptr,ONE});

					errs() << " ]]]\n\n" ;
				}

				if ( auto *op = dyn_cast<StoreInst>( &(*BI) ) )
				{
					errs()  << " [[[[\nI am a store!  :    "  << *BI <<  "\n";
					
					IRBuilder<> builder(op); //insert before op
					builder.SetInsertPoint(&*BI);
					builder.CreateCall(hook2, {});
					
					// Instruction *newInst = CallInst::Create(hook2,"");
					// 				newInst->insertBefore( &(*BI));	

					builder.SetInsertPoint( &(*BB), ++builder.GetInsertPoint() ); // insert after op

					const DebugLoc &loc = BI->getDebugLoc();
					if (loc)
					{
						int l =  loc.getLine();
						errs() <<"getLine  : " << loc.getLine() << "\n";
						Value *L = ConstantInt::get(Type::getInt32Ty(C), l);
						Value* args[] = {L};
						//builder.CreateCall(hook3,args);
					}

					
					StoreInst *CI = dyn_cast<StoreInst>(BI);
					Value  *addr = CI->getPointerOperand();
					Value *voidptr = builder.CreatePointerCast(addr, Type::getInt8PtrTy(C) );
					CallInst *MI = builder.CreateCall(hookEA, {voidptr, ONE});        

					errs() << " ]]]\n\n" ;
				}

            }

            return true;
        }
    
	};


// second pass for CPU, host
// for Global Variables
// deprecaded pass
//     struct instru_globalvar : public ModulePass{
//         static char ID;   
//         instru_globalvar() : ModulePass(ID) {}

//         virtual bool runOnModule(Module &M)
//         {

//         }
//     };


// pass:  GPU pass
// third pass
// 3rd pass
// memory accesses only
    struct instru_kernel_memory : public ModulePass{
        static char ID;   
        FunctionCallee hook1;
        FunctionCallee hook2;
        FunctionCallee hook3;
        FunctionCallee hook4;
        FunctionCallee hook5;
		FunctionCallee hookMain;
		FunctionCallee hookString;
		bool flag1;
		Value* str_fname;
		Value* ptr_fname;
		Type* VoidTy;
		Value* p_stackzone; 

        instru_kernel_memory() : ModulePass(ID) {}

        virtual bool runOnModule(Module &M)
        {

		LLVMContext &C = M.getContext();
		VoidTy = Type::getVoidTy(C); 

                errs() << "\n ======== 3rd pass: GPU kernel: memory =============== \n\n";

//// auto-detect CUDA kernels
		NamedMDNode* MDannotatios = M.getNamedMetadata("nvvm.annotations");
		
		if (MDannotatios == NULL){

			errs() << "I don't have any CUDA kernel\n";
			return false;
		}
		else{
			errs() << "I have CUDA kernels\n";
		}

        for(unsigned i=0; i<MDannotatios->getNumOperands(); i++)
		{
			MDNode* nn = MDannotatios->getOperand(i);
			////////////////errs()<<" I have operands " <<  nn->getNumOperands() << "\n" ;
			for(unsigned j=0; j<nn->getNumOperands(); j++)
			{
				if(! nn->getOperand(j))
					continue;

				//MDOperand  op = ( nn->getOperand(j));		
				Metadata*  op = ( nn->getOperand(j));
				//errs() << *op  << "\n";

				if( auto* str = dyn_cast<MDString>(nn->getOperand(j) ) )
				{
					std::string ss = str->getString().str();
					if (ss=="kernel")
					{
						errs() << "I found a CUDA kernel:\t";
						if( auto* v = dyn_cast<ValueAsMetadata>(nn->getOperand(j-1) ) )
							if (auto* F = dyn_cast<Function> (v->getValue())  )
							{
								errs() << F->getName() << "\n";
								StringRef fk = F->getName();
								std::string fkname = fk.str();
								CUDAKN[KNcnt].assign(fkname);
								KNcnt++;
							}
					}
				}
//		Metadata md =  *( nn->getOperand(j) );
 			}
		//	nn-> dump(); errs() << "\n";
		}

////	
        errs() << "\nLinking Analysis Functions:\n\n";
		for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
		{	// it is all about mangling
			StringRef fn = F->getName();
            std::string fname = fn.str();
			//	errs() << fname << "\n";
			
			int ii;
			for (ii=0; ii<AFsize; ii++)
			if ( fname.find(AF[ii]) != std::string::npos ) 
			{	
		//		errs() << AF[ii] << "  is changed to "  ;
				AF[ii].assign(fname);
		//		errs() << AF[ii] <<  "\n";
			}
		}

        FunctionCallee hookFunc1; FunctionCallee hookFunc2;
		FunctionCallee hookFunc3; FunctionCallee hookFunc4; FunctionCallee hookFunc5;
		FunctionCallee hookFuncMain;
		FunctionCallee hookFuncString;

		std::vector<Type*> ArgTypes1 = {};
		FunctionType *FuncType1 = FunctionType::get(VoidTy, ArgTypes1, false);
            
//      hookFunc1 = M.getOrInsertFunction(AF[0], FuncType1);

		std::vector<Type*> ArgTypes2 = {Type::getInt32Ty(C)};
		FunctionType *FuncType2 = FunctionType::get(VoidTy, ArgTypes2, false);
		hookFunc1 = M.getOrInsertFunction(AF[0], FuncType2);
        hookFunc2 = M.getOrInsertFunction(AF[1], FuncType1);
		
		std::vector<Type*> ArgTypes3 = {Type::getInt32Ty(C), Type::getInt32Ty(C)};
		FunctionType *FuncType3 = FunctionType::get(VoidTy, ArgTypes3, false);
		hookFunc3 = M.getOrInsertFunction(AF[2], FuncType3);

//		std::vector<Type*> ArgTypes4 = {Type::getInt64Ty(C)};
//		FunctionType *FuncType4 = FunctionType::get(VoidTy, ArgTypes4, false);
//		hookFunc4 = M.getOrInsertFunction("print4", FuncType4);

		std::vector<Type*> ArgTypes5 = {Type::getInt8PtrTy(C), Type::getInt32Ty(C),  Type::getInt32Ty(C),  Type::getInt32Ty(C), Type::getInt32Ty(C), Type::getInt8PtrTy(C)};
		FunctionType *FuncType5 = FunctionType::get(VoidTy, ArgTypes5, false);
		hookFunc5 = M.getOrInsertFunction(AF[3], FuncType5);

		//		hookFuncMain = M.getOrInsertFunction("fini", FuncType1);
		
		std::vector<Type*> ArgTypes6 = {Type::getInt8PtrTy(C), Type::getInt32Ty(C)};
		FunctionType* FuncType6 = FunctionType::get(VoidTy, ArgTypes6, false);
		hookFuncString = M.getOrInsertFunction(AF[4], FuncType6);

		//		Type::getFloatTy(M.getContext())  for float
		
		hook1= hookFunc1; hook2= hookFunc2; hook3= hookFunc3; 
//		hook4= hookFunc4; 
		hook5= hookFunc5;
//		hookMain = hookFuncMain;
        hookString = hookFuncString;

                errs() << "\nInstrument:\n\n";
        for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
		{
			bool native = true;
			std::string fname = F->getName().str();
		//	errs() << fname << "\n";
			int ii;
            for (ii=0; ii<AFsize; ii++)
                if ( fname.find(AF[ii]) != std::string::npos )
                {	native = false; 
                //	errs() << "found !" << fname << " \n";
				}

			if (!native)
			{
		//		errs() << "skipped. "<< "\n\n";
                continue;
			}

			flag1 = false;

			bool yeskernel = false;
			for (int ii=0; ii<KNcnt; ii++)
				if ( fname.find(CUDAKN[ii])!= std::string::npos )
				{
						yeskernel = true;
						break;
				}

				if (!yeskernel)
				{
					
					for (auto &a :  F->args() )
						p_stackzone = (Value*) &a;
					continue;
				}
				else
				{
					if ( auto *op = dyn_cast<CallInst>( F->getEntryBlock().getFirstNonPHI() ) )
					{       //we know that the first inst of cuda kernel is the InitKernel
							p_stackzone = (Value*) op;
							p_stackzone -> dump();
					}
					else
					{
						llvm::errs() << "No valid CallInst in the entry block!\n";
						continue;
					}
				}

				for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
					instru_kernel_memory::runOnBasicBlock32(BB);
			//	errs() <<  "next\n";
        	}
			
			return false;
        }
// 

        virtual bool runOnBasicBlock32(Function::iterator &BB)
        {
        	LLVMContext &C = BB->getContext();
			Value *ONE = ConstantInt::get(Type::getInt32Ty(C), 1);
			Value *TWO = ConstantInt::get(Type::getInt32Ty(C), 2);
			Value *THREE = ConstantInt::get(Type::getInt32Ty(C), 3);

	    	for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
            {
				if ( auto *op = dyn_cast<LoadInst>( &(*BI) ) )
                {
					//	errs()  << " [[[\nI am a load!  :    "  << *BI <<  "\n";

					IRBuilder<> builder(op); //insert before op
					
					const DebugLoc &loc = BI->getDebugLoc();
					if (!loc)
						printf(" I don't have a !dgb tag\n");

                    builder.SetInsertPoint( &(*BB), ++builder.GetInsertPoint() ); // insert after op	
                    if (loc)
                    {
						int l =  loc.getLine();
						int col =  loc.getCol();
						Value* L = builder.getInt32(l); 
						Value* sColm = builder.getInt32(col);
                        Value* args[] = {L, builder.getInt32(col) };
                //	CallInst* tmp = builder.CreateCall(hook3,args);
				//	CallInst* newInst = builder.CreateCall(hook1, ONE);
				//	tmp->setDebugLoc( loc);
				//	newInst->setDebugLoc( loc);

                        LoadInst* CI = dyn_cast<LoadInst>(BI);
                        Value* addr = CI->getPointerOperand();//TODO
						Value* voidptr = builder.CreatePointerCast(addr, Type::getInt8PtrTy(C) );
						Type* tp = CI->getType();
                // errs() << *tp << " : " << tp->getPrimitiveSizeInBits() <<  " : " << tp->getScalarSizeInBits() ;
						int sizebits = (int) tp->getPrimitiveSizeInBits();
						CallInst* MI = builder.CreateCall(hook5, {voidptr, builder.getInt32(sizebits), L, sColm,  ONE, p_stackzone });
						MI->setDebugLoc(loc);
					}

                //	 errs() << " ]]]\n\n" ;
					continue;
				}	

				if ( auto *op = dyn_cast<StoreInst>( &(*BI) ) )
				{
					//	errs()  << " [[[\nI am a Store!  :    "  << *BI <<  "\n";
				//		Instruction *newInst = CallInst::Create(hook1,"");
					IRBuilder<> builder(op); //insert before op
					
					const DebugLoc &loc = BI->getDebugLoc();
					if (!loc)
						printf(" I don't have a !dgb tag\n");

					//	newInst->insertAfter( &(*BI));
			
					builder.SetInsertPoint( &(*BB), ++builder.GetInsertPoint() ); // insert after op	
					if (loc)
					{
						int l =  loc.getLine();
						int col =  loc.getCol();
						Value* L = ConstantInt::get(Type::getInt32Ty(C), l);
						Value* scolm = builder.getInt32(col);
						Value* args[] = {L, scolm };
					//	CallInst* tmp = builder.CreateCall(hook3,args);
					//	CallInst* newInst = builder.CreateCall(hook1, TWO);
					//	tmp->setDebugLoc( loc);
					//	newInst->setDebugLoc( loc);
									
						StoreInst* CI = dyn_cast<StoreInst>(BI);
						Value* addr = CI->getPointerOperand();//TODO
						Value* voidptr = builder.CreatePointerCast(addr, Type::getInt8PtrTy(C) );

						Type* tp = CI->getValueOperand() -> getType();
					// errs() << *tp << " : " << tp->getPrimitiveSizeInBits() <<  " : " << tp->getScalarSizeInBits() ;
						int sizebits = (int) tp->getPrimitiveSizeInBits();

						// CallInst* MI = builder.CreateCall(hook5, {voidptr, L });
						CallInst* MI = builder.CreateCall(hook5, {voidptr, builder.getInt32(sizebits), L, scolm, TWO, p_stackzone });
						MI->setDebugLoc(loc);
					}

							//	errs() << " ]]]\n\n" ;
					continue;
				}
				
				continue;
            }

            return true;
        }
    
	};



// pass:  GPU pass
// fourth pass
// 4th pass
// basic block related instrumentation

    struct instru_kernel_branch : public ModulePass{
        static char ID;   
        FunctionCallee hook1;
        FunctionCallee hook2;
        FunctionCallee hook3;
        FunctionCallee hook4;
		FunctionCallee hookMain;
		FunctionCallee hookBB;
		FunctionCallee hookRet;
		FunctionCallee hookInit;
		FunctionCallee hookString;
		bool flag1;
		Value* str_fname;
		Value* ptr_fname;
		Type* VoidTy;
		Value* p_stackzone;

        instru_kernel_branch() : ModulePass(ID) {}

        virtual bool runOnModule(Module &M)
        {

			LLVMContext &C = M.getContext();
			VoidTy = Type::getVoidTy(C); 

            errs() << "\n ======== 4rd pass: GPU kernel branch instrumentation =============== \n\n";

			NamedMDNode* MDannotatios = M.getNamedMetadata("nvvm.annotations");
			//MDannotatios -> dump(); errs() << "\n";

            for(unsigned i=0; i<MDannotatios->getNumOperands(); i++)
			{
				//auto detect GPU kernels
				MDNode* nn = MDannotatios->getOperand(i);
				////////////////errs()<<" I have operands " <<  nn->getNumOperands() << "\n" ;
				for(unsigned j=0; j<nn->getNumOperands(); j++)
				{
					if(! nn->getOperand(j))
						continue;
					//MDOperand  op = ( nn->getOperand(j));		
					Metadata*  op = ( nn->getOperand(j));
					//errs() << *op  << "\n";
					if( auto* str = dyn_cast<MDString>(nn->getOperand(j) ) )
					{
						std::string ss = str->getString().str();
						if (ss=="kernel")
						{
							errs() << "I found a CUDA kernel:\t";
							if( auto* v = dyn_cast<ValueAsMetadata>(nn->getOperand(j-1) ) )
								if (auto* F = dyn_cast<Function> (v->getValue())  )
								{
									errs() << F->getName() << "\n";
									StringRef fk = F->getName();
									std::string fkname = fk.str();
									CUDAKN[KNcnt].assign(fkname);
									KNcnt++;
								}
						}
					}
				}
			}

            errs() << "\nLinking Analysis Functions:\n\n";
			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
			{	// it is all about mangling
				StringRef fn = F->getName();
						std::string fname = fn.str();
			//	errs() << fname << "\n";
				
				int ii;
				for (ii=0; ii<AFsize; ii++)
				if ( fname.find(AF[ii]) != std::string::npos ) 
				{	
				//	errs() << AF[ii] << "  is changed to "  ;
					AF[ii].assign(fname);
				//	errs() << AF[ii] <<  "\n";
				}
			}

            FunctionCallee hookFunc1; FunctionCallee hookFunc2;
			FunctionCallee hookFunc3; FunctionCallee hookFunc4;
			FunctionCallee hookFuncMain;
			FunctionCallee hookFuncBB;
			FunctionCallee hookFuncInit;
			FunctionCallee hookFuncString;

			std::vector<Type*> ArgTypes1 = {Type::getInt32Ty(C)};
			FunctionType *FuncType1 = FunctionType::get(VoidTy, ArgTypes1, false);
			hookFunc1 = M.getOrInsertFunction(AF[0], FuncType1);

			std::vector<Type*> ArgTypes2 = {};
			FunctionType *FuncType2 = FunctionType::get(VoidTy, ArgTypes2, false);
            hookFunc2 = M.getOrInsertFunction(AF[1], FuncType2);

			std::vector<Type*> ArgTypes3 = {Type::getInt32Ty(C), Type::getInt32Ty(C)};
			FunctionType *FuncType3 = FunctionType::get(VoidTy, ArgTypes3, false);
			hookFunc3 = M.getOrInsertFunction(AF[2], FuncType3);

			std::vector<Type*> ArgTypes4 = {Type::getInt8PtrTy(C), Type::getInt32Ty(C)};
			FunctionType *FuncType4 = FunctionType::get(VoidTy, ArgTypes4, false);
			hookFuncString = M.getOrInsertFunction(AF[4], FuncType4);

			std::vector<Type*> ArgTypes5 = {Type::getInt32Ty(C), Type::getInt32Ty(C),Type::getInt32Ty(C),Type::getInt32Ty(C), Type::getInt8PtrTy(C)};
			FunctionType *FuncType5 = FunctionType::get(VoidTy, ArgTypes5, false);
			hookFuncBB = M.getOrInsertFunction("passBasicBlock", FuncType5);


			//hookFuncBB = M.getOrInsertFunction("passBasicBlock", VoidTy, Type::getInt8PtrTy(C), Type::getInt32Ty(C),Type::getInt32Ty(C),Type::getInt32Ty(C), Type::getInt8PtrTy(C),  NULL);
			
			hook1= hookFunc1; 
			hook2= hookFunc2; 
			hook3= hookFunc3; 
            hookBB = hookFuncBB;
			hookString = hookFuncString;

            errs() << "\nInstrument:\n\n";

            for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
			{
				bool native = true;
				std::string fname = F->getName().str();
			//	errs() << fname << "\n";
				int ii;
				for (ii=0; ii<AFsize; ii++)
					if ( fname.find(AF[ii]) != std::string::npos )
					{	
						native = false;
						//	errs() << "found !" << fname << " \n";
					}

				if (!native)
					continue;

				flag1 = false;

				bool yeskernel = false;
				for (int ii=0; ii<KNcnt; ii++)
					if ( fname.find(CUDAKN[ii])!= std::string::npos )
					{
						yeskernel = true;
						break;
					}

				if (!yeskernel)
				{
					for (auto &a :  F->args() )
						p_stackzone = (Value*) &a;

					if ( p_stackzone->getType() != Type::getInt8PtrTy(C) )
					{
						errs() << "this function didn't have insert an extra pointer, probably because it is not called due to -O2. will skip\n";
						errs() << F->getName().str() << "\n";
						continue;
					}
				}
				else
				{
					if ( auto *op = dyn_cast<CallInst>( F->getEntryBlock().getFirstNonPHI() ) )
					{	//we know that the first inst of cuda kernel is the InitKernel
						p_stackzone = (Value*) op;
						p_stackzone -> dump();
					}
					else
					{
						llvm::errs() << "No valid CallInst in the entry block!\n";
						continue;
					}

	//				for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
	//					instru_kernel_branch::runOnBasicBlock41(BB, yeskernel); //check and retrieve p_stackzone
				}

				for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
					instru_kernel_branch::runOnBasicBlock42(BB, yeskernel);
			//	errs() <<  "next\n";
            }

			return false;
        }

        virtual bool runOnBasicBlock42(Function::iterator &BB, bool yeskernel)
        {
        	LLVMContext &C = BB->getContext();
			Value *ONE = ConstantInt::get(Type::getInt32Ty(C), 1);
			Value *TWO = ConstantInt::get(Type::getInt32Ty(C), 2);
			Value *THREE = ConstantInt::get(Type::getInt32Ty(C), 3);

			Function* bb_func = BB->getParent();
			StringRef f_name = bb_func->getName();	

			Instruction* inst = BB->getFirstNonPHI();
			const DebugLoc &loc = inst->getDebugLoc();
			std::string bb_name = BB->getName().str();
			IRBuilder<> builder(inst);
//			if ( auto* ci = dyn_cast<CallInst> (inst) )
//				if ( ci->getCalledFunction()->getName().str() == "InitKernel" )
//					builder.SetInsertPoint( &(*BB), ++builder.GetInsertPoint() ); 
				

			if(!flag1)
			{
				str_fname = builder.CreateGlobalStringPtr(f_name);
				ptr_fname = builder.CreatePointerCast(str_fname, Type::getInt8PtrTy(C) );
				flag1 = true;
			}

//              errs() << "\n ==== ===== next pair ==== ==== \n";

            if ( bb_name.length() == 0)
			{
				errs() << "length = 0!!!\n";
	//			return true;
			}

			Value* gv_bbname = builder.CreateGlobalStringPtr(bb_name);
			Value* str_bbname = builder.CreatePointerCast(gv_bbname, Type::getInt8PtrTy(C) );

			if (loc)
			{
	//	                errs() << f_name << "\n";
	//			errs() << bb_name << "\n";
				int ln = loc.getLine();
				int cl = loc.getCol();
				if (bb_name=="entry" && yeskernel)	//make sure InitKernel is always the first
					builder.SetInsertPoint( &(*BB), ++builder.GetInsertPoint() ); 

			//	errs() <<"function name: " <<  f_name<< "\n";
			//	errs() << "block name: " << bb_name << "\n";
				
				CallInst* ci = builder.CreateCall(hookBB, {ONE, ONE, builder.getInt32(ln), builder.getInt32(cl), p_stackzone } );
				//CallInst* ci = builder.CreateCall(hookBB, {str_bbname, ONE, builder.getInt32(ln), builder.getInt32(cl), p_stackzone } );
				ci->setDebugLoc(loc);
			} 
			else 	
			{
				for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
				{
					const DebugLoc &loc2 = BI->getDebugLoc();
					if (loc2)
					{
						int ln = loc2.getLine();
						int cl = loc2.getCol();

						if (bb_name=="entry" && yeskernel)	//make sure InitKernel is always the first
							builder.SetInsertPoint( &(*BB), ++builder.GetInsertPoint() ); 

						CallInst *ci = builder.CreateCall(hookBB, {ONE, ONE, builder.getInt32(ln), builder.getInt32(cl), p_stackzone} );
						//	CallInst *ci = builder.CreateCall(hookBB, {str_bbname, ONE, builder.getInt32(ln), builder.getInt32(cl), p_stackzone} );
						ci->setDebugLoc(loc2);
						break;
					}
				}
			}

			return true;
        }
    };



/// pass:  GPU pass
// fifth pass
// 5th pass
// this pass is for constructing call path.
    struct instru_kernel_callpath : public ModulePass{
        static char ID;   
		FunctionCallee hookFCall;
		FunctionCallee hookString;
		bool flag1;
		Value* str_fname;
		Value* ptr_fname;
		Value* p_stackzone;
		Type* VoidTy; 

        instru_kernel_callpath() : ModulePass(ID) {}

        virtual bool runOnModule(Module &M)
        {

			LLVMContext &C = M.getContext();
			VoidTy = Type::getVoidTy(C); 

            errs() << "\n ======== 5th pass: GPU constructing call path  =============== \n\n";

			NamedMDNode* MDannotatios = M.getNamedMetadata("nvvm.annotations");
			//MDannotatios -> dump(); errs() << "\n";

            for(unsigned i=0; i<MDannotatios->getNumOperands(); i++)
			{
				//auto detect GPU kernels
				MDNode* nn = MDannotatios->getOperand(i);
				
			////////////////errs()<<" I have operands " <<  nn->getNumOperands() << "\n" ;
				for(unsigned j=0; j<nn->getNumOperands(); j++)
				{
					if(! nn->getOperand(j))
						continue;

					//MDOperand  op = ( nn->getOperand(j));		
					Metadata* op = ( nn->getOperand(j));
					//errs() << *op  << "\n";

					if( auto* str = dyn_cast<MDString>(nn->getOperand(j) ) )
					{
						std::string ss = str->getString().str();
						if (ss=="kernel")
						{
							errs() << "I found a CUDA kernel:\t";
							if( auto* v = dyn_cast<ValueAsMetadata>(nn->getOperand(j-1) ) )
								if (auto* F = dyn_cast<Function> (v->getValue())  )
								{
									errs() << F->getName() << "\n";
									StringRef fk = F->getName();
									std::string fkname = fk.str();
									CUDAKN[KNcnt].assign(fkname);
									KNcnt++;
								}
						}
					}
 				}
			}

////	
            errs() << "\nLinking Analysis Functions:\n\n";

			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
			{	
				// it is all about mangling
				StringRef fn = F->getName();
				std::string fname = fn.str();
				//	errs() << fname << "\n";
				
				int ii;
				for (ii=0; ii<AFsize; ii++)
				if ( fname.find(AF[ii]) != std::string::npos ) 
				{	
				//	errs() << AF[ii] << "  is changed to "  ;
					AF[ii].assign(fname);
				//	errs() << AF[ii] <<  "\n";
				}
			}

			FunctionCallee hookFuncFCall;
			FunctionCallee hookFuncString;

			std::vector<Type*> ArgTypes1 = {Type::getInt8PtrTy(C), Type::getInt32Ty(C)};
			FunctionType *FuncType1 = FunctionType::get(VoidTy, ArgTypes1, false);
			hookFuncString = M.getOrInsertFunction(AF[4], FuncType1);

			std::vector<Type*> ArgTypes2 = {Type::getInt8PtrTy(C), Type::getInt8PtrTy(C), Type::getInt32Ty(C),Type::getInt32Ty(C), Type::getInt8PtrTy(C)};
			FunctionType *FuncType2 = FunctionType::get(VoidTy, ArgTypes2, false);
			hookFuncFCall = M.getOrInsertFunction("callFunc", FuncType2);

            hookFCall = hookFuncFCall;
			hookString = hookFuncString;

		////
		////

			std::set<std::string> listF;
			listF.clear();
			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
				for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
									instru_kernel_callpath::runOnBasicBlock51(BB, listF);
									std::cout << "yo\n";

			errs() << "print out list F, length= "<< listF.size() << "\n";
			for(auto &f: listF)
			{
				errs() << f << "\n";
				instru_kernel_callpath::copyFunction( M, M.getFunction(f) );
			}
			errs() << "done printing\n";


		////
		////
	
			for ( int ii=0; ii<KNcnt; ii++)
			{
				// for all cuda kernels
				Function* F = M.getFunction(CUDAKN[ii]);
				for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
							instru_kernel_callpath::runOnBasicBlock53(BB);
								
			}

			//for all non-kernel device functions, such as foo_AA
			for(auto &f: listF)
			{
				Function* F = M.getFunction(f);

				for (auto &a :  F->args() )
					p_stackzone = (Value*) &a;
			//	p_stackzone -> dump();

				for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
									instru_kernel_callpath::runOnBasicBlock54(BB);
			}	


			errs() << "going to delele\n";

			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
			{
				std::string fname = F->getName().str();
				if ( fname.find("2delete") != std::string::npos )
					for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
						instru_kernel_callpath::runOnBasicBlock55(BB);
			}

			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
			{
				std::string fname = F->getName().str();
				if ( fname.find("2delete") != std::string::npos )
				{
					errs() << "will erase " << fname << "\n";	
					F->dropAllReferences();
					F->eraseFromParent();
					F = M.begin(); //to avoid iterator fault
				}
			}

			return false; //DEBUG
        }
// 
        virtual bool runOnBasicBlock51(Function::iterator &BB, std::set<std::string> &listF)
		{
			for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
			{
				if ( auto *op = dyn_cast<CallInst>( &(*BI) ) )
				{
					Function* f = op->getCalledFunction();
					if (!f) //this check is important for: %call =  call deferenceble bla bla...
						continue;

					std::string callerName = op->getFunction() -> getName().str();
					std::string calleeName = op->getCalledFunction() -> getName().str();

					bool yeskernel=false;
					for (int ii=0; ii<KNcnt; ii++)
						if ( callerName.find(CUDAKN[ii])!= std::string::npos )
						{
								yeskernel = true;
								break;
						}				


					bool yesnative = true;

					for (int ii=0; ii<AFsize; ii++)
						if ( calleeName.find(AF[ii]) != std::string::npos )
							yesnative = false;

					if (yeskernel && yesnative)
					{
						if ( ! op->getCalledFunction()-> isDeclaration() )
							listF.insert(calleeName);	// if called by a cuda kernel, enlist if not ... 
					}
					else
					{
						if (yesnative)
						{
							if( ! op->getCalledFunction()->isDeclaration())
							{
							//	errs() << callerName << "\t" ;
							//	errs() << "-->\t" ;
							//	errs() << calleeName << "\n\n" ;	
								listF.insert(calleeName);
							}
						}
					}
				}
			}

			return true;

		}

        virtual bool runOnBasicBlock55(Function::iterator &BB)
        {	
        	LLVMContext &C = BB->getContext();
			Value *ONE = ConstantInt::get(Type::getInt32Ty(C), 1);
			Value *TWO = ConstantInt::get(Type::getInt32Ty(C), 2);
			Value *THREE = ConstantInt::get(Type::getInt32Ty(C), 3);

			Function* bb_func = BB->getParent();
			StringRef f_name = bb_func->getName();	

			IRBuilder<> bbbuilder(&(*BB));

			if(!flag1)
			{
				str_fname = bbbuilder.CreateGlobalStringPtr(f_name);
				ptr_fname = bbbuilder.CreatePointerCast(str_fname, Type::getInt8PtrTy(C) );
				flag1 = true;
			}

	    	for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
            {
				if ( auto *op = dyn_cast<CallInst>( &(*BI) ) )
				{	
				//	errs() << op->getCalledFunction() -> getName().str();
					Function* f = op->getCalledFunction();
					
					if (!f) //this check is important for: %call =  call deferenceble bla bla...
							continue;

					int numArgs = cast<CallBase>(op)->arg_size();

					StringRef callee = op->getCalledFunction()->getName();
					std::string calleeName = callee.str();

					if( calleeName.find("2delete") == std::string::npos )
						continue;

					errs() << "55:\n";
					errs() << "caller: " << op->getFunction()->getName().str() << "\n";
					op->dump();

					op ->replaceAllUsesWith(UndefValue::get(op->getType()) );
					op ->eraseFromParent();

					BI = BB->begin(); //very important to avoid seg fault/core dump
					continue;
				}

            }

            return true;
		}

        virtual bool runOnBasicBlock53(Function::iterator &BB)
        {	
        	LLVMContext &C = BB->getContext();
			Value *ONE = ConstantInt::get(Type::getInt32Ty(C), 1);
			Value *TWO = ConstantInt::get(Type::getInt32Ty(C), 2);
			Value *THREE = ConstantInt::get(Type::getInt32Ty(C), 3);

			Function* bb_func = BB->getParent();
			StringRef f_name = bb_func->getName();	

			IRBuilder<> bbbuilder(&(*BB));

			if(!flag1)
			{
				str_fname = bbbuilder.CreateGlobalStringPtr(f_name);
				ptr_fname = bbbuilder.CreatePointerCast(str_fname, Type::getInt8PtrTy(C) );
				flag1 = true;
			}

	    	for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
            {
				if ( auto *op = dyn_cast<CallInst>( &(*BI) ) )
				{	
				//	errs() << op->getCalledFunction() -> getName().str();
					Function* f = op->getCalledFunction();

					if (!f) //this check is important for: %call =  call deferenceble bla bla...
							continue;

					// int numArgs =  op->getNumArgOperands();
					int numArgs = cast<CallBase>(op)->arg_size();

					StringRef callee = op->getCalledFunction()->getName();
					std::string calleeName = callee.str();

					if ( calleeName.find("llvm.dbg.")!= std::string::npos || calleeName.find("ptx.sreg")!= std::string::npos
						|| calleeName.find("nvvm.barrier")!= std::string::npos || calleeName.find("llvm.nvvm.")!= std::string::npos 
						|| calleeName.find("llvm.fabs")!= std::string::npos || calleeName.find("llvm.fma")!= std::string::npos
						|| calleeName.find("assertfail")!= std::string::npos
						|| calleeName == "RetKernel" || calleeName == "passBasicBlock" ) 
						continue;	//skip functions containing these keywords

					if ( calleeName == "InitKernel")
					{
						p_stackzone = (Value*)op;
						errs() << "53: assiging p stack\n" ;
						continue;
					}

					if( calleeName.find("2delete") == std::string::npos )
						continue;

					///// part 1
					///// replace the function call with new callee who has one extra argument 	
					IRBuilder<> builder(op);

					std::string newCallee = calleeName.erase( calleeName.find("2delete"), 7) ;
					Function* newF = op->getModule()->getFunction(newCallee);
					// Function::ArgumentListType &args = f -> getArgumentList();
					std::vector<Value*> newargs;

					for (int ii=0; ii<numArgs; ii++)
						newargs.push_back( op ->getArgOperand(ii) );

					newargs.push_back( p_stackzone );

					//      for(int ii=0; ii < newargs.size(); ii++)
					//              errs() << ii  << " " << *(newargs[ii]) << "\n";

					Value* newop = builder.CreateCall( newF, newargs);


				//	errs() << "53: new CallInst" ;
							//	newop ->dump();

					op->replaceAllUsesWith(newop);	//Exellent!!!

					//// part 2
					//// Instrument

				//	errs() << " [[[\nI am an CallInst !  :   "  << *BI <<  "\n";

					StringRef caller = op->getFunction() -> getName();
					std::string callerName = caller.str();
					Value *str_caller, *ptr_caller;

					str_caller = builder.CreateGlobalStringPtr(callerName); // pass callee name
					ptr_caller = builder.CreatePointerCast(str_caller, Type::getInt8PtrTy(C) );

					const DebugLoc &loc = BI->getDebugLoc();

					Value *args1[] = {ptr_caller, ONE};
					//CallInst* MI = builder.CreateCall(hookString, args1);

					Value *Str, *strPtr;
					Str = builder.CreateGlobalStringPtr(newCallee); // pass callee name
					strPtr = builder.CreatePointerCast(Str, Type::getInt8PtrTy(C) );
					Value *args2[] = {strPtr, TWO};
					//CallInst* tmpI = builder.CreateCall(hookString, args2);
					
					if(loc)
					{
						int ln = loc.getLine();
						int cl = loc.getCol();

						//MI -> setDebugLoc(loc);
						// tmpI -> setDebugLoc(loc);
						errs() << " I would pass this pointer at this instrumentation site " ;
						newop -> dump();	
						p_stackzone ->dump();
						CallInst* ci1 = builder.CreateCall( hookFCall, {ptr_caller, strPtr, builder.getInt32(ln), builder.getInt32(cl), p_stackzone} );	//DEBUG
						ci1 -> setDebugLoc(loc);
					}
					else
						errs() << " ]]]\n\n" ;

					op ->eraseFromParent();
					BI = BB->begin(); //very important to avoid seg fault/core dump
					continue;
				}

            }

            return true;
		}

        virtual bool runOnBasicBlock54(Function::iterator &BB)
        {	
			// I am for non-kernel device functions	
        	LLVMContext &C = BB->getContext();
			Value *ONE = ConstantInt::get(Type::getInt32Ty(C), 1);
			Value *TWO = ConstantInt::get(Type::getInt32Ty(C), 2);
			Value *THREE = ConstantInt::get(Type::getInt32Ty(C), 3);

			Function* bb_func = BB->getParent();
			StringRef f_name = bb_func->getName();	

			IRBuilder<> bbbuilder(&(*BB));
			
			if(!flag1)
			{
				str_fname = bbbuilder.CreateGlobalStringPtr(f_name);
				ptr_fname = bbbuilder.CreatePointerCast(str_fname, Type::getInt8PtrTy(C) );
				flag1 = true;
			}

	    	for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
            {
				if ( auto *op = dyn_cast<CallInst>( &(*BI) ) )
				{	
				//	errs() << op->getCalledFunction() -> getName().str();
					Function* f = op->getCalledFunction();
					
					if (!f) //this check is important for: %call =  call deferenceble bla bla...
							continue;

					int numArgs = cast<CallBase>(op)->arg_size();

					StringRef callee = op->getCalledFunction()->getName();
					std::string calleeName = callee.str();

					StringRef caller = op->getFunction() -> getName();
					std::string callerName = caller.str();

					if ( calleeName.find("llvm.dbg.")!= std::string::npos || calleeName.find("ptx.sreg")!= std::string::npos
						|| calleeName.find("nvvm.barrier")!= std::string::npos || calleeName.find("llvm.nvvm.")!= std::string::npos 
						|| calleeName.find("llvm.fabs")!= std::string::npos || calleeName.find("llvm.fma")!= std::string::npos
						|| calleeName.find("assertfail")!= std::string::npos
						|| calleeName == "RetKernel" || calleeName == "passBasicBlock" ) 
						continue;	//skip functions containing these keywords

					//if ( callerName.find("2delete") != std::string::npos)
					//	continue;	//skip, because the old function will be erased

					if ( calleeName.find("2delete") == std::string::npos)
						continue;	// skip, to avoid repeating instrumentaion

					///// part 1
					///// replace the function call with new callee who has one extra argument 	
					
					IRBuilder<> builder(op);
					
					errs() << "\n\n54 : caller= " << callerName << "\n";
					op->dump();

					std::string newCallee = calleeName.erase( calleeName.find("2delete"), 7) ;
					Function* newF = op->getModule()->getFunction(newCallee);
					
					// Function::ArgumentListType &args = f -> getArgumentList();

					std::vector<Value*> newargs;
					for (int ii=0; ii<numArgs; ii++)
						newargs.push_back( op ->getArgOperand(ii)  );

					errs() << "54 : 0\n";
					newargs.push_back( p_stackzone );
					//      for(int ii=0; ii < newargs.size(); ii++)
					//              errs() << ii  << " " << *(newargs[ii]) << "\n";
					Value* newop = builder.CreateCall( newF, newargs);
					
					//	errs() << "54: new CallInst" ;
					//	newop ->dump();

					errs() << "54 : 1\n";
					op->replaceAllUsesWith(newop);	//Exellent!!!

					errs() << "54 : 2\n";
					//// part 2
					//// Instrument

					//	errs() << " [[[\nI am an CallInst !  :   "  << *BI <<  "\n";

					const DebugLoc &loc = BI->getDebugLoc();

					Value *args1[] = {ptr_fname, ONE};
					//   CallInst* MI = builder.CreateCall(hookString, args1);

					Value *Str, *strPtr;
					Str = builder.CreateGlobalStringPtr(newCallee); // pass callee name
					strPtr = builder.CreatePointerCast(Str, Type::getInt8PtrTy(C) );
					Value *args2[] = {strPtr, TWO};
					// CallInst* tmpI = builder.CreateCall(hookString, args2);
					
					if(loc)
					{
						int ln = loc.getLine();
						int cl = loc.getCol();
						
						//MI -> setDebugLoc(loc);
						// tmpI -> setDebugLoc(loc);
						errs() << " I would pass this pointer at this instrumentation site " ;
						newop -> dump();	
						p_stackzone ->dump();
						CallInst* ci1 = builder.CreateCall( hookFCall, {ptr_fname, strPtr, builder.getInt32(ln), builder.getInt32(cl), p_stackzone} );
						ci1 -> setDebugLoc(loc);
					}
					else
						errs() << " ]]]\n\n" ;


					errs() << "54 : 3\n";

					op ->eraseFromParent();
					BI = BB->begin(); //very important to avoid seg fault/core dump
					continue;
				}

            }

            return true;
	}


		void copyFunction(Module &M, Function* F)
		{
			LLVMContext &C = M.getContext();

			// Function::ArgumentListType &args = F-> getArgumentList();
			std::string fname = F->getName().str();

			/////
			/////
			
			//construct the new function

			std::vector<Type*> argsTy;

			for ( auto &a : F->args())
					argsTy.push_back( a.getType() );
					
			argsTy.push_back( Type::getInt8PtrTy(C) );

			FunctionType* FuncTy = FunctionType::get( F->getReturnType(), argsTy, false);

			std::string newfname = fname+"asdfg";

			FunctionCallee fc_callee =  M.getOrInsertFunction( newfname ,FuncTy);
			Function* fc = cast<Function>(fc_callee.getCallee());

			fc->setCallingConv(CallingConv::C); // here C means C calling convention

			// set names for each argument
			Function::arg_iterator newiter = fc->arg_begin();
			for ( auto &a: F->args())
			{
				newiter->setName( a.getName() );
				newiter++;
			}
			
			newiter->setName("p_stackzone");

			fc->dump();

			ValueToValueMapTy VMap;
			Function::arg_iterator DestI = fc->arg_begin();

			for (const Argument & I : F->args())
				if (VMap.count(&I) == 0)      // Is this argument preserved?
				{
						DestI->setName(I.getName()); // Copy the name over...
						VMap[&I] = &*DestI++;        // Add mapping to VMap
				}

			SmallVector<ReturnInst*, 8> Returns;  // Ignore returns cloned.


			// ClonedCodeInfo *codeinfo = nullptr;
			// CloneFunctionInto(fc, &(*F) , VMap, false, Returns, "", codeinfo);
			CloneFunctionInto(fc, F, VMap,
				CloneFunctionChangeType::LocalChangesOnly,
				Returns);
			
			/////
			/////
			// now you want to append "2delete" to the orignal func name

			std::string newname = fname+"2delete";
			F->setName(newname);

			/////	
			/////
			// now: remove "asdfg" from the new function name
			fc->setName(fname);
					
			/////
			/////do you need to worry about metadata?

			/// F ->eraseFromParent();
		}

    };


/// pass:  GPU pass
// sixth pass
// 6th pass
// function signature modify
    struct instru_kernel_sig : public ModulePass{
        static char ID;   
		bool flag1;
		Value* str_fname;
		Value* ptr_fname;
		Type* VoidTy; 

        instru_kernel_sig() : ModulePass(ID) {}

        virtual bool runOnModule(Module &M)
        {

			LLVMContext &C = M.getContext();
			VoidTy = Type::getVoidTy(C); 

            errs() << "\n ======== 6th pass; kernel function and device routines signature modificatioin =============== \n\n";

//// auto-detect CUDA kernels
			NamedMDNode* MDannotatios = M.getNamedMetadata("nvvm.annotations");
            for(unsigned i=0; i<MDannotatios->getNumOperands(); i++)
			{
				MDNode* nn = MDannotatios->getOperand(i);
				////////////////errs()<<" I have operands " <<  nn->getNumOperands() << "\n" ;
				for(unsigned j=0; j<nn->getNumOperands(); j++)
				{
					if(! nn->getOperand(j))
						continue;
					//MDOperand  op = ( nn->getOperand(j));		
					//Metadata*  op = ( nn->getOperand(j));
					//errs() << *op  << "\n";
					if( auto* str = dyn_cast<MDString>(nn->getOperand(j) ) )
					{
						std::string ss = str->getString().str();
						if (ss=="kernel")
						{
							errs() << "I found a CUDA kernel:\t";
							if( auto* v = dyn_cast<ValueAsMetadata>(nn->getOperand(j-1) ) )
								if (auto* F = dyn_cast<Function> (v->getValue())  )
								{
									errs() << F->getName() << "\n";
									StringRef fk = F->getName();
									std::string fkname = fk.str();
									CUDAKN[KNcnt].assign(fkname);
									KNcnt++;
								}
						}
					}
				}
			}

////	
            errs() << "\nLinking Analysis Functions:\n\n";

			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
			{	
				// it is all about mangling
				StringRef fn = F->getName();
				std::string fname = fn.str();
				//errs() << fname << "\n";
				
				int ii;
				for (ii=0; ii<AFsize; ii++)
				if ( fname.find(AF[ii]) != std::string::npos ) 
				{	
				//	errs() << AF[ii] << "  is changed to ";
					AF[ii].assign(fname);
				//	errs() << AF[ii] <<  "\n";
				}
			}

////
////

            errs() << "\nModify kernel function signature:\n\n";
            
			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
			{	
				//round 1
				bool yeskernel = false;
				std::string fname = F->getName().str();
			//	errs() << fname << "\n";
				for (int ii=0; ii<KNcnt; ii++)
					if ( fname.find(CUDAKN[ii])!= std::string::npos && fname.find("asdfg") == std::string::npos)
						yeskernel = true;

				if (!yeskernel)
				{
				//	errs() << " not a kernel: skipped. "<< "\n\n";
					continue;
				}

				// Function::ArgumentListType &args = F-> getArgumentList();

				//construct the new function
				std::vector<Type*> argsTy;
				for ( auto &a : F->args())
					argsTy.push_back( a.getType() );

				argsTy.push_back( Type::getInt8PtrTy(C) );//typetag
			//	argsTy.push_back( Type::getInt8PtrTy(C) );//typetag
			//	argsTy.push_back( Type::getInt8PtrTy(C) );//typetag //yes, I need three pointers
				FunctionType* FuncTy = FunctionType::get( VoidTy, argsTy, false);

				std::string newfname = fname+"asdfg";
				FunctionCallee fc_callee =  M.getOrInsertFunction( newfname ,FuncTy);
				Function* fc = cast<Function>(fc_callee.getCallee());
				fc->setCallingConv(CallingConv::C); //here C means C calling convention

				//set names for each argument
				// Function::ArgumentListType &newargs = fc -> getArgumentList();
				// Function::ArgumentListType::iterator newiter = newargs.begin();


				Function::arg_iterator newiter = fc->arg_begin();
				
				for ( auto &a: F->args())
				{
				//	a.dump();
				//	newiter->dump();
					newiter->setName( a.getName() );
				//	newiter->dump();
					newiter++;
				}

				newiter->setName("ptrhead");
			//	newiter++;
			//	newiter->setName("b_stack");
			//	newiter++;
			//	newiter->setName("b_stackHeight");

				ValueToValueMapTy VMap;
				Function::arg_iterator DestI = fc ->arg_begin();
				for (const Argument & I : F->args())
					if (VMap.count(&I) == 0)      // Is this argument preserved?
					{
						DestI->setName(I.getName()); // Copy the name over...
						VMap[&I] = &*DestI++;        // Add mapping to VMap
					}

				SmallVector<ReturnInst*, 8> Returns;  // Ignore returns cloned.
				// ClonedCodeInfo *codeinfo = nullptr;
				// CloneFunctionInto(fc, &(*F) , VMap, false, Returns, "", codeinfo);
			
				CloneFunctionInto(fc, &(*F), VMap,
					CloneFunctionChangeType::LocalChangesOnly,
					Returns);
			}


			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
            {	
				// round 2
				bool newkernel = false;
				bool oldkernel = false;
                std::string fname = F->getName().str();
            	
				//	errs() << fname << "\n";
				for (int ii=0; ii<KNcnt; ii++)
					if ( fname.find(CUDAKN[ii])!= std::string::npos && fname.find("asdfg") == std::string::npos)
							oldkernel = true;
					else if ( fname.find("asdfg") != std::string::npos )
						newkernel = true;

				if (!oldkernel && !newkernel)
				{
						//errs() << " not a kernel: skipped. "<< "\n\n";
						continue;
				}

				errs() << " second round. "<< "\n\n";
				if ( oldkernel)
				{
					std::string newname = fname+"2delete";
					F->setName(newname);
				}
			}

			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
			{	// round 3 
				bool oldkernel = false;
				bool newkernel = false;
				std::string fname = F->getName().str();
				//errs() << fname << "\n";
				int ii;
				for (ii=0; ii<KNcnt; ii++)
					if ( fname.find(CUDAKN[ii])!= std::string::npos && fname.find("asdfg") == std::string::npos)
					{
						oldkernel = true;
						continue;
					}
					else if ( fname.find("asdfg") != std::string::npos )
					{
						newkernel = true;
						continue;
					}

				if (!oldkernel && !newkernel)
				{
						//errs() << " not a kernel: skipped. "<< "\n\n";
						continue;
				}
				errs() << " third round. "<< "\n\n";

				if ( newkernel)
				{
					errs() << fname << "\n";
					std::string newname = fname.erase( fname.find("asdfg"), 5);
					errs() << newname << "\n";
					F->setName(newname);
				}
			}

		//////
		//////
		//NamedMDNode* MDannotatios = M.getNamedMetadata("nvvm.annotations");
		//MDannotatios -> dump(); errs() << "\n";
        for(unsigned i=0; i<MDannotatios->getNumOperands(); i++)
		{//auto detect GPU kernels
			MDNode* nn = MDannotatios->getOperand(i);
			////////////////errs()<<" I have operands " <<  nn->getNumOperands() << "\n" ;
			for(unsigned j=0; j<nn->getNumOperands(); j++)
			{
				if(! nn->getOperand(j))
					continue;
				if( auto* str = dyn_cast<MDString>(nn->getOperand(j) ) )
				{
					std::string ss = str->getString().str();
					if (ss=="kernel")
					{
						errs() << "I found a CUDA kernel in metadata and will replace it \t";

						if( auto* v = dyn_cast<ValueAsMetadata>(nn->getOperand(j-1) ) )
							if (auto* F = dyn_cast<Function> (v->getValue())  )
							{
								errs() << F->getName() << "\n";
								std::string fname = F->getName().str();
								std::string newname = fname.erase( fname.find("2delete"), 7);
								Function* newF = M.getFunction(newname);
								//F->setName(newname);
								//errs() << " going to replace this new func : " << newF->getName().str() << "\n";
								ValueAsMetadata* newVMdata = ValueAsMetadata::get(newF);
								nn->replaceOperandWith( j-1, newVMdata ); 
							}		
					}
				}
 			}
		}

		/////
		/////
		errs() << " \n\nnow delete those old kernels. "<< "\n";
		std::string ToDelete[20];
		int TDcnt = 0;	

		for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
        {	
			// round 4 
		//	Function* f = (Function*)(&F);	
		//	errs() << f->getName() << "vanity check\n";
		//	if ( !f )
		//		continue;
		//	errs() << f->getName() << "vanity check\n";
        	std::string fname = F->getName().str();
        	//errs() << fname << "\n";
        	for (int ii=0; ii<KNcnt; ii++)
                if ( fname.find(CUDAKN[ii])!= std::string::npos && fname.find("2delete") != std::string::npos)
				{
					ToDelete[TDcnt].assign(fname);
					TDcnt++;
					continue;
				}
        }
                        
		for (int ii=0; ii<TDcnt; ii++)
		{
			Function* f = M.getFunction( ToDelete[ii] );
			if(f)
				f->eraseFromParent();
			errs() << "To Delete: " << ToDelete[ii] << "\n";
		}

		return false;
        }


    }; //end of pass: instru_kernel_sig


/// pass:  host pass
// seventh pass
// 7th pass
// host-side function signature modify
    struct instru_host_sig : public ModulePass{
        static char ID;   
        FunctionCallee hook1;
        FunctionCallee hook2;
        FunctionCallee hook3;
        FunctionCallee hook4;
        FunctionCallee hook5;
		FunctionCallee hookMain;
		FunctionCallee hookString;
		FunctionCallee hookRet;
		FunctionCallee hookGridDims;
		FunctionCallee hookGridReset;
		FunctionCallee hookGridGet;
		bool flag1;
		Value* str_fname;
		Value* ptr_fname;
		Type* VoidTy;
		int offset = 0; //this is offset for cudaSetupArgument()
		int prevSize = 0; //this is size for cudaSetupArgument()
		Value* buffer_h = NULL;
		Value* buffer_d = NULL;
		Value* buffer_stack = NULL; //for global stack on GPU
	//	Value* buffer_stackHeight = NULL; //for global stack height on GPU
		

        instru_host_sig() : ModulePass(ID) {}

        virtual bool runOnModule(Module &M)
        {

			LLVMContext &C = M.getContext();
			VoidTy = Type::getVoidTy(C); 

            errs() << "\n ======== 7th pass; host side function signature modificatioin =============== \n\n";

			FunctionCallee hookFuncAppendTrace;
			FunctionCallee hookFuncGetHandle;
			FunctionCallee hookFuncGridDims;
			FunctionCallee hookFuncGridReset;
			FunctionCallee hookFuncGridGet;
			FunctionCallee hookAPI_malloc;
			FunctionCallee hookAPI_free;

			FunctionCallee hookFuncCudaSync;

			Function* hookCudaSync;

			std::vector<Type*> ArgTypes1 = {};
			FunctionType *FuncType1 = FunctionType::get(Type::getInt32Ty(C), ArgTypes1, false);
			hookFuncCudaSync = M.getOrInsertFunction("cudaDeviceSynchronize", FuncType1);

			std::vector<Type*> ArgTypes2 = {Type::getInt8PtrTy(C), Type::getInt8PtrTy(C)};
			FunctionType *FuncType2 = FunctionType::get(VoidTy, ArgTypes2, false);
			hookFuncAppendTrace = M.getOrInsertFunction("appendTrace", FuncType2);

			std::vector<Type*> ArgTypes3 = {Type::getInt32Ty(C)};
			FunctionType *FuncType3 = FunctionType::get(Type::getInt8PtrTy(C), ArgTypes3, false);
			hookFuncGetHandle = M.getOrInsertFunction("getHandle", FuncType3);

			std::vector<Type*> ArgTypes4 = {Type::getInt32Ty(C), Type::getInt32Ty(C), Type::getInt32Ty(C)};
			FunctionType *FuncType4 = FunctionType::get(VoidTy, ArgTypes4, false);
			hookFuncGridDims = M.getOrInsertFunction("gridDims", FuncType4);

			std::vector<Type*> ArgTypes5 = {};
			FunctionType *FuncType5 = FunctionType::get(VoidTy, ArgTypes5, false);
			hookFuncGridReset = M.getOrInsertFunction("gridReset", FuncType5);

			std::vector<Type*> ArgTypes6 = {};
			FunctionType *FuncType6 = FunctionType::get(Type::getInt64Ty(C), ArgTypes6, false);
			hookFuncGridGet = M.getOrInsertFunction("gridGet", FuncType6);

			std::vector<Type*> ArgTypes7 = {Type::getInt64Ty(C)};
			FunctionType *FuncType7 = FunctionType::get(Type::getInt8PtrTy(C), ArgTypes7, false);
			hookAPI_malloc = M.getOrInsertFunction("malloc", FuncType7);

			std::vector<Type*> ArgTypes8 = {Type::getInt8PtrTy(C)};
			FunctionType *FuncType8 = FunctionType::get(VoidTy, ArgTypes8, false);
			hookAPI_free = M.getOrInsertFunction("free", FuncType8);

			hookGridDims = hookFuncGridDims;
			hookGridReset = hookFuncGridReset;
			hookGridGet = hookFuncGridGet;

//// auto-detect CUDA kernels on host side 
		Function* fcrf = M.getFunction("__cuda_register_globals");
		if (fcrf) //just in case
		{
			//errs() << " I am in __cuda_register_globals function"; 
			//errs() << "\n";
			errs() << " I found these CUDA kernel functions: "; 
			errs() << "\n";
			for(Function::iterator BB = fcrf->begin(), E = fcrf->end(); BB != E; ++BB)
	    			for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
					if ( auto* ci = dyn_cast<CallInst>(BI) )
					{
						Function* callee = ci->getCalledFunction();
						std::string calleeName = callee->getName().str();
						if ( calleeName.find("cudaRegisterFunction") != std::string::npos )
						{
							Value* arg1 =  ci->getArgOperand(1);
                                        		User* bc_op = (User*) arg1; //the second argument is a BitCastOperator
                                        		Value* fkernel = bc_op->getOperand(0); //the beginning operand is the function we are looking for
                                        		std::string fname = ((Function*)fkernel) ->getName().str();
							errs() << fname << "\n";
							CUDAKN[KNcnt].assign(fname);
							KNcnt++;
						}
					}
		}

////	
                errs() << "\nLinking Analysis Functions:\n\n";
		for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
		{	// it is all about mangling
			StringRef fn = F->getName();
            std::string fname = fn.str();
		//	errs() << fname << "\n";
			
			int ii;
			for (ii=0; ii<AFsize; ii++)
			if ( fname.find(AF[ii]) != std::string::npos ) 
			{	
				errs() << AF[ii] << " is changed to "  ;
				AF[ii].assign(fname);
				errs() << AF[ii] <<  "\n";
			}
		}


                errs() << "\nInstrument:\n\n";
        for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
		{	//round 1
			bool yeskernel = false;
			std::string fname = F->getName().str();
			//errs() << fname << "\n";
			int ii;
			for (int ii=0; ii<KNcnt; ii++)
                if ( fname.find(CUDAKN[ii])!= std::string::npos && fname.find("asdfg") == std::string::npos)
					yeskernel = true;

			if (!yeskernel)
			{
				//errs() << " not a kernel: skipped. "<< "\n\n";
                 continue;
			}

			errs() << fname << "\n";
			// Function::ArgumentListType &args = F-> getArgumentList();

			//construct the new function
			std::vector<Type*> argsTy;
			for ( auto &a : F->args())
				argsTy.push_back( a.getType() );
			argsTy.push_back( Type::getInt8PtrTy(C) );
		//	argsTy.push_back( Type::getInt8PtrTy(C) );
		//	argsTy.push_back( Type::getInt8PtrTy(C) );// Yes, I need three pointers

			FunctionType* FuncTy = FunctionType::get( VoidTy, argsTy, false);

			std::string newfname = fname+"asdfg";
			FunctionCallee fc_callee =  M.getOrInsertFunction( newfname ,FuncTy);
			Function* fc = cast<Function>(fc_callee.getCallee());
			fc->setCallingConv(CallingConv::C); //here C means C calling convention

			//set names for each argument
			// Function::ArgumentListType &newargs = fc -> getArgumentList();
			// Function::ArgumentListType::iterator newiter = newargs.begin();

			Function::arg_iterator newiter = fc->arg_begin();
			for ( auto &a: F->args())
			{
			//	a.dump();
			//	newiter->dump();
				newiter->setName( a.getName() );
			//	newiter->dump();
				newiter++;
			}
			newiter->setName("ptrhead");
		//	newiter++;
		//	newiter->setName("b_stack");
		//	newiter++;
		//	newiter->setName("b_stackHeight");

			ValueToValueMapTy VMap;
			Function::arg_iterator DestI = fc ->arg_begin();
			for (const Argument & I : F->args())
				if (VMap.count(&I) == 0)      // Is this argument preserved?
				{
					DestI->setName(I.getName()); // Copy the name over...
					VMap[&I] = &*DestI++;        // Add mapping to VMap
				}

			SmallVector<ReturnInst*, 8> Returns;  // Ignore returns cloned.
			// ClonedCodeInfo *codeinfo = nullptr;
			//CloneFunctionInto(fc, &(*F) , VMap, false, Returns);//bitcode compile error
			// CloneFunctionInto(fc, &(*F) , VMap, false , Returns, "", codeinfo); //TODO
			CloneFunctionInto(fc, &(*F), VMap,
				CloneFunctionChangeType::LocalChangesOnly,
				Returns);
            
		}


		errs() << "\n\n second round. "<< "\n";
		for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
        {	// round 2
            bool oldkernel = false;
            bool newkernel = false;
            std::string fname = F->getName().str();
            //errs() << fname << "\n";
            for (int ii=0; ii<KNcnt; ii++)
                if ( fname.find(CUDAKN[ii])!= std::string::npos && fname.find("asdfg") == std::string::npos)
                    oldkernel = true;
				else if ( fname.find("asdfg") != std::string::npos )
					newkernel = true;

			if (!oldkernel && !newkernel)
					continue;

			if ( oldkernel)
			{
				std::string newname = fname+"2delete";
				F->setName(newname);//does this automatically updates all calls to this function? //seems yes
                errs() << fname << " to " << newname << "\n";
			}
		}

		errs() << " \n\nthird round. "<< "\n";
		for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
        {	// round 3 
            bool oldkernel = false;
            bool newkernel = false;
            std::string fname = F->getName().str();
            //errs() << fname << "\n";
			int ii;
            for (ii=0; ii<KNcnt; ii++)
                if ( fname.find(CUDAKN[ii])!= std::string::npos && fname.find("asdfg") == std::string::npos)
				{
                    oldkernel = true;
					continue;
				}
                else if ( fname.find("asdfg") != std::string::npos )
				{
                    newkernel = true;
					continue;
				}

			if (!oldkernel && !newkernel)
			{
					//errs() << " not a kernel: skipped. "<< "\n\n";
					continue;
			}

			if ( newkernel)
			{
				std::string newname = fname.erase( fname.find("asdfg"), 5);
				F->setName(newname);
				errs() << fname << " to " << newname << "\n";
			}
        }

		////////
		////////
		// replacing old uses of 2delete function
        for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
		{
			bool yeskernel = false;
			std::string fname = F->getName().str();
			for (int ii=0; ii<KNcnt; ii++)
				if ( fname.find(CUDAKN[ii])!= std::string::npos && fname.find("2delete") == std::string::npos)
				{
					yeskernel = true;
					break;
				}

			if (yeskernel) 
			{	
				errs() << "found kernel " << fname << "\n";
                		for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
                			instru_host_sig::runOnBasicBlock71(BB); // this is for kernel only
			} else
			{
				for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
					instru_host_sig::runOnBasicBlock72(BB); // this is for non-kernel only
			}
            	}

		/////////
		/////////

		errs() << " \n\nnow delete those old kernels. "<< "\n";
		std::string ToDelete[20];
		int TDcnt = 0;	

		for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
                {	// round 4 
                        std::string fname = F->getName().str();
                        //errs() << fname << "\n";
                        for (int ii=0; ii<KNcnt; ii++)
                                if ( fname.find(CUDAKN[ii])!= std::string::npos && fname.find("2delete") != std::string::npos)
				{
					ToDelete[TDcnt].assign(fname);
					TDcnt++;
					continue;
				}
                }
                        
		for (int ii=0; ii<TDcnt; ii++)
		{
			errs() << "To Delete: " << ToDelete[ii] << "\n";
			Function* f = M.getFunction( ToDelete[ii] );
			if(f)
			{
				f->deleteBody();
				f->dropAllReferences();
			//	f->replaceAllUsesWith( UndefValue::get( (Type*)f->getType()) ); 
				f->eraseFromParent();
			}
		}

		////////
		////////

		// instrumenting cuda kernel calls or invokes
                for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
                {
                        bool yeskernel = false;
                        std::string fname = F->getName().str();
                        for (int ii=0; ii<KNcnt; ii++)
                                if ( fname.find(CUDAKN[ii])!= std::string::npos && fname.find("2delete") == std::string::npos)
                                {
                                        yeskernel = true;
                                        break;
                                }

                        if (!yeskernel)
                                for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
                                        instru_host_sig::runOnBasicBlock73(BB); //run this only for non-kernel functions
		}

		////////
		////////

		errs() << " done host_sig pass.\n\n" ;
        }
// 

        virtual bool runOnBasicBlock71(Function::iterator &BB)
        {
        	LLVMContext &C = BB->getContext();

			Function* bb_func = BB->getParent();
			StringRef fname = bb_func -> getName();
			std::string bbname = BB->getName().str();
			Value* ptrhead, *b_stack; // *b_stackHeight;

			int instcnt = 0;
			if (bbname.find("entry") != std::string::npos )
			{
			//	errs() << "I am in entry block of " << fname.str()<< "\n" ;

				AllocaInst* ai; //, *ai2; //, *ai3;
				StoreInst* si; // *si2; //, *si3; 
				CastInst* ci;
				for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
				{ 
					instcnt ++;
					
					if (instcnt == 1)
					{
						IRBuilder<> builder(&(*BI));
						ai = builder.CreateAlloca( Type::getInt8PtrTy(C) , NULL, "ptrhead.addr");
					//	ai2 = builder.CreateAlloca( Type::getInt8PtrTy(C) , NULL, "b_stack.addr");
					//	ai3 = builder.CreateAlloca( Type::getInt8PtrTy(C) , NULL, "b_stackHeight.addr");
						ai->setAlignment(llvm::Align(8));
					//	ai2->setAlignment(8);
					//	ai3->setAlignment(8);

						// Function::ArgumentListType &args = bb_func -> getArgumentList();
						errs() << " asdfgg size = "  << bb_func->arg_size() << "\n"; 

						Value* arg_ptrhead; //, *arg_stack; //, *arg_stackHeight;

						int ii=0;
					/*	for (auto &a :  args )
											{
													if ( ii==args.size()-3 )
															arg_ptrhead = (Value*) &a;
													else if ( ii == args.size()-2)
															arg_stack = (Value*) &a;
													else if ( ii == args.size()-1)
															arg_stackHeight = (Value*) &a;
													ii++;
											}
	*/
						for (auto &a :  bb_func->args() )
						{
							if ( ii==bb_func->arg_size()-1 )
								arg_ptrhead = (Value*) &a;
						//	else if ( ii == args.size()-1)
						//		arg_stack = (Value*) &a;
							ii++;
							(&a) ->dump();
						}
						//errs() << *arg;	
						//errs() << "\n";

						si = builder.CreateStore( arg_ptrhead , ai); 
						//si2 = builder.CreateStore( arg_stack , ai2);
						//si3 = builder.CreateStore( arg_stackHeight , ai3);
						si->setAlignment(llvm::Align(8));
						//si2->setAlignment(8);
						//si3->setAlignment(8);

						ptrhead = builder.CreatePointerCast(ai, Type::getInt8PtrTy(C));
						//b_stack = builder.CreatePointerCast(ai2, Type::getInt8PtrTy(C));
						//b_stackHeight = builder.CreatePointerCast(ai3, Type::getInt8PtrTy(C));
					}
				}
			}

	    	for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
            	{
			if ( auto *op = dyn_cast<CallInst>( &(*BI) ) )
			{
				Function* f = op->getCalledFunction();
				if (!f) //this check is important for: %call =  call deferenceble bla bla...
					continue;	
				StringRef callee = op->getCalledFunction() -> getName();
                                std::string calleeName = callee.str();

				if( (calleeName.find("cudaSetupArgument") != std::string::npos)  )  
				{
				//	errs() << "cudaSetupArgument " << "\n";
					Value* arg = op->getArgOperand(2); //the last one is offset
					if (auto* op = dyn_cast<ConstantInt>(arg) )
						offset =  op->getSExtValue();

					Value* arg1 = op->getArgOperand(1); //the second one is size
					if (auto* op = dyn_cast<ConstantInt>(arg1) )
						prevSize =  op->getSExtValue();

					//errs() << "\n";
				}
				else if( (calleeName.find("cudaLaunch") != std::string::npos)  )
                                {
				//	errs() << "cudaLaunch: " << op->getNumArgOperands() << "\n";
					Value* arg = op->getArgOperand(0);
					
					IRBuilder<> builder(&(*BI));
					///
					/// insert one more cudaSetupArgument right before cudaLuanch
					errs() << "offset is " << offset << "\n";
					errs() << "size is " << prevSize  << "\n";

					int myOffset = offset+ prevSize;

					errs() << "my offset is " << myOffset  << "\n";
					if ( myOffset%8 != 0)
						myOffset = (myOffset/8)*8+8;	//make sure align to 8

					errs() << "final offset is " << myOffset  << "\n";

					errs() << "\n";
					Function* hookCudaArg = BI->getModule()->getFunction("cudaSetupArgument");
					//hookCudaArg ->dump();
					if (!hookCudaArg) {
						llvm::errs() << "Error: cudaSetupArgument function not found in module.\n";
						return false; // or handle error appropriately
					}

					std::cout << "Before call\n";
					builder.CreateCall( hookCudaArg, {ptrhead /*generated in entry block*/ , builder.getInt64(8), builder.getInt64(myOffset) }  );
					std::cout << "After call\n";

					//	builder.CreateCall( hookCudaArg, {b_stack /*generated in entry block*/ , builder.getInt64(8), builder.getInt64(16+offset) }  );
				//	builder.CreateCall( hookCudaArg, {b_stackHeight /*generated in entry block*/ , builder.getInt64(8), builder.getInt64(24+offset) }  );

					/// 
					/// replace old kernel call
					Function* F = BB -> getParent();
					Value* newcall = builder.CreatePointerCast( F , Type::getInt8PtrTy(C))  ;
					op->setArgOperand(0, newcall); //simply substitute the first argument
					errs() << "newcall :" <<  *newcall << "\n";
				}	
			}
            	}

            	return true;
	}


        virtual bool runOnBasicBlock72(Function::iterator &BB)
        {
        	LLVMContext &C = BB->getContext();

			Function* bb_func = BB->getParent();
			StringRef fname = bb_func -> getName();
			std::string bbname = BB->getName().str();

	    	for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
            {
				if ( auto *op = dyn_cast<InvokeInst>( &(*BI) ) )
				{
					Function* f = op->getCalledFunction();
									if (!f) //this check is important for: %call =  call deferenceble bla bla...
											continue;
									StringRef callee = op->getCalledFunction() -> getName();
									std::string calleeName = callee.str();

									if( (calleeName.find("2delete") != std::string::npos)  )
					{
						errs() << "I find a InvokeInst " << calleeName << "\n" ;
							
						int numArgs = cast<CallBase>(op)->arg_size();

						std::string newname = calleeName.erase( calleeName.find("2delete") ,7);
						Function* newcall = op->getModule()->getFunction( newname );	//search and retrieve the real kernel function to call
						errs() << "host-sig: will Invoke " <<  newcall->getName() <<"\n";

						IRBuilder<> builder(&(*BI));
						///
						///

							
						BasicBlock* b1 = op->getNormalDest();
						BasicBlock* b2 = op->getUnwindDest();
						///

						// Function::ArgumentListType &args = f -> getArgumentList();
						std::vector<Value*> newargs;
											for (int ii=0; ii<numArgs; ii++)
							newargs.push_back( op ->getArgOperand(ii)  );

						Value* str = builder.CreateGlobalStringPtr("TODO");//TODO
						Value* tmp = builder.CreatePointerCast(str, Type::getInt8PtrTy(C) );
						newargs.push_back( tmp ); 
					//	newargs.push_back( tmp ); 
				//		newargs.push_back( tmp );//yes, I need three pointers 

					//	for(int ii=0; ii < newargs.size(); ii++)
					//		errs() << ii  << " " << *(newargs[ii]) << "\n";
					//	Value *toshow = builder.CreateInvoke( newcall, b1, b2, newargs);
						Value *toshow = builder.CreateCall( newcall, newargs);
						builder.CreateBr( b1);
						toshow ->dump();
			
					//	errs() << "about to remove the instruction below : \n";
							//	op ->dump();
						op ->eraseFromParent();
						BI = BB->begin(); //very important to avoid seg fault/core dump
					}
				}

			if ( auto *op = dyn_cast<CallInst>( &(*BI) ) )
			{
				Function* f = op->getCalledFunction();
				if (!f) //this check is important for: %call =  call deferenceble bla bla...
					continue;	
				StringRef callee = op->getCalledFunction() -> getName();
                                std::string calleeName = callee.str();

				if( (calleeName.find("2delete") != std::string::npos)  ) 
				{	
				////////// change the call to old kernel to the new kernel, and set a pointer as a temporal argument 
				int numArgs = cast<CallBase>(op)->arg_size();

					std::string newname = calleeName.erase( calleeName.find("2delete") ,7);
					Function* newcall = op->getModule()->getFunction( newname );	//search and retrieve the real kernel function to call
					errs() << "host-sig: will call " <<  newcall->getName() <<"\n";

					IRBuilder<> builder(&(*BI));
					///
					///
					// Function::ArgumentListType &args = f -> getArgumentList();
					std::vector<Value*> newargs;
                    for (int ii=0; ii<numArgs; ii++)
						newargs.push_back( op ->getArgOperand(ii)  );

					Value* str = builder.CreateGlobalStringPtr("TODO");//TODO
					Value* tmp = builder.CreatePointerCast(str, Type::getInt8PtrTy(C) );
					newargs.push_back( tmp ); 
				//	newargs.push_back( tmp ); 
			//		newargs.push_back( tmp );//yes, I need three pointers 

				//	for(int ii=0; ii < newargs.size(); ii++)
				//		errs() << ii  << " " << *(newargs[ii]) << "\n";
					Value *toshow = builder.CreateCall( newcall, newargs);
					toshow ->dump();
		
				//	errs() << "about to remove the instruction below : \n";
             			//	op ->dump();
					op ->eraseFromParent();
					BI = BB->begin(); //very important to avoid seg fault/core dump
				} 
				else if( (calleeName.find("cudaRegisterFunction") != std::string::npos)  )
                                {
				////////// register the new kernel 
					// and also remove the old kernel
                                //	op ->dump();
                                //	errs() <<  *(op->getArgOperand(1)) << "\n";
					Value* tmp =  op->getArgOperand(1);
					User* bc_op = (User*) tmp; //the second argument is a BitCastOperator

					Value* func = bc_op->getOperand(0); //the beginning operand is the function we are looking for
					//func ->dump();
					std::string fname = ((Function*)func) ->getName().str();
					std::string newfname = fname.erase( fname.find("2delete",7) );
					Function* newF = op->getModule()->getFunction( newfname );	//search and retrieve the real kernel function to call
					IRBuilder<> builder(&(*BI));
					Value* newcall = builder.CreatePointerCast( newF , Type::getInt8PtrTy(C))  ;
					op ->setArgOperand(1, newcall);

					//if ( auto *bc = dyn_cast<BitCastOperator>( bc_op ) )  //	errs() << "yes, this is BitCastOperator\n";
				}
			}
		}
	
           	return true;
        }


		virtual bool runOnBasicBlock73(Function::iterator &BB)
		{
			LLVMContext &C = BB->getContext();
			Function* bb_func = BB->getParent();
			StringRef fname = bb_func -> getName();
			Value* ONE = ConstantInt::get(Type::getInt32Ty(C), 1);

			for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
			{

				if ( auto *op = dyn_cast<InvokeInst>(BI) )
				{
					continue;//DEBUG
							if (! op->getCalledFunction() ) //this check is important for: %call =  call deferenceble bla bla...
									continue;
							StringRef callee = op->getCalledFunction() -> getName();
							std::string calleeName = callee.str();

			IRBuilder<> builder(&(*BI));
			if  ( calleeName.find("dim3")!= std::string::npos )
			{
				op->dump();
						//	int numArgs =  op->getNumArgOperands();
			//	op->getArgOperand(1) -> dump();
			//	op->getArgOperand(2) -> dump();
			//	op->getArgOperand(3) -> dump();

				builder.CreateCall(hookGridDims, { op->getArgOperand(1), op->getArgOperand(2), op->getArgOperand(3)});
			}

			bool yeskernel = false;
			for (int ii=0; ii<KNcnt; ii++)
				if ( calleeName.find(CUDAKN[ii])!= std::string::npos )
				{
					yeskernel = true;
					continue;
				}
			if (!yeskernel)
				continue;	//look at only CUDA kernel calls

			//Value* totalThreads = builder.CreateCall(hookGridReset, {1});
			Value* size4stack = builder.CreateCall(hookGridGet, {} );
	//		Value* size4stackHeight = builder.CreateCall(hookGridGet, builder.getInt32(2) );
			builder.CreateCall(hookGridReset, {});

			Function* fmalloc = BI->getModule()->getFunction("malloc");
			assert( fmalloc && "malllc() not exists!!\n");
			Function* fcudamalloc = BI->getModule()->getFunction("cudaMalloc");
			assert( fcudamalloc && "cudaMalloc() not exists!!\n");

		//	buffer_stack = builder.CreateAlloca( Type::getInt8PtrTy(C), NULL /*buffer for call stack*/);
					//	builder.CreateCall( fcudamalloc, { buffer_stack, size4stack} );
		//	buffer_stackHeight = builder.CreateAlloca( Type::getInt8PtrTy(C), NULL /*buffer for call stack height*/);
					//	builder.CreateCall( fcudamalloc, { buffer_stackHeight, size4stackHeight} );

			if (!buffer_h)
				buffer_h = builder.CreateCall( fmalloc, builder.getInt64(BUFFERSIZE) );

			buffer_d = builder.CreateAlloca( Type::getInt8PtrTy(C), nullptr /*, ptrhead*/);
							Value* ptr2 = builder.CreateCall( fcudamalloc, { buffer_d, builder.getInt64(BUFFERSIZE)} );

			///////
			///////
			int num = cast<CallBase>(op)->arg_size();;
			Type* loadedType = buffer_d->getType()->getPointerElementType();
			Value* li_d = builder.CreateLoad(loadedType, buffer_d);
		//	Value* li_stack = builder.CreateLoad(buffer_stack);
		//	Value* li_stackHeight = builder.CreateLoad(buffer_stackHeight);
			Value* li_h = builder.CreateLoad(loadedType, buffer_h);
			op -> setArgOperand(num-1, li_d);
		//	op -> setArgOperand(num-1, li_stack);
		//	op -> setArgOperand(num-1, li_stackHeight);
			
			///////
			///////

			continue;
			builder.SetInsertPoint( &(*BB), ++builder.GetInsertPoint() ); // insert after op
			Function* ffree = BI->getModule()->getFunction("free");
			Function* fcudafree = BI->getModule()->getFunction("cudaFree");
			Function* fcudaSync = BI->getModule()->getFunction("cudaDeviceSynchronize");
			Function* fcudaMemcpy = BI->getModule()->getFunction("cudaMemcpy");
			assert(ffree && "some function not found 1");
			assert(fcudafree && "some function not found 1.1");
			assert(fcudaSync && "some function not found 1.2");
			assert(fcudaMemcpy && "some function not found 1.3");

			builder.CreateCall( fcudaSync);

			///////
			///////
			Value* args[] = { buffer_h, li_d, builder.getInt64(BUFFERSIZE), builder.getInt32(2) };
			builder.CreateCall( fcudaMemcpy, args );
			Function* fGetHandle = BI->getModule()->getFunction("getHandle");
			Function* fAppendTrace = BI->getModule()->getFunction("appendTrace");
			assert( fGetHandle && fAppendTrace && "some function not found 2");

			Value* p2trace = builder.CreateCall(fGetHandle, builder.getInt32(2));	
			builder.CreateCall(fAppendTrace, {p2trace, buffer_h});				

			///////
			///////
			builder.CreateCall( fcudaSync);
			builder.CreateCall( ffree, buffer_h );
			buffer_h = NULL;
			builder.CreateCall( fcudafree, li_d);
		//	builder.CreateCall( fcudafree, li_stack);
		//	builder.CreateCall( fcudafree, li_stackHeight);
		}

					if ( auto *op = dyn_cast<CallInst>(BI) )
					{
							if (! op->getCalledFunction() ) //this check is important for: %call =  call deferenceble bla bla...
									continue;
							StringRef callee = op->getCalledFunction() -> getName();
							std::string calleeName = callee.str();

			IRBuilder<> builder(&(*BI));
			if  ( calleeName.find("dim3")!= std::string::npos )
			{
				op->dump();
						//	int numArgs =  op->getNumArgOperands();
			//	op->getArgOperand(1) -> dump();
			//	op->getArgOperand(2) -> dump();
			//	op->getArgOperand(3) -> dump();

				builder.CreateCall(hookGridDims, { op->getArgOperand(1), op->getArgOperand(2), op->getArgOperand(3)});
			}

			bool yeskernel = false;
			for (int ii=0; ii<KNcnt; ii++)
				if ( calleeName.find(CUDAKN[ii])!= std::string::npos )
				{
					yeskernel = true;
					continue;
				}
			if (!yeskernel)
				continue;	//look at only CUDA kernel calls

			//Value* totalThreads = builder.CreateCall(hookGridReset, {1});
			Value* size4stack = builder.CreateCall(hookGridGet, {} );
	//		Value* size4stackHeight = builder.CreateCall(hookGridGet, builder.getInt32(2) );
			builder.CreateCall(hookGridReset, {});

			Function* fmalloc = BI->getModule()->getFunction("malloc");
			assert( fmalloc && "malllc() not exists!!\n");
			Function* fcudamalloc = BI->getModule()->getFunction("cudaMalloc");
			assert( fcudamalloc && "cudaMalloc() not exists!!\n");

		//	buffer_stack = builder.CreateAlloca( Type::getInt8PtrTy(C), NULL /*buffer for call stack*/);
					//	builder.CreateCall( fcudamalloc, { buffer_stack, size4stack} );
		//	buffer_stackHeight = builder.CreateAlloca( Type::getInt8PtrTy(C), NULL /*buffer for call stack height*/);
					//	builder.CreateCall( fcudamalloc, { buffer_stackHeight, size4stackHeight} );

			if (!buffer_h)
				buffer_h = builder.CreateCall( fmalloc, builder.getInt64(BUFFERSIZE) );

			buffer_d = builder.CreateAlloca( Type::getInt8PtrTy(C), nullptr /*, ptrhead*/);
							Value* ptr2 = builder.CreateCall( fcudamalloc, { buffer_d, builder.getInt64(BUFFERSIZE)} );

			///////
			///////
			int num = cast<CallBase>(op)->arg_size();
			
			Type* loadedType = buffer_d->getType()->getPointerElementType();

			Value* li_d = builder.CreateLoad(loadedType, buffer_d);
		//	Value* li_stack = builder.CreateLoad(buffer_stack);
		//	Value* li_stackHeight = builder.CreateLoad(buffer_stackHeight);
			Value* li_h = builder.CreateLoad(loadedType, buffer_h);
			op -> setArgOperand(num-1, li_d);
		//	op -> setArgOperand(num-1, li_stack);
		//	op -> setArgOperand(num-1, li_stackHeight);
			
			///////
			///////
			builder.SetInsertPoint( &(*BB), ++builder.GetInsertPoint() ); // insert after op
			Function* ffree = BI->getModule()->getFunction("free");
			Function* fcudafree = BI->getModule()->getFunction("cudaFree");
			Function* fcudaSync = BI->getModule()->getFunction("cudaDeviceSynchronize");
			Function* fcudaMemcpy = BI->getModule()->getFunction("cudaMemcpy");
			assert(ffree && "some function not found 1");
			assert(fcudafree && "some function not found 1.1");
			assert(fcudaSync && "some function not found 1.2");
			assert(fcudaMemcpy && "some function not found 1.3");

			builder.CreateCall( fcudaSync);

			///////
			///////
			Value* args[] = { buffer_h, li_d, builder.getInt64(BUFFERSIZE), builder.getInt32(2) };
			builder.CreateCall( fcudaMemcpy, args );
			Function* fGetHandle = BI->getModule()->getFunction("getHandle");
			Function* fAppendTrace = BI->getModule()->getFunction("appendTrace");
			assert( fGetHandle && fAppendTrace && "some function not found 2");

			Value* p2trace = builder.CreateCall(fGetHandle, builder.getInt32(2));	
			builder.CreateCall(fAppendTrace, {p2trace, buffer_h});				

			///////
			///////
			builder.CreateCall( fcudaSync);
			builder.CreateCall( ffree, buffer_h );
			buffer_h = NULL;
			builder.CreateCall( fcudafree, li_d);
		//	builder.CreateCall( fcudafree, li_stack);
		//	builder.CreateCall( fcudafree, li_stackHeight);
			continue;
		}
	}	
	
		return true;
	}


    }; //end of pass



/// pass:  GPU pass
// eighth pass
// 8th pass
// this is path is for kernel initialization and return.
    struct instru_kernel_basic : public ModulePass{
        static char ID;   
		FunctionCallee hookRet;
		FunctionCallee hookInit;
		bool flag1;
		Value* str_fname;
		Value* ptr_fname;
		Type* VoidTy;
		Value* p_stackzone; 

        instru_kernel_basic() : ModulePass(ID) {}

        virtual bool runOnModule(Module &M)
        {

			LLVMContext &C = M.getContext();
			VoidTy = Type::getVoidTy(C); 

			errs() << "\n ======== 8th pass: GPU kernel basic:  initialization and return. =============== \n\n";

			NamedMDNode* MDannotatios = M.getNamedMetadata("nvvm.annotations");
			//MDannotatios -> dump(); errs() << "\n";

			for(unsigned i=0; i<MDannotatios->getNumOperands(); i++)
			{//auto detect GPU kernels
				MDNode* nn = MDannotatios->getOperand(i);
				////////////////errs()<<" I have operands " <<  nn->getNumOperands() << "\n" ;
				for(unsigned j=0; j<nn->getNumOperands(); j++)
				{
					if(! nn->getOperand(j))
						continue;

					//MDOperand  op = ( nn->getOperand(j));		
					Metadata*  op = ( nn->getOperand(j));
					//errs() << *op  << "\n";
					if( auto* str = dyn_cast<MDString>(nn->getOperand(j) ) )
					{
						std::string ss = str->getString().str();
						if (ss=="kernel")
						{
							errs() << "I found a CUDA kernel:\t";
							if( auto* v = dyn_cast<ValueAsMetadata>(nn->getOperand(j-1) ) )
								if (auto* F = dyn_cast<Function> (v->getValue())  )
								{
									errs() << F->getName() << "\n";
									StringRef fk = F->getName();
									std::string fkname = fk.str();
									CUDAKN[KNcnt].assign(fkname);
									KNcnt++;
								}
						}
					}
				}
			}

////	
            errs() << "\nLinking Analysis Functions:\n\n";

			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
			{	// it is all about mangling
				StringRef fn = F->getName();
						std::string fname = fn.str();
			//	errs() << fname << "\n";
				
				int ii;
				for (ii=0; ii<AFsize; ii++)
				if ( fname.find(AF[ii]) != std::string::npos ) 
				{	
				//	errs() << AF[ii] << "  is changed to "  ;
					AF[ii].assign(fname);
				//	errs() << AF[ii] <<  "\n";
				}
			}

			FunctionCallee hookFuncRet;
			FunctionCallee hookFuncInit;

			std::vector<Type*> ArgTypes1 = {Type::getInt8PtrTy(C)};
			FunctionType* FuncType1 = FunctionType::get(VoidTy, ArgTypes1, false);
			hookFuncRet = M.getOrInsertFunction(AF[5], FuncType1);
			
			FunctionType* FuncType2 = FunctionType::get(Type::getInt8PtrTy(C), ArgTypes1, false);
			hookFuncInit = M.getOrInsertFunction("InitKernel", FuncType2);

			hookRet = hookFuncRet;
			hookInit = hookFuncInit;

			errs() << "\nInstrument:\n\n";
			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
			{
				bool native = true;
				std::string fname = F->getName().str();
			//	errs() << fname << "\n";
				int ii;
				for (ii=0; ii<AFsize; ii++)
					if ( fname.find(AF[ii]) != std::string::npos )
					{
						native = false;
								//	errs() << "found !" << fname << " \n";
					}

				if (!native)
				{
			//		errs() << "skipped. "<< "\n\n";
					continue;
				}

				flag1 = false;			
				for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
					instru_kernel_basic::runOnBasicBlock82(BB);

				for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
					instru_kernel_basic::runOnBasicBlock81(BB);
			//	errs() <<  "next\n";
			}

			return false;
        }
// 
        virtual bool runOnBasicBlock81(Function::iterator &BB)
        { 
			//instrument at kernel return;
        	LLVMContext &C = BB->getContext();

			Function* bb_func = BB->getParent();
			StringRef f_name = bb_func->getName();	

			IRBuilder<> bbbuilder(&(*BB));
			if(!flag1)
			{
				str_fname = bbbuilder.CreateGlobalStringPtr(f_name);
				ptr_fname = bbbuilder.CreatePointerCast(str_fname, Type::getInt8PtrTy(C) );
				flag1 = true;
			}

	    	for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
            {
				if ( auto *op = dyn_cast<ReturnInst>( &(*BI) ) )
				{
				//	errs() << " [[[\nI am ReturnInst !  :    "  << *BI <<  "\n";
				//	errs() << op->getFunction() -> getName().str();

					const DebugLoc &loc = BI->getDebugLoc();

					StringRef fn = op->getFunction() -> getName();
					std::string fname = fn.str();
					//const char* ffname;
					//ffname = fname.c_str();

					IRBuilder<> builder(op); //IMPORTANT: you can't insert a call after a call, you will get dead loop
		
					bool yeskernel=false;
					for (int ii=0; ii<KNcnt; ii++)
					{
					//	errs() << CUDAKN[ii] << " and  " << fname << "\n";
						if ( fname.find(CUDAKN[ii])!= std::string::npos)
							yeskernel=true;
					}	
					
					// if ( fname.find("axpy_kernel")!= std::string::npos )
					if (yeskernel)
					{
						errs() << "location : "<< fn.str() << "\n";
						op -> dump();
						CallInst* tmpI = builder.CreateCall(hookRet, {p_stackzone});
						
						if (loc)
							tmpI->setDebugLoc(loc);
					}
					
					// errs() << " ]]]\n\n" ;
					continue;
				}

            }

            return true;
		}

        virtual bool runOnBasicBlock82(Function::iterator &BB)
        {
        	LLVMContext &C = BB->getContext();

			Function* bb_func = BB->getParent();
			StringRef f_name = bb_func->getName();	

			Instruction* inst = BB->getFirstNonPHI();
			const DebugLoc &loc = inst->getDebugLoc();
			std::string bb_name = BB->getName().str();
			IRBuilder<> builder(inst);

			if(!flag1)
			{
				str_fname = builder.CreateGlobalStringPtr(f_name);
				ptr_fname = builder.CreatePointerCast(str_fname, Type::getInt8PtrTy(C) );
				flag1 = true;
			}

		//insert intialization into Kernel's entry block		
	       	bool yeskernel=false;
			for (int ii=0; ii<KNcnt; ii++)
			{
				if ( f_name.str().find(CUDAKN[ii])!= std::string::npos)
					yeskernel=true;
				//continue;
			}
			if (yeskernel && bb_name == "entry" ) //insert intialization into Kernel's entry block
			{
				errs() << "location : "<< f_name.str() << ". in entry block\n";

				// Function::ArgumentListType &args = bb_func -> getArgumentList();

				int ii=0; //the next 10 lines are very iffy
				Value* arg1, *arg2, *arg3;
	/*                        for (auto &a :  args )
							{
									if ( ii==args.size()-3 )
											arg1 = (Value*) &a;
									else if ( ii==args.size()-2 )
											arg2 = (Value*) &a;
									else if ( ii==args.size()-1 )
											arg3 = (Value*) &a;
									ii++;
							}
	*/
				for (auto &a :  bb_func->args() )
				{
					if ( ii==bb_func->arg_size()-1 )
						arg1 = (Value*) &a;
				//	else if ( ii==args.size()-1 )
							//        	arg2 = (Value*) &a;
					ii++;
				}
				
				CallInst* tmpI = builder.CreateCall(hookInit, {arg1});
				p_stackzone = (Value*)tmpI;
				//CallInst* tmpI = builder.CreateCall(hookInit, {arg1, arg2});
				//CallInst* tmpI = builder.CreateCall(hookInit, {arg1, arg2, arg3});

				if(loc)
					tmpI->setDebugLoc(loc);
				else
					for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
					{
						const DebugLoc &loc2 = BI->getDebugLoc();
						if (loc2)
						{
							tmpI->setDebugLoc(loc2);
							break;
						}
					}

			} // end of insertion

		return true;
        }
    };	//end of pass


/// pass:  host pass
// nineth pass
// 9th pass
// host-side time measurement
    struct instru_host_measure : public ModulePass{
        static char ID;   
        FunctionCallee hook1;
        FunctionCallee hook2;
        FunctionCallee hook3;
        FunctionCallee hook4;
        FunctionCallee hook5;
		FunctionCallee hookMain;
		FunctionCallee hookString;
		FunctionCallee hookRet;
		FunctionCallee hookGridDims;
		FunctionCallee hookGridReset;
		FunctionCallee hookGridGet;
		bool flag1;
		Value* str_fname;
		Value* ptr_fname;
		Type* VoidTy;
		int offset = 0; //this is offset for cudaSetupArgument()
		int prevSize = 0; //this is size for cudaSetupArgument()
		Value* buffer_h = NULL;
		Value* buffer_d = NULL;
		Value* buffer_stack = NULL; //for global stack on GPU
	//	Value* buffer_stackHeight = NULL; //for global stack height on GPU
		

        instru_host_measure() : ModulePass(ID) {}

        virtual bool runOnModule(Module &M)
        {

			LLVMContext &C = M.getContext();
			VoidTy = Type::getVoidTy(C); 

			errs() << "\n ======== 7th pass; host side time mesurement =============== \n\n";

			FunctionCallee hookFuncCudaSync;
			FunctionCallee hookFuncMeasure;

			std::vector<Type*> ArgTypes1 = {Type::getInt32Ty(C)};
			FunctionType* FuncTy1 = FunctionType::get(VoidTy, ArgTypes1, false);
			hookFuncCudaSync = M.getOrInsertFunction("cudaDeviceSynchronize", FuncTy1);
			hookFuncMeasure = M.getOrInsertFunction("measureKernel", FuncTy1);

	//// auto-detect CUDA kernels on host side 
			Function* fcrf = M.getFunction("__cuda_register_globals");
			if (fcrf) //just in case
			{
				//errs() << " I am in __cuda_register_globals function"; 
				//errs() << "\n";
				errs() << " I found these CUDA kernel functions: "; 
				errs() << "\n";
				for(Function::iterator BB = fcrf->begin(), E = fcrf->end(); BB != E; ++BB)
					for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
						if ( auto* ci = dyn_cast<CallInst>(BI) )
						{
							Function* callee = ci->getCalledFunction();
							std::string calleeName = callee->getName().str();
							if ( calleeName.find("cudaRegisterFunction") != std::string::npos )
							{
								Value* arg1 =  ci->getArgOperand(1);

								User* bc_op = (User*) arg1; //the second argument is a BitCastOperator
								Value* fkernel = bc_op->getOperand(0); //the beginning operand is the function we are looking for
								std::string fname = ((Function*)fkernel) ->getName().str();
								
								errs() << fname << "\n";
								
								
								CUDAKN[KNcnt].assign(fname);
								KNcnt++;
							}
						}
			}

	////	
			errs() << "\nLinking Analysis Functions:\n\n";
			
			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
			{	// it is all about mangling
				StringRef fn = F->getName();
				std::string fname = fn.str();
			//	errs() << fname << "\n";
				
				int ii;
				for (ii=0; ii<AFsize; ii++)
				if ( fname.find(AF[ii]) != std::string::npos ) 
				{	
					errs() << AF[ii] << " is changed to "  ;
					AF[ii].assign(fname);
					errs() << AF[ii] <<  "\n";
				}
			}


			errs() << "\nInstrument:\n\n";
			for(Module::iterator F = M.begin(), E = M.end(); F!= E; ++F)
			{
				bool yeskernel = false;
				std::string fname = F->getName().str();
				for (int ii=0; ii<KNcnt; ii++)
					if ( fname.find(CUDAKN[ii])!= std::string::npos && fname.find("2delete") == std::string::npos)
					{
						yeskernel = true;
						break;
					}

				if (!yeskernel) 
				{	
					for(Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
						instru_host_measure::runOnBasicBlock72(BB); // this is for non-kernel only
				}
			}

			////////

			errs() << " done host_time_measure pass.\n\n" ;
        }
// 

        virtual bool runOnBasicBlock72(Function::iterator &BB)
        {
        	LLVMContext &C = BB->getContext();

			Function* bb_func = BB->getParent();
			StringRef fname = bb_func -> getName();
			std::string bbname = BB->getName().str();

	    	for(BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; ++BI)
            {
				if ( auto *op = dyn_cast<CallInst>( &(*BI) ) )
				{
					Function* f = op->getCalledFunction();
					if (!f) //this check is important for: %call =  call deferenceble bla bla...
						continue;	

					StringRef callee = op->getCalledFunction() -> getName();
					std::string calleeName = callee.str();

					bool yeskernel = false;
					for (int ii=0; ii<KNcnt; ii++)
						if ( calleeName.find(CUDAKN[ii])!= std::string::npos )
						{
								yeskernel = true;
								continue;
						}

					if (!yeskernel)
							continue;       //look at only CUDA kernel calls

					Function* hookMeasure = BI->getModule()->getFunction( "measureKernel");
					Function* hookSync = BI->getModule()->getFunction("cudaDeviceSynchronize");
					assert ( hookMeasure != NULL);
					assert ( hookSync != NULL);

					IRBuilder<> builder(&(*BI));
					builder.CreateCall( hookSync, {} );
					builder.CreateCall( hookMeasure, builder.getInt32(1) );

					builder.SetInsertPoint( &(*BB), ++builder.GetInsertPoint() ); // insert after op
					builder.CreateCall( hookSync, {} );
					builder.CreateCall( hookMeasure, builder.getInt32(2) );
			
				}
			}
	
           	return true;
        }


	}; //end of pass

	struct MemoryBandwidthPass : public FunctionPass {
		static char ID;
		FunctionCallee hookStartTimer, hookEndTimer, hookRecordAccess;

		MemoryBandwidthPass() : FunctionPass(ID) {}


		bool overlap(int start, int end, int lineStart, int lineEnd){

			if (start > end || lineStart > lineEnd)
				return false; // Invalid range

			if (start > lineEnd || end < lineStart) {
				return false; // No overlap
			}

			return true; // Overlap exists
		}

		void findInstructionsToInstrument(Function *F, std::vector<Instruction*> &instructionsToInstrument, int lineStart, int lineEnd) {	

			std::unordered_map<Instruction*, int> LineStart; // Min line number (predecessor)
			std::unordered_map<Instruction*, int> LineEnd;   // Max line number (successor)
			std::unordered_set<Instruction*> InWorklist;     // Fast O(1) check for presence
			std::queue<Instruction*> Worklist;               // FIFO queue for processing


			// For getting LineStart

			for (Instruction &I : instructions(*F)) {
				if (DILocation *Loc = I.getDebugLoc()) {
					LineStart[&I] = Loc->getLine();
				} else {
					LineStart[&I] = INT_MAX;
					Worklist.push(&I);
					InWorklist.insert(&I);
				}
			}

			while (!Worklist.empty()) {
				Instruction *I = Worklist.front();
				Worklist.pop();
				InWorklist.erase(I);
			
				int oldMin = LineStart[I];
				int newMin = oldMin;
			
				// If not the first instruction, take the previous instruction's line
				if (Instruction *Prev = I->getPrevNode()) {
					newMin = std::min(newMin, LineStart[Prev]);
				} else {
					// First instruction in the block: check predecessors' terminators
					for (BasicBlock *Pred : predecessors(I->getParent())) {
						Instruction *Term = Pred->getTerminator();
						newMin = std::min(newMin, LineStart[Term]);
					}
				}
			
				// If the minimum line changed, propagate to successors
				if (newMin < oldMin) {
					LineStart[I] = newMin;
			
					// Successors:
					if (Instruction *Next = I->getNextNode()) {
						if (InWorklist.insert(Next).second) {
							Worklist.push(Next);
						}
					} else {
						// If it's the last in the block, push the first of each successor
						for (BasicBlock *Succ : successors(I->getParent())) {
							Instruction *First = &Succ->front();
							if (InWorklist.insert(First).second) {
								Worklist.push(First);
							}
						}
					}
				}
			}
			

			// For getting LineEnd

			for (Instruction &I : instructions(*F)) {
				if (DILocation *Loc = I.getDebugLoc()) {
					LineEnd[&I] = Loc->getLine();
				} else {
					LineEnd[&I] = -1;
					Worklist.push(&I);
					InWorklist.insert(&I);
				}
			}

			while (!Worklist.empty()) {
				Instruction *I = Worklist.front();
				Worklist.pop();
				InWorklist.erase(I);
			
				int oldMax = LineEnd[I];
				int newMax = oldMax;
			
				// If not the last instruction, take the next instruction's line
				if (Instruction *Next = I->getNextNode()) {
					newMax = std::max(newMax, LineEnd[Next]);
				} else {
					// Last instruction in the block: check successors' first instructions
					for (BasicBlock *Succ : successors(I->getParent())) {
						Instruction *First = &Succ->front();
						newMax = std::max(newMax, LineEnd[First]);
					}
				}
			
				// If the maximum line changed, propagate to predecessors
				if (newMax > oldMax) {
					LineEnd[I] = newMax;
			
					// Predecessors:
					if (Instruction *Prev = I->getPrevNode()) {
						if (InWorklist.insert(Prev).second) {
							Worklist.push(Prev);
						}
					} else {
						// If it's the first in the block, push all predecessors' terminators
						for (BasicBlock *Pred : predecessors(I->getParent())) {
							Instruction *Term = Pred->getTerminator();
							if (InWorklist.insert(Term).second) {
								Worklist.push(Term);
							}
						}
					}
				}
			}
			
			
			for (Instruction &I : instructions(*F)) {
				if (!I.getDebugLoc()) {
					int start = LineStart[&I];
					int end = LineEnd[&I];
					if (start != INT_MAX && end != -1) {

						outs() << "Instruction: " << I << " → Line Range: [" << start << ", " << end << "]\n";

						if (overlap(start, end, lineStart, lineEnd)) {
							instructionsToInstrument.push_back(&I);
							outs() << "Instruction to instrument: " << I << "\n";
						}

					}
					else {
						outs() << "Instruction Range Not found: " << I << " → Line Range: [" << start << ", " << end << "]\n";
					}
				}
				else {
					DILocation *Loc = I.getDebugLoc();
					int lineNumber = Loc->getLine();

					if (lineNumber >= lineStart && lineNumber <= lineEnd) {
						instructionsToInstrument.push_back(&I);
						outs() << "Instruction to instrument: " << I << "\n";
					}
					
				}
			}
			

			return;
		}


		bool runOnFunction(Function &F) override {

			int lineStart = -1, lineEnd = -1;

			if (!LineRange.empty()) {
				size_t dashPos = LineRange.find('-');
				if (dashPos != std::string::npos) {
					lineStart = std::stoi(LineRange.substr(0, dashPos));
					lineEnd = std::stoi(LineRange.substr(dashPos + 1));
				} else {
					errs() << "Invalid line-range format. Expected format: start-end\n";
					return false;
				}
			}

			std::cout << "Line range: " << lineStart << " to " << lineEnd << std::endl;
			
			
			errs() << FunctionName << "\n";
			
			
			errs() << "Instrumenting: " << F.getName() << "\n"; // Debug output
			if (F.getName().find(FunctionName) == std::string::npos) // Skip host functions
			return false;
			
			

			errs() << "Entered to device instrumentation: " << F.getName() << "\n"; // Debug output

			
			LLVMContext &Ctx = F.getContext();
			Module *M = F.getParent();

			// Declare instrumentation functions
			Type *VoidTy = Type::getVoidTy(Ctx);
			Type *Int64Ty = Type::getInt64Ty(Ctx);

			// Hook to record memory access (bytes, time_ns)
			FunctionType *RecordAccessTy = FunctionType::get(VoidTy, {Int64Ty, Int64Ty}, false);
			hookRecordAccess = M->getOrInsertFunction("recordMemAccess", RecordAccessTy);

			// Hook to get current GPU timestamp
			FunctionType *GetTimeTy = FunctionType::get(Int64Ty, {}, false);
			hookStartTimer = M->getOrInsertFunction("getGpuTime", GetTimeTy);

			std::vector<Instruction*> instructionsToInstrument;

			findInstructionsToInstrument(&F, instructionsToInstrument, lineStart, lineEnd);


			for (auto I: instructionsToInstrument) {

				if (auto *LI = dyn_cast<LoadInst>(I)) {
					printf("Hello I am Load\n");
					instrumentMemoryOp(LI, /*isLoad=*/true);
				} else if (auto *SI = dyn_cast<StoreInst>(I)) {
					printf("Hello I am Store\n");
					instrumentMemoryOp(SI, /*isLoad=*/false);
				}
			}

			return true;
		}

		void instrumentMemoryOp(Instruction *MemOp, bool isLoad) {
			IRBuilder<> Builder(MemOp);
			LLVMContext &Ctx = MemOp->getContext();
		
			// Try to get debug location from the original instruction
			DebugLoc DbgLoc = MemOp->getDebugLoc();
			if (!DbgLoc) {
				// If no debug info, create a synthetic debug location or inherit from function
				if (DISubprogram *SP = MemOp->getFunction()->getSubprogram()) {
					DbgLoc = DILocation::get(Ctx, 0, 0, SP);
				} else {
					DbgLoc = DebugLoc();
				}
			}
			Builder.SetCurrentDebugLocation(DbgLoc);
		
			Type *DataType = isLoad ? MemOp->getType()
									: cast<StoreInst>(MemOp)->getValueOperand()->getType();
			uint64_t bytesAccessed = DataType->getPrimitiveSizeInBits() / 8;
			
			// Get string representation of the data type
			std::string TypeStr;
			raw_string_ostream RSO(TypeStr);
			DataType->print(RSO);

			// Convert the full instruction to a string
			std::string InstStr;
			raw_string_ostream InstOS(InstStr);
			MemOp->print(InstOS);  // 'I' is the Instruction*
			
			// Get instruction opcode name
			std::string InstType = MemOp->getOpcodeName();  // e.g., "load" or "store"
			
			// Print debug info
			outs() << "Instruction: " << InstOS.str()
				<< ", Type: " << RSO.str()
				<< ", Bytes Accessed: " << bytesAccessed << "\n";
			

		
			// Insert timing before memory op
			CallInst *StartTimeCall = Builder.CreateCall(hookStartTimer, {});
			StartTimeCall->setDebugLoc(DbgLoc);
		
			// Insert timing after memory op
			Builder.SetInsertPoint(MemOp->getNextNode());
			CallInst *EndTimeCall = Builder.CreateCall(hookStartTimer, {});
			EndTimeCall->setDebugLoc(DbgLoc);
		
			// Compute duration (EndTime - StartTime)
			Value *Duration = Builder.CreateSub(EndTimeCall, StartTimeCall);
		
			// Record access (bytes, time)
			CallInst *RecordCall = Builder.CreateCall(hookRecordAccess, {
				Builder.getInt64(bytesAccessed),
				Duration
			});
			RecordCall->setDebugLoc(DbgLoc);
		}
		};

	struct ComputeIntensityPass : public FunctionPass {
		static char ID;
		FunctionCallee hookRecordFlops, hookRecordBytes;

		ComputeIntensityPass() : FunctionPass(ID) {}

		bool overlap(int start, int end, int lineStart, int lineEnd){

			if (start > end || lineStart > lineEnd)
				return false; // Invalid range

			if (start > lineEnd || end < lineStart) {
				return false; // No overlap
			}

			return true; // Overlap exists
		}

		void findInstructionsToInstrument(Function *F, std::vector<Instruction*> &instructionsToInstrument, int lineStart, int lineEnd) {	

			std::unordered_map<Instruction*, int> LineStart; // Min line number (predecessor)
			std::unordered_map<Instruction*, int> LineEnd;   // Max line number (successor)
			std::unordered_set<Instruction*> InWorklist;     // Fast O(1) check for presence
			std::queue<Instruction*> Worklist;               // FIFO queue for processing


			// For getting LineStart

			for (Instruction &I : instructions(*F)) {
				if (DILocation *Loc = I.getDebugLoc()) {
					LineStart[&I] = Loc->getLine();
				} else {
					LineStart[&I] = INT_MAX;
					Worklist.push(&I);
					InWorklist.insert(&I);
				}
			}

			while (!Worklist.empty()) {
				Instruction *I = Worklist.front();
				Worklist.pop();
				InWorklist.erase(I);
			
				int oldMin = LineStart[I];
				int newMin = oldMin;
			
				// If not the first instruction, take the previous instruction's line
				if (Instruction *Prev = I->getPrevNode()) {
					newMin = std::min(newMin, LineStart[Prev]);
				} else {
					// First instruction in the block: check predecessors' terminators
					for (BasicBlock *Pred : predecessors(I->getParent())) {
						Instruction *Term = Pred->getTerminator();
						newMin = std::min(newMin, LineStart[Term]);
					}
				}
			
				// If the minimum line changed, propagate to successors
				if (newMin < oldMin) {
					LineStart[I] = newMin;
			
					// Successors:
					if (Instruction *Next = I->getNextNode()) {
						if (InWorklist.insert(Next).second) {
							Worklist.push(Next);
						}
					} else {
						// If it's the last in the block, push the first of each successor
						for (BasicBlock *Succ : successors(I->getParent())) {
							Instruction *First = &Succ->front();
							if (InWorklist.insert(First).second) {
								Worklist.push(First);
							}
						}
					}
				}
			}
			

			// For getting LineEnd

			for (Instruction &I : instructions(*F)) {
				if (DILocation *Loc = I.getDebugLoc()) {
					LineEnd[&I] = Loc->getLine();
				} else {
					LineEnd[&I] = -1;
					Worklist.push(&I);
					InWorklist.insert(&I);
				}
			}

			while (!Worklist.empty()) {
				Instruction *I = Worklist.front();
				Worklist.pop();
				InWorklist.erase(I);
			
				int oldMax = LineEnd[I];
				int newMax = oldMax;
			
				// If not the last instruction, take the next instruction's line
				if (Instruction *Next = I->getNextNode()) {
					newMax = std::max(newMax, LineEnd[Next]);
				} else {
					// Last instruction in the block: check successors' first instructions
					for (BasicBlock *Succ : successors(I->getParent())) {
						Instruction *First = &Succ->front();
						newMax = std::max(newMax, LineEnd[First]);
					}
				}
			
				// If the maximum line changed, propagate to predecessors
				if (newMax > oldMax) {
					LineEnd[I] = newMax;
			
					// Predecessors:
					if (Instruction *Prev = I->getPrevNode()) {
						if (InWorklist.insert(Prev).second) {
							Worklist.push(Prev);
						}
					} else {
						// If it's the first in the block, push all predecessors' terminators
						for (BasicBlock *Pred : predecessors(I->getParent())) {
							Instruction *Term = Pred->getTerminator();
							if (InWorklist.insert(Term).second) {
								Worklist.push(Term);
							}
						}
					}
				}
			}
			
			
			for (Instruction &I : instructions(*F)) {
				if (!I.getDebugLoc()) {
					int start = LineStart[&I];
					int end = LineEnd[&I];
					if (start != INT_MAX && end != -1) {

						outs() << "Instruction: " << I << " → Line Range: [" << start << ", " << end << "]\n";

						if (overlap(start, end, lineStart, lineEnd)) {
							instructionsToInstrument.push_back(&I);
							outs() << "Instruction to instrument: " << I << "\n";
						}

					}
					else {
						outs() << "Instruction Range Not found: " << I << " → Line Range: [" << start << ", " << end << "]\n";
					}
				}
				else {
					DILocation *Loc = I.getDebugLoc();
					int lineNumber = Loc->getLine();

					if (lineNumber >= lineStart && lineNumber <= lineEnd) {
						instructionsToInstrument.push_back(&I);
						outs() << "Instruction to instrument: " << I << "\n";
					}
					
				}
			}
			

			return;
		}


		bool runOnFunction(Function &F) override {

			int lineStart = -1, lineEnd = -1;

			if (!LineRange.empty()) {
				size_t dashPos = LineRange.find('-');
				if (dashPos != std::string::npos) {
					lineStart = std::stoi(LineRange.substr(0, dashPos));
					lineEnd = std::stoi(LineRange.substr(dashPos + 1));
				} else {
					errs() << "Invalid line-range format. Expected format: start-end\n";
					return false;
				}
			}

			std::cout << "Line range: " << lineStart << " to " << lineEnd << std::endl;


			errs() << "Instrumenting: " << F.getName() << "\n";
			
			if (!FunctionName.empty() && F.getName().find(FunctionName) == std::string::npos) {
				return false;
			}

			

			LLVMContext &Ctx = F.getContext();
			Module *M = F.getParent();

			// Initialize hooks
			Type *VoidTy = Type::getVoidTy(Ctx);
			Type *Int64Ty = Type::getInt64Ty(Ctx);

			// Hook for recording individual FLOPs
			hookRecordFlops = M->getOrInsertFunction(
				"recordFlop",
				FunctionType::get(VoidTy, {Int64Ty}, false)
			);

			// Hook for recording memory accesses
			hookRecordBytes = M->getOrInsertFunction(
				"recordBytesAccess",
				FunctionType::get(VoidTy, {Int64Ty}, false)
			);

			// Mark hooks as noinline
			if (Function *HookFn = dyn_cast<Function>(hookRecordFlops.getCallee())) {
				HookFn->addFnAttr(Attribute::NoInline);
			}
			if (Function *HookFn = dyn_cast<Function>(hookRecordBytes.getCallee())) {
				HookFn->addFnAttr(Attribute::NoInline);
			}

			const DataLayout &DL = M->getDataLayout();

			std::vector<Instruction*> instructionsToInstrument;
			findInstructionsToInstrument(&F, instructionsToInstrument, lineStart, lineEnd);


			// Instrument all relevant operations
			for (auto I: instructionsToInstrument) {
				// Instrument floating point operations
				if (I->isBinaryOp()) {
					switch (I->getOpcode()) {
						case Instruction::FAdd:
						case Instruction::FSub:
						case Instruction::FMul:
						case Instruction::FDiv:
						case Instruction::FRem:
							instrumentFlop(I);
							break;
						default:
							break;
					}
				}

				// Instrument memory accesses
				if (auto *LI = dyn_cast<LoadInst>(I)) {
					instrumentMemoryAccess(LI, true, DL);
				} else if (auto *SI = dyn_cast<StoreInst>(I)) {
					instrumentMemoryAccess(SI, false, DL);
				}
			}

			return true;
		}

		void instrumentFlop(Instruction *FlopInst) {
			IRBuilder<> Builder(FlopInst);
			LLVMContext &Ctx = FlopInst->getContext();

			// Handle debug information
			DebugLoc DbgLoc = FlopInst->getDebugLoc();
			if (!DbgLoc) {
				if (DISubprogram *SP = FlopInst->getFunction()->getSubprogram()) {
					DbgLoc = DILocation::get(Ctx, 0, 0, SP);
				}
			}
			Builder.SetCurrentDebugLocation(DbgLoc);

			// Record 1 FLOP for this operation
			CallInst *CI = Builder.CreateCall(hookRecordFlops, {
				Builder.getInt64(1)
			});
			CI->setDebugLoc(DbgLoc);
		}

		void instrumentMemoryAccess(Instruction *MemOp, bool isLoad, const DataLayout &DL) {
			IRBuilder<> Builder(MemOp);
			LLVMContext &Ctx = MemOp->getContext();

			// Handle debug information
			DebugLoc DbgLoc = MemOp->getDebugLoc();
			if (!DbgLoc) {
				if (DISubprogram *SP = MemOp->getFunction()->getSubprogram()) {
					DbgLoc = DILocation::get(Ctx, 0, 0, SP);
				}
			}
			Builder.SetCurrentDebugLocation(DbgLoc);

			// Calculate bytes accessed
			Type *DataType = isLoad ? MemOp->getType()
									: cast<StoreInst>(MemOp)->getValueOperand()->getType();
			uint64_t bytesAccessed = DataType->getPrimitiveSizeInBits() / 8;

			// Record memory access
			CallInst *CI = Builder.CreateCall(hookRecordBytes, {
				Builder.getInt64(bytesAccessed)
			});
			CI->setDebugLoc(DbgLoc);
		}	

	};

}

char ComputeIntensityPass::ID = 0;
static RegisterPass<ComputeIntensityPass> CI("compute-intensity", "Measures FLOPs/memory acceses in bytes",false,false);

char MemoryBandwidthPass::ID = 0;
static RegisterPass<MemoryBandwidthPass> MM("memory-bandwidth", "Measures GPU memory bandwidth per access",false,false);

char our_pass::ID = 0;
static RegisterPass<our_pass> Z("our-pass", "Line Range Instrumentation", false, false);

char my_rand::ID = 0;
static RegisterPass<my_rand> Y("my-rand", "instrument at beginning of some functions hello world!!", false, false);

char instru_host::ID = 0;
static RegisterPass<instru_host> X("instru-host", "load/store and call path instrumentation", false, false);

char instru_kernel_memory::ID = 0;
static RegisterPass<instru_kernel_memory> XX("instru-kernel-memory", "CUDA kernel memory instrumentation", false, false);

// char instru_globalvar::ID = 0;
// static RegisterPass<instru_globalvar> XXX("instru-global-var", "Deprecaded: global variables instrumentation on host", false, false);

char instru_kernel_branch::ID = 0;
static RegisterPass<instru_kernel_branch> XXXX("instru-kernel-branch", "CUDA kernel branch instrumentation", false, false);

char instru_kernel_callpath::ID = 0;
static RegisterPass<instru_kernel_callpath> XXXXX("instru-kernel-call-path", "CUDA kernel call path instrumentation", false, false);

char instru_kernel_sig::ID = 0;
static RegisterPass<instru_kernel_sig> XXXXXX("instru-kernel-sig", "CUDA kernel signature modification", false, false);

char instru_host_sig::ID = 0;
static RegisterPass<instru_host_sig> XXXXXXX("instru-host-sig", "host side signature modification", false, false);

char instru_kernel_basic::ID = 0;
static RegisterPass<instru_kernel_basic> XXYXXX("instru-kernel-basic", "CUDA kernel init and return", false, false);

char instru_host_measure::ID = 0;
static RegisterPass<instru_host_measure> XXYYXXX("instru-host-measure", "host side time measurement", false, false);

