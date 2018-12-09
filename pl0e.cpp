#include <cctype>
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <string>
#include <memory>
#include <vector>
#include <map>
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TargetRegistry.h"
#include "pl0e-jit.h"
#define PAUSE printf("press ENTER key to continue...\n");getchar();while(getchar() != '\n');
//+---------------+
//	Helper
//+---------------+
//namespace
using namespace llvm;
using namespace llvm::orc;

//lexer
static int CurTok;
static double NumVal;
static std::string IdentName;
static std::vector<int> Tokens; //collect Tokens
static std::string SourceText; //save source file text
static unsigned LineNum; 
enum Token
{
	tok_eof = -1 , tok_num = -2 , tok_ident = -3,

	//block
	tok_int = -11 , tok_double = -12 , tok_void = -13 ,
	tok_proc = -14 , 
	
	//stat
	tok_call = -21 , tok_begin = -22 , tok_end = -23 ,
	tok_if = -24 , tok_then = -25 , tok_else = -26 ,
	tok_while = -27 , tok_do = -28 , tok_ret =-29 ,

	//op
	//+   -   *   /   %
	//==  !=  >
	//>=  <   <=
	//&&  ||
	tok_add = -31 , tok_sub = -32 , tok_mul = -33 , tok_div = -40 , tok_rem = -43 ,
	tok_equ = -34 , tok_neq = -35 , tok_gth = -36 ,
	tok_geq = -37 , tok_lth = -38 , tok_leq = -39 ,
        tok_and = -41 , tok_or  = -42  
};
static int getToken();
static void getNextToken()
{
	CurTok=getToken();
	Tokens.push_back(CurTok);
}
int getNextChar()
{
	int ch;

	ch = getchar();

	SourceText += ch;

	return ch;
}
//class declaration
class ExprAST;
class VarExprAST;
class VariableAST;
class BlockAST;
class PrototypeAST;
class StatAST;
class CallAST;
class FunctionAST;

//operator precedence
static int getTokenPrecedence()
{
	switch(CurTok)
	{
		//logical
		case tok_or:
		case tok_and:return 5;
		//comparation
		case tok_equ:
		case tok_neq:
		case tok_gth:
		case tok_geq:
		case tok_lth:
		case tok_leq:return 10;
		//arithmetic
		case tok_add:
		case tok_sub:return 20;
		case tok_rem:return 25;//remainder
		case tok_mul:
		case tok_div:return 30;
		//others
		default:return -1;
	}
}

//code generation helper
static LLVMContext TheContext;
static IRBuilder<> Builder(TheContext);
static std::unique_ptr<Module> TheModule;
static std::map<std::string,AllocaInst*> LocalVarAddr;	//local var addr 
static std::map<std::string,AllocaInst*> ArgVarAddr;	//arg var addr 
static BasicBlock* RetBB;
static bool hasRetStat;

Type* Int32Type = Type::getInt32Ty(TheContext);
Type* Int32PtrType = Type::getInt32PtrTy(TheContext);
Type* DoubleType = Type::getDoubleTy(TheContext);
Type* DoublePtrType = Type::getDoublePtrTy(TheContext);
Type* VoidType = Type::getVoidTy(TheContext);

extern "C" void getf(double* var)
{
	scanf("%lf",var);
}
extern "C" void geti(int* var)
{
	scanf("%d",var);
}
extern "C" void putf(double val)
{
	printf("%lf",val);
}
extern "C" void puti(int val)
{
	printf("%d",val);
}
extern "C" void putch(char c)
{
	printf("%c",c);
}
extern "C" int getrem(int a,int b)
{
	return a%b;
}
static void logError(const char*,char);
//log error helper
static void logError(const char* err,char kind)
{
	if(kind == 'p')
	{
		printf("%-70s %s\n","parser error info","line num");
		printf("%-70s %s\n","-----------------","--------");
		printf("%-70s %d\n\n",err,LineNum);
	}
	if(kind == 'c')
	{
		printf("%s\n","codegen error info");
		printf("%s\n","------------------");
		printf("%s\n\n",err);
	}
	if(kind == 'g')
	{
		printf("%s\n","generic error info");
		printf("%s\n","------------------");
		printf("%s\n\n",err);
	}
}
static std::unique_ptr<ExprAST> logErrorE(const char* err)
{
	logError(err,'p');
	return nullptr;
}
static std::unique_ptr<VarExprAST> logErrorVE(const char* err)
{
	logError(err,'p');
	return nullptr;
}
static std::unique_ptr<BlockAST> logErrorB(const char* err)
{
	logError(err,'p');
	return nullptr;
}
static std::unique_ptr<StatAST> logErrorS(const char* err)
{
	logError(err,'p');
	return nullptr;
}
static std::unique_ptr<CallAST> logErrorC(const char* err)
{
	logError(err,'p');
	return nullptr;
}
static std::unique_ptr<FunctionAST> logErrorF(const char* err)
{
	logError(err,'p');
	return nullptr;
}
static std::unique_ptr<VariableAST> logErrorVar(const char* err)
{
	logError(err,'p');
	return nullptr;
}
static PrototypeAST* logErrorProto(const char* err)
{
	logError(err,'p');
	return nullptr;
}
static Value* logErrorV(const char* err)
{
	logError(err,'c');
	return nullptr;
}
static Function* logErrorFunc(const char* err)
{
	logError(err,'c');
	return nullptr;
}
static AllocaInst* logErrorA(const char* err)
{
	logError(err,'c');
	return nullptr;
}
static bool logErrorL(const char* err)
{
	logError(err,'c');
	return false;
}
//JIT support
std::unique_ptr<Pl0JIT> JIT;

//AST
enum BasicType
{
	IntT = 1 , DoubleT = 2 , VoidT =3 ,
};
struct VarType
{
	BasicType BT;
	std::vector<int> DimensionInfo;
};
static std::vector<int> RecPosition;	//record positions that need to be print
static int CurPosition = 0;		//current print position
void printRecord()
{
	int index=0;
	for(unsigned i=0 ; i<RecPosition.size() ; i++)
	{
		for( ; index<CurPosition ; index++)
		{
			if(index == RecPosition[i])
			{
				fprintf(stderr,"|");
				index++;
				break;
			}
			fprintf(stderr," ");
		}
	}
	while(index++<CurPosition)
		fprintf(stderr," ");

}
void printBasicType(int BT)
{
	switch(BT)
	{
		case IntT:
			fprintf(stderr," (int)\n");
			break;
		case DoubleT:
			fprintf(stderr," (double)\n");
			break;
		case VoidT:
			fprintf(stderr," (void)\n");
			break;
		default:
			fprintf(stderr," (undefined)\n");
			return;
	}
}
void printVarType(VarType VT)
{
	if(VT.DimensionInfo.size() == 0)
		printBasicType(VT.BT);
	else
	{
		std::string BTName;
		switch(VT.BT)
		{
		case IntT:
			BTName = "int";
			break;
		case DoubleT:
			BTName = "double";
			break;
		default:
			BTName = "undefined";
			break;
		}
		//save ']' nums
		int n = 0;
		
		fprintf(stderr," ");
		for(unsigned i = 0 ; i < VT.DimensionInfo.size() ; i++)
		{
			if(i == VT.DimensionInfo.size()-1)
				fprintf(stderr,"[%d x %s]",VT.DimensionInfo[i],BTName.c_str());
			else
			{
				n++;
				fprintf(stderr,"[%d x ",VT.DimensionInfo[i]);
			}
		}
		//print ']'
		while(n-- > 0)
			fprintf(stderr,"]");

		fprintf(stderr,"\n");
	}
}
void printOperator(int Op)
{
	switch(Op)
	{
	case tok_and:fprintf(stderr,"&&");break;
	case tok_or:fprintf(stderr,"||");break;

	case tok_add:fprintf(stderr,"+");break;
	case tok_sub:fprintf(stderr,"-");break;
	case tok_rem:fprintf(stderr,"%c",'%');break;
	case tok_mul:fprintf(stderr,"*");break;
	case tok_div:fprintf(stderr,"/");break;
	
	case tok_equ:fprintf(stderr,"==");break;
	case tok_neq:fprintf(stderr,"!=");break;
	case tok_gth:fprintf(stderr,">");break;
	case tok_geq:fprintf(stderr,">=");break;
	case tok_lth:fprintf(stderr,"<");break;
	case tok_leq:fprintf(stderr,"<=");break;

	default:fprintf(stderr,"err");break;
	}
}
//parser helper
static bool isTypeToken()
{
	switch(CurTok)
	{
		case tok_int:
		case tok_double:
		case tok_void:
			return true;
		default:
			return false;
	}
}
static bool isOpToken()
{
	switch(CurTok)
	{
	//logical
	case tok_or:
	case tok_and:
	//arithmetic
	case tok_add:
	case tok_sub:
	case tok_rem:
	case tok_mul:
	case tok_div:
	//comparation
	case tok_equ:
	case tok_neq:
	case tok_gth:
	case tok_geq:
	case tok_lth:
	case tok_leq:return true;
	//others
	default:return false;
	}
}
struct ParserSymbolTable
{
	std::map<std::string,VarType*> GVT;	//global 
	std::map<std::string,VarType*> LVT;	//local
	std::map<std::string,VarType*> AVT;	//argument
	//below two used for call checking
	std::vector<PrototypeAST*> AvailableFuncs;
	PrototypeAST* CurrentFunc;
};
static ParserSymbolTable PST;

//support function
static void addIOSupportFunction()
{
	FunctionType* FT;
	Function* F;
	//getf
	FT = FunctionType::get(Type::getVoidTy(TheContext),DoublePtrType,false);
	F = Function::Create(FT,Function::ExternalLinkage,"getf",TheModule.get());
	if(!F)
		return logError("Fail to create getf at addIOSupportFunction()!",'g');
	//geti
	FT = FunctionType::get(Type::getVoidTy(TheContext),Int32PtrType,false);
	F = Function::Create(FT,Function::ExternalLinkage,"geti",TheModule.get());
	if(!F)
		return logError("Fail to create geti at addIOSupportFunction()!",'g');
	//putf
	FT = FunctionType::get(Type::getVoidTy(TheContext),DoubleType,false);
	F = Function::Create(FT,Function::ExternalLinkage,"putf",TheModule.get());
	if(!F)
		return logError("Fail to create putf at addIOSupportFunction()!",'g');
	//puti
	FT = FunctionType::get(Type::getVoidTy(TheContext),Int32Type,false);
	F = Function::Create(FT,Function::ExternalLinkage,"puti",TheModule.get());
	if(!F)
		return logError("Fail to create puti at addIOSupportFunction()!",'g');
	//putch
	FT = FunctionType::get(Type::getVoidTy(TheContext),Type::getInt8Ty(TheContext),false);
	F = Function::Create(FT,Function::ExternalLinkage,"putch",TheModule.get());
	if(!F)
		return logError("Fail to create putch at addIOSupportFunction()!",'g');
}
static void addMathSupportFunction();
static void addOpSupportFunction()
{
	//getrem
	FunctionType* FT;
	Function* F;

	std::vector<Type*> Types(2,Int32Type);
	FT = FunctionType::get(Int32Type,Types,false);
	F = Function::Create(FT,Function::ExternalLinkage,"getrem",TheModule.get());
	if(!F)
		return logError("Fail to create getrem at addOpSupportFunction()!",'g');
}
//initialization
static int getToken();
static void initialization()
{
	InitializeNativeTarget();
	InitializeNativeTargetAsmPrinter();
	InitializeNativeTargetAsmParser();

	TheModule = llvm::make_unique<Module>("llvm module",TheContext);

	addIOSupportFunction();
	addOpSupportFunction();
	addMathSupportFunction();

	LineNum = 1;

	hasRetStat = false;

	getNextToken();
}

//+----------------+
//	Lexer
//+----------------+
static int getToken()
{
	static int CurChar=' ';
	
	while(isspace(CurChar))
	{
		if(CurChar == '\n')
			LineNum++;
		CurChar=getNextChar();
	}
	
	//<re> num = [-] digit (digit | '.')*
	if(isdigit(CurChar) || CurChar == '-')
	{
		if(CurChar == '-')
		{
			if(CurTok==tok_num || CurTok==tok_ident || CurTok == ')' || CurTok == ']' || CurTok == '\'')
			{
				CurChar=getNextChar();
				return tok_sub;
			}
		}

		std::string NumStr;
		NumStr+=CurChar;
		
		while(isdigit(CurChar=getNextChar()) || CurChar=='.')
			NumStr+=CurChar;

		NumVal=strtod(NumStr.c_str(),NULL);
		return tok_num;
	}

	//<re> ident = letter(letter | digit)*
	if(isalpha(CurChar))
	{
		IdentName=CurChar;
		
		while(isalnum(CurChar=getNextChar()))
			IdentName+=CurChar;
		
		//block
		if(IdentName=="PROCEDURE")
			return tok_proc;
		if(IdentName=="INT")
			return tok_int;
		if(IdentName=="DOUBLE")
			return tok_double;
		if(IdentName=="VOID")
			return tok_void;
		//stat
		if(IdentName=="CALL")
			return tok_call;
		if(IdentName=="RET")
			return tok_ret;
		if(IdentName=="BEGIN")
			return tok_begin;
		if(IdentName=="END")
			return tok_end;
		if(IdentName=="IF")
			return tok_if;
		if(IdentName=="THEN")
			return tok_then;
		if(IdentName=="ELSE")
			return tok_else;
		if(IdentName=="WHILE")
			return tok_while;
		if(IdentName=="DO")
			return tok_do;
	
		return tok_ident;
	}

	if(CurChar == '+')
	{
		CurChar=getNextChar();
		return tok_add;
	}
	if(CurChar == '%')
	{
		CurChar=getNextChar();
		return tok_rem;
	}
	if(CurChar == '*')
	{
		CurChar=getNextChar();
		return tok_mul;
	}
	if(CurChar == '/')
	{
		CurChar=getNextChar();
		return tok_div;
	}

	if(CurChar == '=')
	{
		CurChar=getNextChar();
		if(CurChar == '=')
		{
			CurChar=getNextChar();
			return tok_equ;
		}
		return '=';
	}	
	if(CurChar == '!')
	{
		CurChar=getNextChar();
		if(CurChar == '=')
		{
			CurChar=getNextChar();
			return tok_neq;
		}
		return '!';
	}	
	if(CurChar == '>')
	{
		CurChar=getNextChar();
		if(CurChar == '=')
		{
			CurChar=getNextChar();
			return tok_geq;
		}
		return tok_gth;
	}	
	if(CurChar == '<')
	{
		CurChar=getNextChar();
		if(CurChar == '=')
		{
			CurChar=getNextChar();
			return tok_leq;
		}
		return tok_lth;
	}	
	if(CurChar == '&')
	{
		CurChar=getNextChar();
		if(CurChar == '&')
		{
			CurChar=getNextChar();
			return tok_and;
		}
		return '&';
	}
	if(CurChar == '|')
	{
		CurChar=getNextChar();
		if(CurChar == '|')
		{
			CurChar=getNextChar();
			return tok_or;
		}
		return '|';
	}

	if(CurChar=='#')
	{
		do
		{
			CurChar=getNextChar();
		}while(CurChar!='\n' && CurChar!=EOF);
		
		if(CurChar=='\n')
			return getToken();
	}

	if(CurChar==EOF)
		return tok_eof;

	int temp=CurChar;
	CurChar=getNextChar();
	return temp;
}
//+------------+
//	AST
//+------------+
//expression AST
class ExprAST
{
private:
	int BT;
public:
	ExprAST(int BT = -1):BT(BT){}
	virtual ~ExprAST()=default;
	virtual Value* exprCodegen()=0;
	virtual void printAST()=0;
	int getType(){return BT;}
	void setType(int DestType){BT=DestType;}
};
template <typename T>
class NumExprAST:public ExprAST
{
private:
	T Val;
public:
	NumExprAST(int BT,T Val):ExprAST(BT),Val(Val){}	
	virtual Value* exprCodegen();
	virtual void printAST();
};
class VarExprAST:public ExprAST
{
private:
	std::string Name;
	std::vector<std::unique_ptr<ExprAST>> Indexs;
public:
	VarExprAST(int BT,std::string Name,std::vector<std::unique_ptr<ExprAST>> Indexs):ExprAST(BT),Name(Name),Indexs(std::move(Indexs)){}
	virtual Value* exprCodegen();
	Value* exprCodegenPtr();
	virtual void printAST();
	std::string getName(){return Name;}
};
class ParenExprAST:public ExprAST
{
private:
	std::unique_ptr<ExprAST> Val;
public:
	ParenExprAST(int BT,std::unique_ptr<ExprAST> Val):ExprAST(BT),Val(std::move(Val)){}
	virtual Value* exprCodegen();
	virtual void printAST();
};
class BinExprAST:public ExprAST
{
private:
	int Op;
	std::unique_ptr<ExprAST> LHS,RHS;
public:
	BinExprAST(int BT,int Op,std::unique_ptr<ExprAST> LHS,std::unique_ptr<ExprAST> RHS):ExprAST(BT),Op(Op),LHS(std::move(LHS)),RHS(std::move(RHS)){}
	virtual Value* exprCodegen();
	virtual void printAST();
	int getOp(){return Op;}
};
//statement AST
class StatAST
{
public:
	virtual ~StatAST()=default;
	virtual bool statCodegen()=0;
	virtual void printAST()=0;
};
class AssignStatAST:public StatAST
{
private:
	std::unique_ptr<VarExprAST> Var;
	std::unique_ptr<ExprAST> Val;
public:
	AssignStatAST(std::unique_ptr<VarExprAST> Var,std::unique_ptr<ExprAST> Val):Var(std::move(Var)),Val(std::move(Val)){}
	virtual bool statCodegen();
	virtual void printAST();
};
class CallAST:public StatAST,public ExprAST
{
private:
	std::string Callee;
	std::vector<std::unique_ptr<ExprAST>> Args;
public:
	CallAST(BasicType BT,std::string Callee,std::vector<std::unique_ptr<ExprAST>> Args):ExprAST(BT),Callee(Callee),Args(std::move(Args)){}
	virtual bool statCodegen();
	virtual Value* exprCodegen();
	virtual void printAST();
};
class RetStatAST:public StatAST
{
private:
	std::unique_ptr<ExprAST> Val;
public:
	RetStatAST(std::unique_ptr<ExprAST> Val):Val(std::move(Val)){}
	virtual bool statCodegen();
	virtual void printAST();
};
class IfStatAST:public StatAST
{
private:
	std::unique_ptr<ExprAST> CondExpr;
	std::unique_ptr<StatAST> ThenStat,ElseStat;
public:
	IfStatAST(std::unique_ptr<ExprAST> CondExpr,std::unique_ptr<StatAST> ThenStat,std::unique_ptr<StatAST> ElseStat = nullptr):CondExpr(std::move(CondExpr)),ThenStat(std::move(ThenStat)),ElseStat(std::move(ElseStat)){}
	virtual bool statCodegen();
	virtual void printAST();
};
class WhileStatAST:public StatAST
{
private:
	std::unique_ptr<ExprAST> Cond;
	std::unique_ptr<StatAST> Stat;
public:
	WhileStatAST(std::unique_ptr<ExprAST> Cond,std::unique_ptr<StatAST> Stat):Cond(std::move(Cond)),Stat(std::move(Stat)){}
	virtual bool statCodegen();
	virtual void printAST();
};
class InputStatAST:public StatAST
{
private:
	std::unique_ptr<VarExprAST> Var;
public:
	InputStatAST(std::unique_ptr<VarExprAST> Var):Var(std::move(Var)){}
	virtual bool statCodegen();
	virtual void printAST();
};
class OutputStatAST:public StatAST
{
private:
	std::unique_ptr<VarExprAST> Var;
	char Char;
public:
	OutputStatAST(std::unique_ptr<VarExprAST> Var,char Char):Var(std::move(Var)),Char(Char){}
	virtual bool statCodegen();
	virtual void printAST();
};
class MultiStatAST:public StatAST
{
private:
	std::vector<std::unique_ptr<StatAST>> Stats;
public:
	MultiStatAST(std::vector<std::unique_ptr<StatAST>> Stats):Stats(std::move(Stats)){}
	virtual bool statCodegen();
	virtual void printAST();
};
class PrototypeAST
{
private:
	BasicType RetType;
	std::string Name;
	std::vector<std::unique_ptr<VariableAST>> Args;
public:
	PrototypeAST(BasicType RetType,std::string Name,std::vector<std::unique_ptr<VariableAST>> Args):RetType(RetType),Name(Name),Args(std::move(Args)){}
	Function* codegen();
	void printAST();
	std::vector<std::unique_ptr<VariableAST>>& getArgs(){return Args;}
	BasicType getRetType(){return RetType;}
	std::string getName(){return Name;}
};
class FunctionAST
{
private:
	PrototypeAST* Proto;
	std::unique_ptr<BlockAST> Block;
public:
	FunctionAST(PrototypeAST* Proto,std::unique_ptr<BlockAST> Block):Proto(Proto),Block(std::move(Block)){}
	~FunctionAST(){delete Proto;}
	bool codegen();
	void printAST();
};
class VariableAST
{
private:
	std::string Name;
	VarType VT;
public:
	VariableAST(std::string Name,VarType VT):Name(Name),VT(VT){}
	AllocaInst* codegenLocal();
	AllocaInst* codegenArg();
	Value* codegenGlobal();
	void printAST();
	std::string getName(){return Name;}
	BasicType getBasicType(){return VT.BT;}
};
class BlockAST
{
private:
	std::vector<std::unique_ptr<VariableAST>> Vars;
	std::vector<std::unique_ptr<FunctionAST>> Funcs;
	std::unique_ptr<StatAST> Stat;
public:
	BlockAST(std::vector<std::unique_ptr<VariableAST>> Vars,std::vector<std::unique_ptr<FunctionAST>> Funcs,std::unique_ptr<StatAST> Stat):Vars(std::move(Vars)),Funcs(std::move(Funcs)),Stat(std::move(Stat)){}
	bool codegen();
	bool codegenP();	//codegen for program block that uses global var
	void printAST();
};
/* add math support function */
static void addMathSupportFunction()
{
	FunctionType* FT;
	Function* F;
	std::vector<std::unique_ptr<VariableAST>> Vars;
	VarType VT;
	//sqrt
	FT = FunctionType::get(DoubleType,DoubleType,false);
	F = Function::Create(FT,Function::ExternalLinkage,"sqrt",TheModule.get());
	if(!F)
		return logError("Fail to create sqrt at addMathSupportFunction()!",'g');
	//add to available funcs table
	VT.BT = DoubleT;
	Vars.push_back(llvm::make_unique<VariableAST>("",VT));
	PST.AvailableFuncs.push_back(new PrototypeAST(DoubleT,"sqrt",std::move(Vars)));	
}

//implement print AST
//expr
template <typename T>
void NumExprAST<T>::printAST()
{
	int BT = getType();
	switch(BT)
	{
		case IntT:
			if(Val<0)
				fprintf(stderr,"(%d)",(int)Val);
			else	
				fprintf(stderr,"%d",(int)Val);
			printBasicType(BT);
			break;
		case DoubleT:
			if(Val<0)
				fprintf(stderr,"(%lf)",(double)Val);
			else
				fprintf(stderr,"%lf",(double)Val);
			printBasicType(BT);
			break;
		default:
			fprintf(stderr,"undefined type val\n");
			return;
	}
}
void VarExprAST::printAST()
{
	fprintf(stderr,"%s",Name.c_str());
	printBasicType(getType());

	for(unsigned i=0; i<Indexs.size(); i++)
	{
		if(i == Indexs.size()-1)
		{
			printRecord();
			fprintf(stderr,"\\---[]\n");
			
			CurPosition += 4;
			printRecord();
			fprintf(stderr,"\\---");

			CurPosition += 4;
			Indexs[i]->printAST();

			CurPosition -= 4;

			CurPosition -= 4;
		}
		else
		{
			printRecord();
			fprintf(stderr,"+---[]\n");
			RecPosition.push_back(CurPosition);
			
			CurPosition += 4;
			printRecord();
			fprintf(stderr,"\\---");

			CurPosition += 4;
			Indexs[i]->printAST();

			CurPosition -= 4;

			CurPosition -= 4;
			RecPosition.pop_back();
		}
	}
}
void ParenExprAST::printAST()
{
	fprintf(stderr,"()");
	printBasicType(getType());

	printRecord();
	fprintf(stderr,"\\---");

	CurPosition+=4;	
	Val->printAST();

	CurPosition-=4;
}
void BinExprAST::printAST()
{
	fprintf(stderr,"\"");
	printOperator(getOp());
	fprintf(stderr,"\"");
	printBasicType(getType());

	printRecord();
	fprintf(stderr,"+---");
	RecPosition.push_back(CurPosition);

	CurPosition+=4;
	LHS->printAST();

	CurPosition-=4;
	RecPosition.pop_back();

	printRecord();
	fprintf(stderr,"|\n");

	printRecord();
	fprintf(stderr,"\\---");

	CurPosition+=4;
	RHS->printAST();

	CurPosition-=4;
}
//stat print AST
void InputStatAST::printAST()
{
	fprintf(stderr,"\"?\"\n");
	
	printRecord();
	fprintf(stderr,"\\---");

	CurPosition += 4;
	Var->printAST();

	CurPosition -= 4;
}
void OutputStatAST::printAST()
{
	fprintf(stderr,"\"!\"\n");
	
	printRecord();
	fprintf(stderr,"\\---");
	
	CurPosition += 4;
	if(Var)
		Var->printAST();
	else
		fprintf(stderr,"%d\n",(int)Char);

	CurPosition -= 4;
}
void CallAST::printAST()
{
	fprintf(stderr,"call\n");

	printRecord();
	fprintf(stderr,"+---callee\n");
	RecPosition.push_back(CurPosition);

	CurPosition += 4;
	printRecord();
	fprintf(stderr,"\\---%s",Callee.c_str());
	printBasicType(getType());

	RecPosition.pop_back();
	CurPosition -= 4;

	printRecord();
	fprintf(stderr,"\\---args\n");

	CurPosition += 4;

	if(Args.size() == 0)
	{
		printRecord();
		fprintf(stderr,"\\---void\n");
	}
	else
	{
		for(unsigned i = 0; i<Args.size(); i++)
		{
			if(i == Args.size()-1)
			{
				printRecord();
				fprintf(stderr,"\\---arg\n");	
				
				CurPosition += 4;
				printRecord();
				fprintf(stderr,"\\---");
	
				CurPosition += 4;
				Args[i]->printAST();
	
				CurPosition -= 4;
	
				CurPosition -= 4;
			}
			else
			{
				printRecord();
				fprintf(stderr,"+---arg\n");	
				RecPosition.push_back(CurPosition);
	
				CurPosition += 4;
				printRecord();
				fprintf(stderr,"\\---");
			
				CurPosition += 4;
				Args[i]->printAST();
	
				RecPosition.pop_back();
				CurPosition -= 4;
	
				CurPosition -= 4;
			}
		}
	}
	CurPosition -= 4;
}
void RetStatAST::printAST()
{
	fprintf(stderr,"ret\n");
	
	printRecord();
	fprintf(stderr,"\\---");

	if(Val)
	{
		CurPosition += 4;
		Val->printAST();
	
		CurPosition -= 4;
	}
	else
		fprintf(stderr,"VOID\n");
}
void AssignStatAST::printAST()
{
	fprintf(stderr,"\"=\"\n");

	printRecord();
	fprintf(stderr,"+---");
	RecPosition.push_back(CurPosition);

	CurPosition += 4;
	Var->printAST();

	CurPosition -= 4;
	RecPosition.pop_back();
	printRecord();
	fprintf(stderr,"|\n");

	printRecord();
	fprintf(stderr,"\\---");
	
	CurPosition+=4;
	Val->printAST();

	CurPosition-=4;
}
void IfStatAST::printAST()
{
	fprintf(stderr,"if\n");

	printRecord();
	fprintf(stderr,"+---cond\n");
	RecPosition.push_back(CurPosition);
	
	CurPosition += 4;
	printRecord();
	fprintf(stderr,"\\---");

	CurPosition += 4;
	CondExpr->printAST();
	CurPosition -= 4;

	CurPosition -= 4;
	RecPosition.pop_back();

	printRecord();
	if(ElseStat != nullptr)
	{	
		fprintf(stderr,"+---then\n");
		RecPosition.push_back(CurPosition);
		
		CurPosition += 4;
		printRecord();
		fprintf(stderr,"\\---stat\n");	

		CurPosition += 4;
		printRecord();
		fprintf(stderr,"\\---");

		CurPosition += 4;
		ThenStat->printAST();
	
		CurPosition -= 4;

		CurPosition -= 4;

		CurPosition -= 4;
		RecPosition.pop_back();
		
		printRecord();
		fprintf(stderr,"\\---else\n");

		CurPosition += 4;
		printRecord();
		fprintf(stderr,"\\---stat\n");

		CurPosition += 4;
		printRecord();
		fprintf(stderr,"\\---");

		CurPosition+=4;
		ElseStat->printAST();
		
		CurPosition -= 4;

		CurPosition -= 4;
		
		CurPosition -= 4;
	}
	else
	{	
		fprintf(stderr,"\\---then\n");
		
		CurPosition += 4;
		printRecord();
		fprintf(stderr,"\\---stat\n");

		CurPosition += 4;
		printRecord();
		fprintf(stderr,"\\---");

		CurPosition+=4;
		ThenStat->printAST();

		CurPosition -= 4;	

		CurPosition -= 4;	

		CurPosition -= 4;	
	}
}
void WhileStatAST::printAST()
{
	fprintf(stderr,"while\n");
	
	printRecord();
	fprintf(stderr,"+---cond\n");
	RecPosition.push_back(CurPosition);
	
	CurPosition += 4;
	printRecord();
	fprintf(stderr,"\\---");

	CurPosition += 4;
	Cond->printAST();

	CurPosition -= 4;

	CurPosition -= 4;
	RecPosition.pop_back();

	printRecord();
	fprintf(stderr,"\\---loop\n");

	CurPosition += 4;
	printRecord();
	fprintf(stderr,"\\---");

	CurPosition += 4;
	Stat->printAST();

	CurPosition -= 4;

	CurPosition -= 4;
}
void MultiStatAST::printAST()
{
	fprintf(stderr,"stats\n");

	for(unsigned int i=0;i<Stats.size();i++)
	{
		if(i == Stats.size()-1)
		{
			printRecord();
			fprintf(stderr,"\\---stat\n");
			
			CurPosition += 4;
			printRecord();
			fprintf(stderr,"\\---");

			CurPosition += 4;
			Stats[i]->printAST();

			CurPosition -= 4;

			CurPosition -= 4;
		}
		else
		{
			printRecord();
			fprintf(stderr,"+---stat\n");
			RecPosition.push_back(CurPosition);

			CurPosition += 4;
			printRecord();
			fprintf(stderr,"\\---");

			CurPosition += 4;
			Stats[i]->printAST();
			RecPosition.pop_back();

			CurPosition -= 4;

			CurPosition -= 4;
		}
	}
}
void PrototypeAST::printAST()
{
	switch(RetType)
	{
	case IntT:
		fprintf(stderr,"int ");
		break;
	case DoubleT:
		fprintf(stderr,"double ");
		break;
	case VoidT:
		fprintf(stderr,"void ");
		break;
	default:
		fprintf(stderr,"undefined ");
	}
	fprintf(stderr,"%s(",Name.c_str());
	for(unsigned i =0 ; i < Args.size() ; i++)
	{
		switch(Args[i]->getBasicType())
		{
		case IntT:
			fprintf(stderr,"int ");
			break;
		case DoubleT:
			fprintf(stderr,"double ");
			break;
		case VoidT:
			fprintf(stderr,"void ");
			break;
		default:
			fprintf(stderr,"undefined ");
		}
		if(i == Args.size()-1)
			fprintf(stderr,"%s",Args[i]->getName().c_str());
		else
			fprintf(stderr,"%s, ",Args[i]->getName().c_str());
	}
	fprintf(stderr,")\n");
}
void FunctionAST::printAST()
{
	printRecord();
	fprintf(stderr,"+---");
	Proto->printAST();

	printRecord();
	fprintf(stderr,"|\n");

	printRecord();
	fprintf(stderr,"\\---block\n");

	CurPosition+=4;
	Block->printAST();
	CurPosition-=4;
}
void VariableAST::printAST()
{
	fprintf(stderr,"%s",Name.c_str());
	printVarType(VT);
}
void BlockAST::printAST()
{
	if(Vars.size() > 0)
	{
		printRecord();
		fprintf(stderr,"+---vars\n");
		RecPosition.push_back(CurPosition);

		CurPosition += 4;
		for(unsigned i=0;i<Vars.size();i++)
		{
			if(i == Vars.size()-1)
			{
				printRecord();
				fprintf(stderr,"\\---");
				Vars[i]->printAST();
			}
			else
			{
				printRecord();
				fprintf(stderr,"+---");
				Vars[i]->printAST();

				printRecord();
				fprintf(stderr,"|\n");
			}
		}
		CurPosition -= 4;
		RecPosition.pop_back();
	}
	
	for(auto& Func:Funcs)
	{
		printRecord();
		fprintf(stderr,"+---proc\n");
		RecPosition.push_back(CurPosition);

		CurPosition+=4;
		Func->printAST();

		CurPosition-=4;
		RecPosition.pop_back();
	}
	
	printRecord();
	fprintf(stderr,"\\---stat\n");

	CurPosition+=4;
	printRecord();
	fprintf(stderr,"\\---");
	
	CurPosition += 4;
	Stat->printAST();

	CurPosition-=4;

	CurPosition-=4;
}

//implement codegen
//+-----------------+
//	codegen
//+-----------------+
//expr
template <typename T>
Value* NumExprAST<T>::exprCodegen()
{
	int BT = getType();
	switch(BT)
	{
		case IntT:
			return ConstantInt::get(Int32Type,(int)Val,true);
		case DoubleT:	
			return ConstantFP::get(TheContext,APFloat((double)Val));	
		default:
			break;
	}
	return logErrorV("Undefined val type at NumExprAST<T>::exprCodegen()!");
}
Value* VarExprAST::exprCodegen()
{
	//return var's value
	/*  can save codes if the LocalVarAddr table can be type:map<string,value*>  */
	//first,find it as arg var
	auto VA = ArgVarAddr[Name];
	//second,find it as local var
	if(!VA)
		VA = LocalVarAddr[Name];
	if(VA)
	{
		if(Indexs.size() == 0)
			return Builder.CreateLoad(VA,Name.c_str());
		else
		{
			std::vector<Value*> IndexList;
			IndexList.push_back(ConstantInt::get(Int32Type,0));

			for(auto& Index:Indexs)
			{
				if(Index->getType() != IntT)
					return logErrorV("Array index must be int type at VarExprAST::exprCodegen()!");

				auto Val = Index->exprCodegen();
				if(!Val)				
					return nullptr;
					//return logErrorV("Fail to exprCodegen for index at VarExprAST::exprCodegen()!\n");
				
				IndexList.push_back(Val);
			}
			auto ElementAddr = Builder.CreateInBoundsGEP(VA,IndexList,"gepinbounds");
			if(!ElementAddr)
				return logErrorV("Fail to get element addr at VarExprAST::exprCodegen()!");
			
			return Builder.CreateLoad(ElementAddr,Name.c_str());
		}
	}
			
	//third,find it as global var
	auto GVA = TheModule->getGlobalVariable(Name,true);
	if(!GVA)
		return logErrorV("Fail to read var addr at VarExprAST::exprCodegen()!");
	
	if(Indexs.size() == 0)
		return Builder.CreateLoad(GVA,Name.c_str());
	else
	{
		std::vector<Value*> IndexList;
		IndexList.push_back(ConstantInt::get(Int32Type,0));

		for(auto& Index:Indexs)
		{
			if(Index->getType() != IntT)
				return logErrorV("Array index must be int type at VarExprAST::exprCodegen()!");

			auto Val = Index->exprCodegen();
			if(!Val)
				return nullptr;				
				//return logErrorV("Fail to exprCodegen for index at VarExprAST::exprCodegen()!");
				
			IndexList.push_back(Val);
		}
		auto ElementAddr = Builder.CreateInBoundsGEP(GVA,IndexList,"gepinbounds");
		if(!ElementAddr)
			return logErrorV("Fail to get element addr at VarExprAST::exprCodegen()!");
			
		return Builder.CreateLoad(ElementAddr,Name.c_str());
	}
}
Value* VarExprAST::exprCodegenPtr()
{
	//return var's pointer
	auto VA = ArgVarAddr[Name];
	if(!VA)
		VA = LocalVarAddr[Name];

	if(VA)
	{
		if(Indexs.size() == 0)
			return VA;
		else
		{
			std::vector<Value*> IndexList;
			IndexList.push_back(ConstantInt::get(Int32Type,0));

			for(auto& Index:Indexs)
			{
				if(Index->getType() != IntT)
					return logErrorV("Array index must be int type at VarExprAST::exprCodegen()!");

				auto Val = Index->exprCodegen();
				if(!Val)
					return nullptr;				
					//return logErrorV("Fail to exprCodegen for index at VarExprAST::exprCodegen()!");
				
				IndexList.push_back(Val);
			}
			auto ElementAddr = Builder.CreateInBoundsGEP(VA,IndexList,"gepinbounds");
			if(!ElementAddr)
				return logErrorV("Fail to get element addr at VarExprAST::exprCodegen()!");
			
			return ElementAddr;
		}
	}
			
	//second,find it as global var
	auto GVA = TheModule->getGlobalVariable(Name,true);
	if(!GVA)
		return logErrorV("Fail to read var addr at VarExprAST::exprCodegen()!");
	
	if(Indexs.size() == 0)
		return GVA;
	else
	{
		std::vector<Value*> IndexList;
		IndexList.push_back(ConstantInt::get(Int32Type,0));

		for(auto& Index:Indexs)
		{
			if(Index->getType() != IntT)
				return logErrorV("Array index must be int type at VarExprAST::exprCodegen()!");

			auto Val = Index->exprCodegen();
			if(!Val)				
				return logErrorV("Fail to exprCodegen for index at VarExprAST::exprCodegen()!");
				
			IndexList.push_back(Val);
		}
		auto ElementAddr = Builder.CreateInBoundsGEP(GVA,IndexList,"gepinbounds");
		if(!ElementAddr)
			return logErrorV("Fail to get element addr at VarExprAST::exprCodegen()!");
			
		return ElementAddr;
	}
}
Value* ParenExprAST::exprCodegen()
{
	auto PV = Val->exprCodegen();
	if(!PV)
		return nullptr;
		//return logErrorV("Fail to exprCodegen for paren expr at ParenExprAST::exprCodegen()!");
	
	return PV;
}
Value* BinExprAST::exprCodegen()
{
	auto L=LHS->exprCodegen();
	auto R=RHS->exprCodegen();
	
	if(!L || !R)
		return nullptr;
		//return logErrorV("Fail to exprCodegen for LHS or RHS at BinExprAST::exprCodegen()!");
	
	//type cast if LHS and RHS have diff type
	int BT = this->getType();
	int LT = LHS->getType();
	int RT = RHS->getType();
	if(LT != BT || RT != BT)
	{
		//this node's expr type represent the final type,oprand that has diff type need to be cast to it
		switch(BT)
		{
			case DoubleT:
				if(LT != DoubleT)
				{
					if(LT == IntT)
						L = Builder.CreateSIToFP(L,DoubleType,"sitofp");
					else
						return logErrorV("Undefined lhs type at BinExprAST::exprCodegen()!");
				}
				if(RT != DoubleT)
				{
					if(RT == IntT)
						R = Builder.CreateSIToFP(R,DoubleType,"sitofp");
					else
						return logErrorV("Undefined rhs type at BinExprAST::exprCodegen()!");
				}
				break;
			case IntT:
				if(LT != IntT)
				{
					if(LT == DoubleT)
						L = Builder.CreateFPToSI(L,Int32Type,"fptosi");
					else
						return logErrorV("Undefined lhs type at BinExprAST::exprCodegen()!");
				}
				if(RT != IntT)
				{
					if(RT == DoubleT)
						R = Builder.CreateFPToSI(R,Int32Type,"fptosi");
					else
						return logErrorV("Undefined rhs type at BinExprAST::exprCodegen()!");
				}
				break;
			default:
				return logErrorV("Incorrect bin expr type at BinExprAST::exprCodegen()!");
		}
	}
	Value* Val;

	if(BT == DoubleT)
	{
		switch(Op)
		{
		case tok_and:	
		case tok_or:
			return logErrorV("Fail to create and/or for double oprand at BinExprAST::exprCodegen()!");

		case tok_add:	
			return Builder.CreateFAdd(L,R,"fadd");
		case tok_sub:
			return Builder.CreateFSub(L,R,"fsub");
		case tok_mul:		
			return Builder.CreateFMul(L,R,"fmul");
		case tok_div:
			return Builder.CreateFDiv(L,R,"fdiv");
		
		case tok_equ:
			Val = Builder.CreateFCmpUEQ(L,R,"fcmpeq");
			return Builder.CreateUIToFP(Val,DoubleType,"uitofp");
		case tok_neq:
			Val = Builder.CreateFCmpUNE(L,R,"fcmpne");
			return Builder.CreateUIToFP(Val,DoubleType,"uitofp");
		case tok_gth:
			Val = Builder.CreateFCmpUGT(L,R,"fcmpgt");
			return Builder.CreateUIToFP(Val,DoubleType,"uitofp");
		case tok_geq:
			Val = Builder.CreateFCmpUGE(L,R,"fcmpge");
			return Builder.CreateUIToFP(Val,DoubleType,"uitofp");
		case tok_lth:
			Val = Builder.CreateFCmpULT(L,R,"fcmplt");
			return Builder.CreateUIToFP(Val,DoubleType,"uitofp");
		case tok_leq:
			Val = Builder.CreateFCmpULE(L,R,"fcmple");
			return Builder.CreateUIToFP(Val,DoubleType,"uitofp");

		default:
			return logErrorV("Undefined bin op at BinExprAST::exprCodegen()!");
		}
	}
	Function* F;
	std::vector<Value*> Args;
	if(BT == IntT)
	{
		switch(Op)
		{
		//L,R icmp ne 
		//and/or
		//zext
		case tok_and:	
			L = Builder.CreateICmpNE(L,ConstantInt::get(Int32Type,0),"icmpne");
			R = Builder.CreateICmpNE(R,ConstantInt::get(Int32Type,0),"icmpne");
			Val = Builder.CreateAnd(L,R,"and");
			return Builder.CreateZExt(Val,Int32Type,"zext");
		case tok_or:
			L = Builder.CreateICmpNE(L,ConstantInt::get(Int32Type,0),"icmpne");
			R = Builder.CreateICmpNE(R,ConstantInt::get(Int32Type,0),"icmpne");
			Val = Builder.CreateOr(L,R,"or");
			return Builder.CreateZExt(Val,Int32Type,"zext");
		case tok_add:	
			return Builder.CreateAdd(L,R,"add");
		case tok_sub:
			return Builder.CreateSub(L,R,"sub");
		case tok_rem:
			F = TheModule->getFunction("getrem");
			if(!F)
				return logErrorV("Fail to get function getrem at BinExprAST::codegen()!");
			Args.push_back(L);
			Args.push_back(R);
			return Builder.CreateCall(F,Args,"getrem");
		case tok_mul:		
			return Builder.CreateMul(L,R,"mul");
		case tok_div:
			return Builder.CreateSDiv(L,R,"sdiv");
		
		case tok_equ:
			Val = Builder.CreateICmpEQ(L,R,"icmpeq");
			return Builder.CreateZExt(Val,Int32Type,"zext");
		case tok_neq:
			Val = Builder.CreateICmpNE(L,R,"icmpne");
			return Builder.CreateZExt(Val,Int32Type,"zext");
		case tok_gth:
			Val = Builder.CreateICmpSGT(L,R,"icmpgt");
			return Builder.CreateZExt(Val,Int32Type,"zext");
		case tok_geq:
			Val = Builder.CreateICmpSGE(L,R,"icmpge");
			return Builder.CreateZExt(Val,Int32Type,"zext");
		case tok_lth:
			Val = Builder.CreateICmpSLT(L,R,"icmplt");
			return Builder.CreateZExt(Val,Int32Type,"zext");
		case tok_leq:
			Val = Builder.CreateICmpSLE(L,R,"icmple");
			return Builder.CreateZExt(Val,Int32Type,"zext");

		default:
			return logErrorV("Undefined bin op at BinExprAST::codengen()!");
		}
	}
	return logErrorV("Undefined bin expr type at BinExprAST::exprCodegen()!");
}
Value* CallAST::exprCodegen()
{
	switch(getType())
	{
	case IntT:
	case DoubleT:
		break;
	default:
		return logErrorV("Incorrect call expr type at CallAST::exprCodegen()!");
	}

	auto F = TheModule->getFunction(Callee);
	if(!F)
		return logErrorV("Fail to get function at CallAST::exprCodegen()!");

	std::vector<Value*> AVs;
	for(auto& Arg:Args)
	{
		auto Val = Arg->exprCodegen();
		if(!Val)
			return nullptr;
			//return logErrorV("Fail to codegen for arg expr at CallAST::exprCodegen()!");

		//type cast if needed
		if(Val->getType() == Int32Type)
		{
			if(Arg->getType() != IntT)
			{
				switch(Arg->getType())
				{
				case DoubleT:
					Val = Builder.CreateSIToFP(Val,DoubleType,"sitofp");
					break;
				default:	
					return logErrorV("Undefined arg type at CallAST::exprCodegen()!");
				}	
			}
		}else if(Val->getType() == DoubleType)
		{
			if(Arg->getType() != DoubleT)
			{
				switch(Arg->getType())
				{
				case IntT:
					Val = Builder.CreateFPToSI(Val,Int32Type,"fptosi");
					break;
				default:	
					return logErrorV("Undefined arg type at CallAST::exprCodegen()!");
				}	
			}
		}else
			return logErrorV("Undefined LLVM:Value type at CallAST::exprCodegen()!");

		AVs.push_back(Val);
	}
	return	Builder.CreateCall(F,AVs,"call");
}
//statement codegen
bool AssignStatAST::statCodegen()
{
	auto LHS = Var->exprCodegenPtr();
	if(!LHS)
		return false;

	auto RHS = Val->exprCodegen();
	if(!RHS)
		return false;
	
	//type cast if LHS RHS has diff type
	int LT = Var->getType();
	int RT = Val->getType();
	if(LT != RT)
	{
		switch(LT)
		{
		case DoubleT:
			switch(RT)
			{
			case IntT:
				RHS = Builder.CreateSIToFP(RHS,DoubleType,"sitofp");
				break;
				
			default:
				return logErrorL("Undefined val type at AssignStatAST::statCodegen()!");			
			}
			break;			

		case IntT:
			switch(RT)
			{
			case DoubleT:
				RHS = Builder.CreateFPToSI(RHS,Int32Type,"fptosi");
				break;
				
			default:
				return logErrorL("Undefined val type at AssignStatAST::statCodegen()!");			
			}
			break;			

		default:
			return logErrorL("Undefined var type at AssignStatAST::statCodegen()!");
		}
	}	

	Builder.CreateStore(RHS,LHS);
	return true;
}
bool CallAST::statCodegen()
{
	auto F = TheModule->getFunction(Callee);
	if(!F)
		return logErrorL("Fail to get function at CallAST::statCodegen()!");

	std::vector<Value*> AVs;
	for(auto& Arg:Args)
	{
		auto Val = Arg->exprCodegen();
		if(!Val)
			return false;
			//return logErrorL("Fail to statCodegen for arg expr at CallAST::statCodegen()!");

		//type cast if needed
		if(Val->getType() == Int32Type)
		{
			if(Arg->getType() != IntT)
			{
				switch(Arg->getType())
				{
				case DoubleT:
					Val = Builder.CreateSIToFP(Val,DoubleType,"sitofp");
					break;
				default:	
					return logErrorV("Undefined arg type at CallAST::statCodegen()!");
				}	
			}
		}else if(Val->getType() == DoubleType)
		{
			if(Arg->getType() != DoubleT)
			{
				switch(Arg->getType())
				{
				case IntT:
					Val = Builder.CreateFPToSI(Val,Int32Type,"fptosi");
					break;
				default:	
					return logErrorV("Undefined arg type at CallAST::statCodegen()!");
				}	
			}
		}else
			return logErrorV("Undefined LLVM:Value type at CallAST::statCodegen()!");

		AVs.push_back(Val);
	}
	Builder.CreateCall(F,AVs);
	return true;
}
bool RetStatAST::statCodegen()
{
	//in the case "RET expr"
	if(Val)
	{
		auto V = Val->exprCodegen();
		if(!V)
			return false;
			//return logErrorL("Fail to codegen for ret val at RetStatAST::statCodegen()!");

		//check ret val type
		auto F = Builder.GetInsertBlock()->getParent();
		if(!F)
			return logErrorL("Fail to get top func at RetStatAST::statCodegen()!");

		//type cast if have diff type
		Type* RT = F->getReturnType();
		Type* VT = V->getType();
		if(RT != VT)
		{
			if(RT == Int32Type)
			{
				if(VT == DoubleType)
					V = Builder.CreateFPToSI(V,Int32Type,"fptosi");
				else
					return logErrorL("Invaild val type while the func ret type is int at RetStatAST::statCodegen()!");
			}
			else if(RT == DoubleType)
			{
				if(VT == Int32Type)
					V = Builder.CreateSIToFP(V,DoubleType,"sitofp");
				else
					return logErrorL("Invaild val type while the func ret type is double at RetStatAST::statCodegen()!");
			}
			else if(RT == VoidType)
				return true;
			else
				return logErrorL("Undefined ret type at RetStatAST::statCodegen()!");
		}
		auto RVA = ArgVarAddr["_retval"];
		if(!RVA)
			return logErrorL("Fail to get ret val addr at RetStatAST::statCodegen()!");

		Builder.CreateStore(V,RVA);
		Builder.CreateBr(RetBB);
		hasRetStat = true;
		return true;
	}
	//in the case "RET VOID"
	if(Builder.GetInsertBlock()->getParent()->getReturnType() != VoidType)
		return logErrorL("Invaild void ret type while func ret type isn't void at statCodegen()!");

	Builder.CreateBr(RetBB);
	hasRetStat = true;
	return true;
}
bool IfStatAST::statCodegen()
{
	auto Val = CondExpr->exprCodegen();
	if(!Val)
		return false;
		//return logErrorL("Fail to statCodegen for cond at ThenStatAST::statCodegen()!");

	Value* IfCond;
	switch(CondExpr->getType())
	{
	case DoubleT:
		IfCond = Builder.CreateFCmpUNE(Val,ConstantFP::get(TheContext,APFloat(0.0)),"ifcond");
		if(!IfCond)
			return logErrorL("Fail to CreateFCmpUNE at IfStatAST::statCodegen()!");
		break;
	
	case IntT:
		IfCond = Builder.CreateICmpNE(Val,ConstantInt::get(Int32Type,0),"ifcond");
		if(!IfCond)
			return logErrorL("Fail to CreateICmpNE at IfStatAST::statCodegen()!");
		break;
		
	default:
		return logErrorL("Undefined cond expr type at IfStatAST::statCodegen()!");
	}
	Function* F = Builder.GetInsertBlock()->getParent();
	if(!F)
		return logErrorL("Fail to get top-function at IfStatAST::statCodegen()!");
		
	BasicBlock* ThenBB = BasicBlock::Create(TheContext,"then",F);
	BasicBlock* ContBB = BasicBlock::Create(TheContext,"ifcont");
	if(ElseStat != nullptr)
	{
		BasicBlock* ElseBB = BasicBlock::Create(TheContext,"else");
	
		Builder.CreateCondBr(IfCond,ThenBB,ElseBB);
		
		Builder.SetInsertPoint(ThenBB);
		if(!ThenStat->statCodegen())
			return false;
		if(!hasRetStat)
			Builder.CreateBr(ContBB);
		else
			hasRetStat = false;

		F->getBasicBlockList().push_back(ElseBB);
	
		Builder.SetInsertPoint(ElseBB);
		if(!ElseStat->statCodegen())
			return false;
		if(!hasRetStat)
			Builder.CreateBr(ContBB);
		else
			hasRetStat = false;
	}
	else
	{	
		Builder.CreateCondBr(IfCond,ThenBB,ContBB);
		
		Builder.SetInsertPoint(ThenBB);
		if(!ThenStat->statCodegen())
			return false;
		if(!hasRetStat)
			Builder.CreateBr(ContBB);
		else
			hasRetStat = false;
	}
	F->getBasicBlockList().push_back(ContBB);

	Builder.SetInsertPoint(ContBB);
	return true;
}
bool WhileStatAST::statCodegen()
{
	Function* F = Builder.GetInsertBlock()->getParent();
	if(!F)
		return logErrorL("Fail to get top-function at WhileStatAST::statCodegen()!");

	BasicBlock* CondBB = BasicBlock::Create(TheContext,"while.cond",F);
	BasicBlock* LoopBB = BasicBlock::Create(TheContext,"while.loop");
	BasicBlock* EndBB = BasicBlock::Create(TheContext,"while.end");

	Builder.CreateBr(CondBB);	

	Builder.SetInsertPoint(CondBB);
	auto Val = Cond->exprCodegen();
	if(!Val)
		return false;
		//return logErrorL("Fail to statCodegen for cond at WhileStatAST::statCodegen()!");
	
	Value* WhileCond;
	switch(Cond->getType())
	{
	case DoubleT:
		WhileCond = Builder.CreateFCmpUNE(Val,ConstantFP::get(TheContext,APFloat(0.0)),"whilecond");
		if(!WhileCond)
			return logErrorL("Fail to CreateFCmpUNE at WhileStatAST::statCodegen()!");
		break;
	
	case IntT:
		WhileCond = Builder.CreateICmpNE(Val,ConstantInt::get(Int32Type,0),"whilecond");
		if(!WhileCond)
			return logErrorL("Fail to CreateICmpNE at WhileStatAST::statCodegen()!");
		break;
		
	default:
		return logErrorL("Undefined cond expr type at WhileStatAST::statCodegen()!");
	}
	Builder.CreateCondBr(WhileCond,LoopBB,EndBB);

	F->getBasicBlockList().push_back(LoopBB);

	Builder.SetInsertPoint(LoopBB);
	if(!Stat->statCodegen())
		return false;
		//return logErrorL("Fail to statCodegen for stat at WhileStatAST::statCodegen()!");

	Builder.CreateBr(CondBB);

	F->getBasicBlockList().push_back(EndBB);

	Builder.SetInsertPoint(EndBB);
	return true;
}
bool InputStatAST::statCodegen()
{	
	auto VA = Var->exprCodegenPtr();
	if(!VA)
		return false;
	
	Function* F;
	switch(Var->getType())
	{
		case IntT:	
			F = TheModule->getFunction("geti");
			if(!F)
				return logErrorL("Fail to find geti at InputStatAST::statCodegen()!");
			break;

		case DoubleT:	
			F = TheModule->getFunction("getf");
			if(!F)
				return logErrorL("Fail to find getf at InputStatAST::statCodegen()!");
			break;

		default:
			return logErrorL("Undefined var type at InputStatAST::statCodegen()!");
	}
	Builder.CreateCall(F,VA);
	return true;
}
bool OutputStatAST::statCodegen()
{
	Function* F;
	if(Var)
	{
		auto Val = Var->exprCodegen();
		if(!Val)
			return false;
	
		switch(Var->getType())
		{
		case IntT:	
			F = TheModule->getFunction("puti");
			if(!F)
				return logErrorL("Fail to find puti at OutputStatAST::statCodegen()!");
			break;

		case DoubleT:	
			F = TheModule->getFunction("putf");
			if(!F)
				return logErrorL("Fail to find putf at OutputStatAST::statCodegen()!");
			break;

		default:
			return logErrorL("Undefined var type at OutputStatAST::statCodegen()!");
		}
		Builder.CreateCall(F,Val);
	}
	else
	{
		F = TheModule->getFunction("putch");
		if(!F)
			return logErrorL("Fail to find putch at OutputStatAST::statCodegen()!");
		
		Builder.CreateCall(F,ConstantInt::get(Type::getInt8Ty(TheContext),Char));
	}
	return true;
}
bool MultiStatAST::statCodegen()
{
	for(auto& Stat:Stats)
		if(!Stat->statCodegen())
			return false;
	return true;
}
Function* PrototypeAST::codegen()
{
	Type* RT;
	switch(RetType)
	{
	case IntT:
		RT = Int32Type;
		break;
	case DoubleT:
		RT = DoubleType;
		break;
	case VoidT:
		RT = VoidType;
		break;
	default:
		return logErrorFunc("Undefined ret type at Prototype::codegen()!");
	}
	std::vector<Type*> ArgTypes;
	for(auto& Arg:Args)
	{
		switch(Arg->getBasicType())
		{
		case IntT:
			ArgTypes.push_back(Int32Type);
			break;
		case DoubleT:
			ArgTypes.push_back(DoubleType);
			break;
		default:
			return logErrorFunc("Invaild arg type at Prototype::codegen()!");
		}
	}
	FunctionType* FT = FunctionType::get(RT,ArgTypes,false);

	Function* F = Function::Create(FT,Function::ExternalLinkage,Name.c_str(),TheModule.get());

	int idx = 0;
	for(auto& Arg:F->args())
		Arg.setName(Args[idx++]->getName());

	return F;
}
bool FunctionAST::codegen()
{
	Function* F = Proto->codegen();
	if(!F)
		return false;
		//return logErrorL("Fail to codegen for proto at FunctionAST::codegen()!");

	BasicBlock* BB = BasicBlock::Create(TheContext,"entry",F);
	BasicBlock* RetBB = BasicBlock::Create(TheContext,"return");
	BasicBlock* OldRetBB = ::RetBB;
	::RetBB = RetBB;
	Builder.SetInsertPoint(BB);

	//save the old arg info
	std::map<std::string,AllocaInst*> OldBindings = ArgVarAddr;
	ArgVarAddr.clear();

	//use clang treat ret val method,ret val is a local var
	AllocaInst* RVA;
	switch(Proto->getRetType())
	{
	case IntT:
		RVA = Builder.CreateAlloca(Int32Type,0,"retval");
		ArgVarAddr["_retval"] = RVA;
		break;
	case DoubleT:
		RVA = Builder.CreateAlloca(DoubleType,0,"retval");
		ArgVarAddr["_retval"] = RVA;
		break;
	default:
		break;
	}
	//allocate space for arg var
	for(auto& Arg:Proto->getArgs())
	{	
		auto AVA = Arg->codegenArg();
		if(!AVA)
			return false;
			//return logErrorL("Fail to codegen for arg var at FunctionAST::codegen()!");

		ArgVarAddr[Arg->getName()] = AVA;
	}
	//store the passed args value in arg var
	for(auto& Arg:F->args())
	{
		//get arg var addr
		auto AVA = ArgVarAddr[Arg.getName()];
		if(!AVA)
			return logErrorL("Fail to get arg addr at FunctionAST::codegen()!");

		Builder.CreateStore(&Arg,AVA);
	}

	if(!Block->codegen())
		return false;
		//return logErrorL("Fail to codegen for block at FunctionAST::codegen()!");

	//load the ret val from var addr table
	if(!hasRetStat)
		Builder.CreateBr(RetBB);
	else
		hasRetStat = false;	

	F->getBasicBlockList().push_back(RetBB);
	Builder.SetInsertPoint(RetBB);	

	LoadInst* RV;
	switch(Proto->getRetType())
	{
	case IntT:
	case DoubleT:
		RVA = ArgVarAddr["_retval"];
		if(!RVA)
			return logErrorL("Fail to get ret val addr at FunctionAST::codegen()!");

		RV = Builder.CreateLoad(RVA,"retval");
		Builder.CreateRet(RV);
		break;
	case VoidT:
		Builder.CreateRetVoid();
		break;
	default:
		return logErrorL("Undefined func ret type at FunctionAST::codegen()!");
	}
	//restore the old ret bb
	::RetBB = OldRetBB;
	//restore the saved old arg info
	ArgVarAddr = OldBindings;	

	verifyFunction(*F);

	return true;	
}
AllocaInst* VariableAST::codegenArg()
{
	Type* T;
	switch(VT.BT)
	{
		case DoubleT:
			T=DoubleType;
			break;
		case IntT:
			T=Int32Type;
			break;
		default:
			return logErrorA("Undefined var type at VariableAST::codegenLocal()!");
	}
	auto AVA = Builder.CreateAlloca(T,0,Name.c_str());
	if(!AVA)
		return logErrorA("Fail to create alloca at VariableAST::codegenLocal()!");
	
	return AVA;
}
AllocaInst* VariableAST::codegenLocal()
{
	Type* T;
	switch(VT.BT)
	{
		case DoubleT:
			T=DoubleType;
			break;
		case IntT:
			T=Int32Type;
			break;
		default:
			return logErrorA("Undefined var type at VariableAST::codegenLocal()!");
	}
	if(VT.DimensionInfo.size() > 0)
	{
		//create the array type
		for(int i = VT.DimensionInfo.size()-1 ; i>=0 ; i--)
			T = ArrayType::get(T,VT.DimensionInfo[i]);
	}
	auto LVA = Builder.CreateAlloca(T,0,Name.c_str());
	if(!LVA)
		return logErrorA("Fail to create alloca at VariableAST::codegenLocal()!");
	
	return LVA;
}
Value* VariableAST::codegenGlobal()
{
	Type* T;
	Constant* InitVal;

	switch(VT.BT)
	{
		case DoubleT:
			T = DoubleType;
			InitVal = ConstantFP::get(TheContext,APFloat(0.0));
			break;
		case IntT:
			T = Int32Type;
			InitVal = ConstantInt::get(Int32Type,0);
			break;
		default:
			return logErrorV("Undefined var type at VariableAST::codegenGlobal()!");
	}
	if(VT.DimensionInfo.size() > 0)
	{
		//create the array type
		for(int i = VT.DimensionInfo.size()-1 ; i>=0 ; i--)
			T = ArrayType::get(T,VT.DimensionInfo[i]);
		InitVal = ConstantAggregateZero::get(T);
	}
	auto GVA = new GlobalVariable(*TheModule,T,false,GlobalVariable::InternalLinkage,InitVal,Name.c_str());
	if(!GVA)
		return logErrorV("Fail to create global var at VariableAST::codegenGlobal()!");
	
	return GVA;	
}
bool BlockAST::codegen()
{
	//save old map and clear,to make sure the map has only current vars
	std::map<std::string,AllocaInst*> OldBindings(LocalVarAddr);
	LocalVarAddr.clear();

	for(auto& Var:Vars)
	{
		auto LVA = Var->codegenLocal();
		if(!LVA)
			return false;

		LocalVarAddr[Var->getName()] = LVA;
	}

	auto CurBB = Builder.GetInsertBlock();
	for(auto& Func:Funcs)
		if(!Func->codegen())
			return false;
	Builder.SetInsertPoint(CurBB);

	auto S = Stat->statCodegen();
	if(!S)
		return false;

	LocalVarAddr = OldBindings;
	return true;
}
bool BlockAST::codegenP()
{
	FunctionType* FT = FunctionType::get(VoidType,false);

	auto F = Function::Create(FT,Function::ExternalLinkage,"main",TheModule.get());		
	
	BasicBlock* BB = BasicBlock::Create(TheContext,"entry",F);
	Builder.SetInsertPoint(BB);

	//make global var for top-level program
	for(auto& Var:Vars)
	{
		auto GVA = Var->codegenGlobal();
		if(!GVA)
			return false;
	}

	auto CurBB = Builder.GetInsertBlock();
	for(auto& Func:Funcs)
		if(!Func->codegen())
			return false;
	Builder.SetInsertPoint(CurBB);

	auto S = Stat->statCodegen();
	if(!S)
		return false;

	Builder.CreateRetVoid();

	return true;
}

//+----------------+
//	Parser
//+----------------+
std::unique_ptr<ExprAST> parseExpression();
std::unique_ptr<CallAST> parseCall();
std::unique_ptr<StatAST> parseStatement();
std::unique_ptr<VariableAST> parseVariable(char,BasicType);
std::unique_ptr<ExprAST> parseParenExpression()
{
	//paren_expr = "(" expr ")"
	getNextToken();

	auto Val = parseExpression();
	if(!Val)
		return nullptr;
		//return logErrorE("Fail to parse expr at parseParenExpression()!");

	if(CurTok != ')')
		return logErrorE("Miss \')\' after expr at parseParenExpression()!");

	getNextToken();
	
	return llvm::make_unique<ParenExprAST>(Val->getType(),std::move(Val));
}
std::unique_ptr<VarExprAST> parseVarExpression()
{
	//var = ident { "[" expr "]" }
	
	std::string Name = IdentName;
	std::vector<std::unique_ptr<ExprAST>> Indexs;

	getNextToken();
	//get var type
	VarType* VT = PST.AVT[Name];
	if(!VT)
	{
		VT = PST.LVT[Name];
		if(!VT)
		{
			VT = PST.GVT[Name];
			if(!VT)
				return logErrorVE("Fail to get var type at parseVarExpression()!");
		}
	}
	while(CurTok == '[')
	{
		getNextToken();

		auto Index = parseExpression();
		if(!Index)
			return nullptr;
			//return logErrorVE("Fail to parse array index expr at parseVarExpression()!");

		Indexs.push_back(std::move(Index));

		if(CurTok != ']')
			return logErrorVE("Miss \']\' at parseVarExpression()!");

		getNextToken();
	}
	//compare if var expr refer to the correct value,only allow basic type : int or double
	if(VT->DimensionInfo.size() != Indexs.size())
		return logErrorVE("Invalid index reference at parseVarExpression()!");

	return llvm::make_unique<VarExprAST>(VT->BT,Name,std::move(Indexs));
	
}
std::unique_ptr<ExprAST> parseBasicExpression()
{
	//basic_expr = num
	//  	     | ident
	//	     | "(" expr ")"
	if(CurTok==tok_ident)
	{
		auto IdentExpr = parseVarExpression();
		if(!IdentExpr)
			return nullptr;			

		return std::move(IdentExpr);
	}
	if(CurTok==tok_num)
	{
		double Val=NumVal;

		getNextToken();

		if(Val-(int)Val == 0)
			return llvm::make_unique<NumExprAST<int>>(IntT,(int)Val);
		else
			return llvm::make_unique<NumExprAST<double>>(DoubleT,Val);
	}
	if(CurTok=='(')
	{
		auto Val = parseParenExpression();
		if(!Val)
			return nullptr;
		
		return Val;
	}
	if(CurTok==tok_call)
	{
		auto Val = parseCall();
		if(!Val)
			return nullptr;

		return std::move(Val);
	}
	return logErrorE("Fail to get correct token at parseBasicExpression()!");
}
std::unique_ptr<ExprAST> parseBinOpRHS(int Prec,std::unique_ptr<ExprAST> LHS)
{
	int Op;
	int cur_prec,next_prec;
	while(1)
	{
		Op=CurTok;
		cur_prec=getTokenPrecedence();

		if(cur_prec <= Prec)
			return LHS;

		getNextToken();

		auto RHS=parseBasicExpression();
		if(!RHS)
			return nullptr;
			//return logErrorE("Fail to parse RHS at parseBinOpRHS()!");
		next_prec=getTokenPrecedence();
		if(cur_prec < next_prec)
			RHS=parseBinOpRHS(cur_prec,std::move(RHS));
			
		//allow only some types to calculate
		int BT;
		int LT = LHS->getType();
		int RT = RHS->getType();
		switch(LT)
		{
			case IntT:
			case DoubleT:
				break;
			default:
				return logErrorE("Undefined LHS type to make bin expr at parseBinOpRHS()!");
		}
		switch(RT)
		{
			case IntT:
			case DoubleT:
				break;
			default:
				return logErrorE("Undefined RHS type to make bin expr at parseBinOpRHS()!");
		}
		BT = LT;
		//bin expr type is only the high-bits type -- double > int > ...
		if(LT != RT)
		{
			if(LT==DoubleT || RT==DoubleT)
				BT = DoubleT;
		}		
		LHS=llvm::make_unique<BinExprAST>(BT,Op,std::move(LHS),std::move(RHS));
	}
}
std::unique_ptr<ExprAST> parseExpression()
{
	//expr = num 
	//     | ident
	//     | "(" expr ")"
	//     | expr op expr
	//     | "CALL" ...
	//in order to parse easier , expr syntax convert to:
	//expr = base_expr { op base_expr }
	//basic_expr = num
	//	    | ident
	//	    | "(" expr ")"
	//	    | "CALL" ...
	auto LHS=parseBasicExpression();
	if(!LHS)
		return nullptr;
		//return logErrorE("Failing to parse base expression at parseExpression()");
	return parseBinOpRHS(0,std::move(LHS));
}
std::unique_ptr<StatAST> parseAssignStatement()
{
	auto Var = parseVarExpression();
	if(!Var)
		return nullptr;

	if(CurTok != '=')
		return logErrorS("Miss \'=\' after ident at parseAssignStatement()!");
	
	getNextToken();
	
	auto Val=parseExpression();
	if(!Val)
		return nullptr;
		//return logErrorS("Fail to parse expr at parseAssignStatement()!");
	
	return llvm::make_unique<AssignStatAST>(std::move(Var),std::move(Val));
}
std::unique_ptr<CallAST> parseCall()
{	
	getNextToken();
	
	if(CurTok!=tok_ident)
		return logErrorC("Miss callee at parseCall()!");

	std::string Callee = IdentName;

	getNextToken();

	if(CurTok != '(')
		return logErrorC("Miss \'(\' after call at parseCall()!");

	getNextToken();

	std::vector<std::unique_ptr<ExprAST>> Args;
	if(CurTok != ')')
	{
		while(1)
		{
			auto Arg = parseExpression();
			if(!Arg)
				return logErrorC("Fail to parse arg expr at parseCall()!");
		
			Args.push_back(std::move(Arg));

			if(CurTok != ',')
				break;

			getNextToken();
		}
		if(CurTok != ')')
			return logErrorC("Miss \')\' at parseCall()!");

		getNextToken();
	}
	else
		getNextToken();
	//check if the call is vaild
	PrototypeAST* Proto = nullptr;
	
	for(auto& Fun:PST.AvailableFuncs)
		if(Fun->getName() == Callee)
		{	
			Proto = Fun;
			break;
		}

	if(!Proto)
	{
		Proto = PST.CurrentFunc;
		if(!Proto || Proto->getName() != Callee)	
			return logErrorC("Fail to find callee at parseCall()!");
	}

	if(Proto->getArgs().size() != Args.size())
		return logErrorC("Invaild arg nums at parseCall()!");

	//add args type cast support if needed
	int idx = 0;
	for(auto& Arg:Proto->getArgs())
	{
		if(Arg->getBasicType() != Args[idx]->getType())
		{
			Args[idx]->setType(Arg->getBasicType());
			//return logErrorC("Invaild arg type at parseCall()!");
		}
		idx++;	
	}
	BasicType BT = Proto->getRetType();
	
	return llvm::make_unique<CallAST>(BT,Callee,std::move(Args));
}
std::unique_ptr<StatAST> parseRetStatement()
{
	getNextToken();
	//in the case "RET VOID"
	if(CurTok==tok_void)
	{
		getNextToken();
		return llvm::make_unique<RetStatAST>(nullptr);
	}

	auto Val = parseExpression();
	if(!Val)
		return nullptr;
		//return logErrorS("Fail to parse ret expr at parseRetStatement()!");

	return llvm::make_unique<RetStatAST>(std::move(Val));
}
std::unique_ptr<StatAST> parseIfStatement()
{
	getNextToken();
	
	auto CondExpr = parseExpression();
	if(!CondExpr)
		return nullptr;
	
	if(CurTok != tok_then)
		return logErrorS("Miss THEN after ifcond at parseIfStatement()!");

	getNextToken();

	auto ThenStat = parseStatement();
	if(!ThenStat)
		return nullptr;
	
	if(CurTok == tok_else)
	{
		getNextToken();
		auto ElseStat = parseStatement();
		if(!ElseStat)
			return nullptr;

		return llvm::make_unique<IfStatAST>(std::move(CondExpr),std::move(ThenStat),std::move(ElseStat));
	}
	return llvm::make_unique<IfStatAST>(std::move(CondExpr),std::move(ThenStat));
}
std::unique_ptr<StatAST> parseWhileStatement()
{
	getNextToken();

	auto Cond = parseExpression();
	if(!Cond)
		return nullptr;
		//return logErrorS("Fail to parse condition at parseWhileStatement()!");

	if(CurTok!=tok_do)
		return logErrorS("Miss DO after condition at parseWhileStatement()!");

	getNextToken();
		
	auto Stat = parseStatement();
	if(!Stat)
		return nullptr;
		//return logErrorS("Fail to parse statement at parseWhileStatement()!");

	return llvm::make_unique<WhileStatAST>(std::move(Cond),std::move(Stat));
}
std::unique_ptr<StatAST> parseInputStatement()
{
	getNextToken();
	
	if(CurTok!=tok_ident)
		return logErrorS("Miss identifier after \'?\' at parseInputStatement()!");

	auto Var = parseVarExpression();
	if(!Var)
		return nullptr;

	return llvm::make_unique<InputStatAST>(std::move(Var));
}
std::unique_ptr<StatAST> parseOutputStatement()
{
	getNextToken();

	if(CurTok==tok_ident)
	{
		auto Var = parseVarExpression();
		if(!Var)
			return nullptr;
		
		return llvm::make_unique<OutputStatAST>(std::move(std::move(Var)),'\0');
	}
	
	if(CurTok == '\'')
	{
		char Char;
		getNextToken();

		if(CurTok == '\'')
		{
			getNextToken();
	
			Char=' ';
			return llvm::make_unique<OutputStatAST>(nullptr,Char);
		}
		if(CurTok == '\\')
		{
			getNextToken();
			
			if(CurTok == '\'')
			{
				Char='\'';
			}else if(CurTok == tok_ident)
			{
				if(IdentName[0] == 'n')Char='\n';
				else if(IdentName[0] =='t')Char='\t';
				else
					return logErrorS("Fail to get corret escape char after \\ at parseOutputStatement()!");	
			}else
				return logErrorS("Fail to get escape char at parseOutputStatement()!");	
		}
		else if(CurTok==tok_ident)Char=IdentName[0];
		else if(CurTok==tok_num)Char=int(NumVal)+48;
		else if(CurTok>0)Char=CurTok;
		else if(isOpToken())
		{
			switch(CurTok)
			{
			case tok_add:Char='+';break;
			case tok_sub:Char='-';break;
			case tok_rem:Char='%';break;
			case tok_mul:Char='*';break;
			case tok_div:Char='/';break;
			case tok_gth:Char='>';break;
			case tok_lth:Char='<';break;
			}
		}
		else
			return logErrorS("Fail to get correct char after \' at parseOutputStatement()!");	
		
		getNextToken();

		if(CurTok != '\'')
			return logErrorS("Miss \' after char at parseOutputStatement()!");

		getNextToken();

		return llvm::make_unique<OutputStatAST>(nullptr,Char);
	}
	return logErrorS("Fail to get correct CurTok at parseOutputStatement()!");	
}
std::unique_ptr<StatAST> parseMultiStatement()
{
	std::vector<std::unique_ptr<StatAST>> Stats;
	getNextToken();
	
	while(1)
	{	auto Stat = parseStatement();
		if(!Stat)
			return nullptr;
			//return logErrorS("Fail to parse statement at parseMultiStatement()!");
		
		Stats.push_back(std::move(Stat));
		
		if((char)CurTok == ';')
			getNextToken();
		else
			break;
	}
	if(CurTok != tok_end)
		return logErrorS("Missing \"END\" at parseMultiStatement()!");

	getNextToken();
	
	return llvm::make_unique<MultiStatAST>(std::move(Stats));
}

std::unique_ptr<StatAST> parseStatement()
{
	//stat = "BEGIN" stat { ";" stat } "END"
	//     | ident "=" expr
	//     | "CALL" ident
	//     | "IF" expr "THEN" stat [ "ELSE" stat ]
	//     | "WHILE" expr "DO" stat
	//     | "?" ident
	//     | "!" ident
	if(CurTok==tok_ident)
	{
		auto Stat=parseAssignStatement();
		if(!Stat)
			return nullptr;
		return Stat;
	}
	
	if(CurTok==tok_call)
	{
		std::unique_ptr<StatAST> Stat=parseCall();
		if(!Stat)
			return nullptr;
		return Stat;
	}
	
	if(CurTok==tok_ret)
	{
		auto Stat=parseRetStatement();
		if(!Stat)
			return nullptr;
		return Stat;
	}
	
	if(CurTok==tok_begin)
	{
		auto Stat=parseMultiStatement();
		if(!Stat)
			return nullptr;
		
		return Stat;
	}

	if(CurTok==tok_if)
	{
		auto Stat=parseIfStatement();
		if(!Stat)
			return nullptr;
		
		return Stat;
	}	
	
	if(CurTok==tok_while)
	{
		auto Stat=parseWhileStatement();
		if(!Stat)
			return nullptr;
		
		return Stat;
	}	

	if(CurTok == '?')
	{
		auto Stat = parseInputStatement();
		if(!Stat)
			return nullptr;
		
		return Stat;
	}
	
	if(CurTok == '!')
	{
		auto Stat = parseOutputStatement();
		if(!Stat)
			return nullptr;
		
		return Stat;
	}	
	return logErrorS("Fail to parse stat at parseStatement()!");
}
PrototypeAST* parsePrototype()
{
	getNextToken();

	BasicType RetType;

	switch(CurTok)
	{
	case tok_int:
		RetType = IntT;
		break;
	case tok_double:
		RetType = DoubleT;
		break;
	case tok_void:
		RetType = VoidT;
		break;
	default:
		return logErrorProto("Miss ret type at parsePrototype()!");
	}
	getNextToken();

	if(CurTok!=tok_ident)
		return logErrorProto("Miss ident at parsePrototype()!");
	
	std::string Name = IdentName;

	getNextToken();

	if(CurTok != '(')
		return logErrorProto("Miss \'(\' at parsePrototype()!");

	getNextToken();

	std::vector<std::unique_ptr<VariableAST>> Args;
	BasicType BT;
	while(isTypeToken())
	{
		switch(CurTok)
		{	
		case tok_int:
			BT = IntT;
			break;
		case tok_double:
			BT = DoubleT;
			break;
		default:
			return logErrorProto("Error type at parsePrototype()!");
		}
		getNextToken();

		auto Var = parseVariable('a',BT);
		if(!Var)
			return nullptr;
			//return logErrorProto("Fail to parse var at parsePrototype()!");
		Args.push_back(std::move(Var));

		if(CurTok != ',')
			break;
			
		getNextToken();
	}
	if(CurTok != ')')
		return logErrorProto("Miss \')\' at parsePrototype()!");

	getNextToken();

	if(CurTok != ';')
		return logErrorProto("Miss \';\' at parsePrototype()!");
	
	getNextToken();

	PrototypeAST* Proto = new PrototypeAST(RetType,Name,std::move(Args));
	if(!Proto)
		return logErrorProto("Fail to create proto at parsePrototype()!");

	PST.AvailableFuncs.push_back(Proto);

	return Proto;
}
std::unique_ptr<BlockAST> parseBlock();
std::unique_ptr<FunctionAST> parseFunction()
{
	//procedure = "PROCEDURE" ident ";" block ";"
	std::unique_ptr<BlockAST> Block;

	auto Proto = parsePrototype();
	if(!Proto)
		return nullptr;	
	//save the cur func to support recursive func call
	PrototypeAST* OldBinding = PST.CurrentFunc;
	PST.CurrentFunc = Proto;

	Block=parseBlock();
	if(!Block)
		return nullptr;
	//restore the prior func
	PST.CurrentFunc=OldBinding;
	
	if(CurTok != ';')
		return logErrorF("Miss \';\' after block at parseFunctions()");

	getNextToken();
	
	PST.AvailableFuncs.push_back(Proto);	

	return llvm::make_unique<FunctionAST>(Proto,std::move(Block));	
}
std::unique_ptr<VariableAST> parseVariable(char VarKind,BasicType BT)
{
	VarType* VT = new VarType;
	std::string Name;

	Name = IdentName;
	VT->BT = BT;

	getNextToken();
	//arg var now not allow to use array type
	if(VarKind == 'a' && CurTok == '[')
		return logErrorVar("Fail to make array var for arg at parseVariable()!");

	while(CurTok == '[')
	{
		getNextToken();

		if(CurTok!=tok_num)
			return logErrorVar("Miss num after \'[\' at parseVariable()!");

		VT->DimensionInfo.push_back((int)NumVal);

		getNextToken();
		
		if(CurTok != ']')
			return logErrorVar("Miss \']\' at parseVariable()!");

		getNextToken();
	}
	//add var info to PST
	switch(VarKind)
	{
	case 'l':
		PST.LVT[Name] = VT;
		break;
	case 'g':
		PST.GVT[Name] = VT;
		break;
	case 'a':
		PST.AVT[Name] = VT;
		break;
	default:
		return logErrorVar("Invaild var kind arg at parseVariable()!");
	}
	return llvm::make_unique<VariableAST>(Name,*VT);
}
std::unique_ptr<BlockAST> parseBlock()
{
	//block = { type ident { "," ident } ";" } { procedure } stat
	std::vector<std::unique_ptr<VariableAST>> Vars;
	std::vector<std::unique_ptr<FunctionAST>> Funcs;
	std::unique_ptr<StatAST> Stat;
	
	//record the old var type info
	std::map<std::string,VarType*> OldBindings = PST.LVT;
	PST.LVT.clear();	

	BasicType BT;	
	while(isTypeToken())
	{
		switch(CurTok)
		{
		case tok_int:
			BT = IntT;
			break;
		case tok_double:
			BT = DoubleT;
			break;
		default:
			return logErrorB("Error type at parseVariable()!");
		}

		getNextToken();

		while(1)
		{
			if(CurTok!=tok_ident)
				return logErrorB("Miss ident at parseBlock()!");

			auto Var = parseVariable('l',BT);
			if(!Var)
				return nullptr;

			Vars.push_back(std::move(Var));		

			if(CurTok != ',')
				break;

			getNextToken();
		}
		if(CurTok != ';')
			return logErrorB("Miss \';\' at parseBlock()!");

		getNextToken();
	}
	
	int FuncNums = 0;
	while(CurTok==tok_proc)
	{
		auto Func=parseFunction();
		if(!Func)
			return nullptr;
			//return logErrorB("Fail to parse function at parseBlock()!");

		Funcs.push_back(std::move(Func));
		FuncNums++;
	}

	Stat=parseStatement();
	if(!Stat)
		return nullptr;
		//return logErrorB("Fail to parse statement at parseBlock()!");

	//restore the recorded var type info
	PST.LVT = OldBindings;

	//remove the sub procedures
	if(FuncNums > 0)
		PST.AvailableFuncs.erase(PST.AvailableFuncs.end()-FuncNums,PST.AvailableFuncs.end());

	return llvm::make_unique<BlockAST>(std::move(Vars),std::move(Funcs),std::move(Stat));
}
std::unique_ptr<BlockAST> parseProgram()
{
	//program = block "."
	std::vector<std::unique_ptr<VariableAST>> Vars;
	std::vector<std::unique_ptr<FunctionAST>> Funcs;
	std::unique_ptr<StatAST> Stat;
	
	BasicType BT;
	while(isTypeToken())
	{
		switch(CurTok)
		{
			case tok_int:
				BT = IntT;
				break;
			case tok_double:
				BT = DoubleT;
				break;
			default:
				return logErrorB("Undefined type at parseProgram()!");
		}

		getNextToken();

		while(1)
		{
			if(CurTok!=tok_ident)
				return logErrorB("Miss ident at parseProgram()!");

			auto Var = parseVariable('g',BT);
			if(!Var)
				return nullptr;

			Vars.push_back(std::move(Var));		

			if(CurTok != ',')
				break;

			getNextToken();
		}
		if(CurTok != ';')
			return logErrorB("Miss \';\' at parseProgram()!");

		getNextToken();
	}
	
	int FuncNums = 0;
	while(CurTok==tok_proc)
	{
		auto Func=parseFunction();
		if(!Func)
			return nullptr;
			//return logErrorB("Fail to parse function at parseProgram()!");
		Funcs.push_back(std::move(Func));
		FuncNums++;
	}

	Stat=parseStatement();
	if(!Stat)
		return nullptr;
		//return logErrorB("Fail to parse statement at parseProgram()!");

	//remove the sub procedures
	if(FuncNums > 0)
		PST.AvailableFuncs.erase(PST.AvailableFuncs.end()-FuncNums,PST.AvailableFuncs.end());

	if(CurTok != '.')
		return logErrorB("Miss \'.\' after block at parseProgram()!");

	return llvm::make_unique<BlockAST>(std::move(Vars),std::move(Funcs),std::move(Stat));
}

//+---------------+
//	test
//+---------------+
void testLexer()
{
	while(1)
	{
		switch(CurTok)
		{
			case tok_eof:printf("eof\n");return;
			case tok_ident:printf("ident ");break;
			case tok_num:printf("num ");break;

			case tok_proc:printf("proc ");break;
			case tok_int:printf("int ");break;

			case tok_call:printf("call ");break;
			case tok_begin:printf("begin\n");break;
			case tok_end:printf("end\n");break;
			case tok_if:printf("if ");break;
			case tok_then:printf("then\n");break;
			case tok_else:printf("else\n");break;

			case tok_add:printf("+");break;
			case tok_sub:printf("-");break;
			case tok_mul:printf("*");break;
			case tok_div:printf("/");break;

			case tok_equ:printf("==");break;
			case tok_neq:printf("!=");break;
			case tok_gth:printf(">");break;
			case tok_geq:printf(">=");break;
			case tok_lth:printf("<");break;
			case tok_leq:printf("<=");break;

			default:printf("%c",(char)CurTok);
		}
		getNextToken();
	}
}
void testParser()
{
	auto P = parseProgram();
	if(!P)
		return;
	printf("acc!\n");
	
	printf("program\n");
	P->printAST();
}
void testCodegen()
{
	auto P = parseProgram();
	if(!P)
		return;
		//return logError("Failing to parse program at testCodegen()!\n");
	
	if(!P->codegenP())
		return;
	
	TheModule->print(llvm::outs(), nullptr);
}
void testJIT()
{
	auto P = parseProgram();
	if(!P)
		return;
		//return logError("Fail to parse program!\n");


	if(!P->codegenP())
		return;
		//return logError("Fail to codegen for program!\n");

	freopen("/dev/tty","r+",stdin);

	TheModule->print(llvm::outs(), nullptr);	

	auto H = JIT->addModule(std::move(TheModule));

	void (*Main)() = (void(*)())(JIT->getSymbolAddress("main"));
	Main();

	JIT->removeModule(H);
}
void printTable()
{
	printf(".------------------------------------------.\n");
	printf("|          PL/0 front end On LLVM          |\n");
	printf("+------------------------------------------+\n");
	printf("| 1) print AST                             |\n");
	printf("| 2) dump IR                               |\n");
	printf("| 3) optimize IR                           |\n");
	printf("| 4) display tokens                        |\n");
	printf("| 5) JIT code & quit                       |\n");
	printf("| 6) emit object file & quit               |\n");
	printf("| 7) quit                                  |\n");
	printf("`------------------------------------------'\n");
}
void printAST(std::unique_ptr<BlockAST>& P)
{
	fprintf(stderr,"program\n");
	P->printAST();
}
void dumpIR()
{
	TheModule->print(llvm::outs(), nullptr);
}
void codeJIT()
{
	JIT = llvm::make_unique<Pl0JIT>();

	TheModule->setDataLayout(JIT->getTargetMachine().createDataLayout());

	auto H = JIT->addModule(std::move(TheModule));

	void (*Main)() = (void(*)())(JIT->getSymbolAddress("main"));
	Main();

	JIT->removeModule(H);
}
void emitToObject()
{
	auto TargetTriple = sys::getDefaultTargetTriple();
	
	std::string Error;
	auto Target = TargetRegistry::lookupTarget(TargetTriple,Error);

	if(!Target)
		return logError(Error.c_str(),'g');

	auto CPU = "generic";
	auto Features = "";
	TargetOptions opt;
	auto RM = Optional<Reloc::Model>();
	
	auto TargetMachine = Target->createTargetMachine(TargetTriple,CPU,Features,opt,RM);

	TheModule->setDataLayout(TargetMachine->createDataLayout());
	TheModule->setTargetTriple(TargetTriple);

	auto FileName = "output.o";
	std::error_code EC;
	raw_fd_ostream dest(FileName,EC,sys::fs::F_None);

	if(EC)
		return logError(EC.message().c_str(),'g');

	legacy::PassManager pass;
	auto FileType = TargetMachine::CGFT_ObjectFile;

	if(TargetMachine->addPassesToEmitFile(pass,dest,FileType))
		return logError("Target Machine can't emit a file of this type!\n",'g');

	pass.run(*TheModule);
	dest.flush();

	printf("write to file \"output.o\"\n");
}	
void displayTokens()
{
	int i=0;
	for(auto Tok:Tokens)
	{
		switch(Tok)
		{
		case tok_eof:printf("%10s","eof");return;
		case tok_ident:printf("%10s","ident");break;
		case tok_num:printf("%10s","num");break;

		case tok_proc:printf("%10s","proc");break;
		case tok_int:printf("%10s","int");break;
		case tok_double:printf("%10s","double");break;
		case tok_void:printf("%10s","void");break;

		case tok_call:printf("%10s","call");break;
		case tok_ret:printf("%10s","ret");break;
		case tok_begin:printf("%10s","begin");break;
		case tok_end:printf("%10s","end");break;
		case tok_if:printf("%10s","if");break;
		case tok_then:printf("%10s","then");break;
		case tok_else:printf("%10s","else");break;
		case tok_while:printf("%10s","while");break;
		case tok_do:printf("%10s","do");break;

		case tok_and:printf("%10s","&&");break;
		case tok_or:printf("%10s","||");break;

		case tok_add:printf("%10s","+");break;
		case tok_sub:printf("%10s","-");break;
		case tok_rem:printf("%10s","%");break;
		case tok_mul:printf("%10s","*");break;
		case tok_div:printf("%10s","/");break;

		case tok_equ:printf("%10s","==");break;
		case tok_neq:printf("%10s","!=");break;
		case tok_gth:printf("%10s",">");break;
		case tok_geq:printf("%10s",">=");break;
		case tok_lth:printf("%10s","<");break;
		case tok_leq:printf("%10s","<=");break;

		default:printf("%10c",Tok);
		}
		i++;
		if(i%10==0)printf("\n");
	}
	printf("\n");
}
void optimizeIR()
{
	auto FPM = llvm::make_unique<legacy::FunctionPassManager>(TheModule.get());

	FPM->add(createPromoteMemoryToRegisterPass());
 	FPM->add(createInstructionCombiningPass());
       	FPM->add(createReassociatePass());
	FPM->add(createGVNPass());
        FPM->add(createCFGSimplificationPass());
        FPM->doInitialization();
  	
	for (auto &F : *TheModule)
		FPM->run(F);

	dumpIR();
}
void printSource()
{
	int line = 1;
	unsigned idx = 0;
	char ch;

	printf(".------------------.\n");
	printf("| source file text |\n");
	printf("`------------------'\n");
	while(idx < SourceText.size())
	{
		printf("%d:\t",line++);
		while(idx < SourceText.size())
		{	
			ch = SourceText[idx++];
			printf("%c",ch);
			if(ch == '\n')
				break;
		}
	}
	printf("\n");
}
void mainLoop()
{
	auto P = parseProgram();
	if(!P)
	{
		printSource();
		return;
	}
	
	if(!P->codegenP())
		return;	

	system("clear");
	freopen("/dev/tty","r+",stdin);

	int key;
	while(1)
	{
		printTable();
		printf("select:");
		scanf("%d",&key);
		switch(key)
		{
			case 1:system("clear");printAST(P);break;
			case 2:system("clear");dumpIR();break;
			case 3:system("clear");optimizeIR();break;
			case 4:system("clear");displayTokens();break;
			case 5:codeJIT();return;
			case 6:emitToObject();return;
			case 7:return;
			default:printf("please select again.\n");
		}
		PAUSE
		system("clear");
	}
}
void garbageCollection()
{
	//free space of map<... , (type)*>
	for(auto& VT:PST.LVT)
		delete VT.second;
	for(auto& VT:PST.GVT)
		delete VT.second;
}
int main()
{
	initialization();
	mainLoop();
	//garbageCollection();
	return 0;
}
