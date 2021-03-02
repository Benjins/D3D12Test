#include <stdio.h>

#include <Windows.h>

#include <d3d12.h>

#include <d3dcompiler.h>

#include <dxgi1_2.h>

#include "basics.h"

#include "d3d12_ext.h"

#include "fuzz_shader_compiler.h"

#include "fuzz_dxbc.h"

#include "shader_meta.h"

#include "string_stack_buffer.h"

#include "d3d_resource_mgr.h"

#include "stb_image_write.h"

#include <assert.h>
#include <unordered_map>


// struct ShaderAST;
// struct ShaderResource;
// struct ShaderInput;

// Uncomment this to get pipeline statistics, esp. Pixel Shader invocations
//#define WITH_PIPELINE_STATS_QUERY

// Generate AST from 

typedef unsigned char byte;

//enum struct D3DShaderType
//{
//	Vertex,
//	Pixel,
//	Compute,
//	Count
//};

struct FuzzShaderASTNode
{
	// ???
	enum struct VariableType
	{
		Float4,
		Int,
		Bool
	};

	enum struct NodeType
	{
		BinaryOperator,
		TextureAccess,
		ReadConstant,
		ReadVariable,
		Literal,
		FuncCall,

		StatementFirst,
		Assignment = StatementFirst,
		RangeForLoop,
		// TODO:
		//IfBranch,
		StatementBlock,
		StatementLast = StatementBlock,

		Count
	};

	NodeType Type;
};

struct FuzzShaderFuncCall : FuzzShaderASTNode
{
	static constexpr NodeType StaticType = NodeType::FuncCall;

	std::string FuncName;
	std::vector<FuzzShaderASTNode*> Arguments;
	int32 OutputSize = 0; // In 32-bit components, e.g. 1 = float 4 = float4
};

struct FuzzShaderBinaryOperator : FuzzShaderASTNode
{
	enum struct Operator
	{
		Add,
		Subtract,
		Multiply,
		Divide,
		Count
	};


	static constexpr NodeType StaticType = NodeType::BinaryOperator;

	FuzzShaderASTNode* LHS = nullptr;
	FuzzShaderASTNode* RHS = nullptr;
	Operator Op;
};

struct FuzzShaderTextureAccess : FuzzShaderASTNode
{
	static constexpr NodeType StaticType = NodeType::TextureAccess;

	std::string TextureName;
	std::string SamplerName;
	FuzzShaderASTNode* UV = nullptr;
};

struct FuzzShaderAssignment : FuzzShaderASTNode
{
	static constexpr NodeType StaticType = NodeType::Assignment;

	std::string VariableName;
	FuzzShaderASTNode* Value = nullptr;
	bool IsPredeclared = false; // Really just used in a hack for the end of the vertex shader
};

struct FuzzShaderLiteral : FuzzShaderASTNode
{
	static constexpr NodeType StaticType = NodeType::Literal;

	float Values[4] = {};
};

struct FuzzShaderReadVariable : FuzzShaderASTNode
{
	static constexpr NodeType StaticType = NodeType::ReadVariable;

	std::string VariableName;
};

struct FuzzShaderStatementBlock : FuzzShaderASTNode
{
	static constexpr NodeType StaticType = NodeType::StatementBlock;

	std::vector<FuzzShaderASTNode*> Statements;
};

//struct FuzzShaderResourceBinding
//{
//	enum struct ResourceType
//	{
//		// Texture
//		// Sampler
//		// ????
//	};
//};

struct FuzzShaderRootConstants
{
	std::string VarName;
	int32 ConstantCount = 0; // In 4x32-bit constants, e.g. float4
	int32 SlotIndex = 0;
};

struct FuzzShaderRootCBV
{
	std::string VarName;
	int32 ConstantCount = 0;
	int32 SlotIndex = 0;
};

//struct FuzzShaderRootCBVDescriptorTable
//{
//	std::string VarName;
//	int32 ConstantCount = 0;
//};


struct FuzzShaderTextureBinding
{
	std::string SamplerName;
	std::string ResourceName;
	int32 SlotIndex = 0;
};

struct FuzzShaderSemanticVar
{
	ShaderSemantic Semantic = ShaderSemantic::POSITION;
	std::string VarName;
	int32 SemanticIdx = 0;
	int32 ParamIdx = 0;
};

struct FuzzShaderAST
{
	D3DShaderType Type;
	// ....

	// Var names for the vertex stage input from Input Assembler
	// (must be empty for pixel shader)
	std::vector<FuzzShaderSemanticVar> IAVars;

	// Var names for Vertex output and Pixel input
	std::vector<FuzzShaderSemanticVar> InterStageVars;

	std::vector<FuzzShaderRootConstants> RootConstants;
	std::vector<FuzzShaderRootCBV> RootCBVs;
	std::vector<FuzzShaderTextureBinding> BoundTextures;


	FuzzShaderASTNode* RootASTNode = nullptr;
	
	struct BlockAllocation
	{
		enum { BlockSize = 64 * 1024 };

		byte* Base = nullptr;
		byte* Stack = nullptr;

		byte* Allocate(int Size, int Alignment = sizeof(void*))
		{
			byte* AlignedStart = (byte*)(((size_t)Stack + Alignment - 1) / Alignment * Alignment);
			if (AlignedStart + Size <= Base + BlockSize)
			{
				Stack = AlignedStart + Size;
				return AlignedStart;
			}
			else
			{
				return nullptr;
			}
		}

		BlockAllocation()
		{
			Base = (byte*)malloc(BlockSize);
			Stack = Base;
		}

		~BlockAllocation()
		{
			free(Base);
			Base = nullptr;
			Stack = nullptr;
		}
	};
	
	std::vector<BlockAllocation*> BlockAllocations;
	// TODO: Some of these will have virtual dtors...need to preserve type info or just make it virtual and suck it up
	// Orrr.....we play very dirty and use the type info to cast it and call the right dtor
	std::vector<FuzzShaderASTNode*> AllocatedNodes;

	std::vector<std::unordered_map<std::string, FuzzShaderASTNode*>> VariablesInScope;

	// TODO: Might be worth caching some of this info,
	// depends on how much time it ends up taking
	int GetNumVariablesInScope() const
	{
		int Total = 0;
		for (const auto& VarMap : VariablesInScope)
		{
			Total += VarMap.size();
		}

		return Total;
	}

	std::string GetNthVariableInScope(int Index) const
	{
		for (const auto& VarMap : VariablesInScope)
		{
			if (Index < VarMap.size())
			{
				auto Iter = VarMap.begin();
				std::advance(Iter, Index);
				return Iter->first;
			}
			else
			{
				Index -= VarMap.size();
			}
		}

		assert(false);
		return "";
	}

	template<typename T>
	T* AllocateNode()
	{
		if (BlockAllocations.size() == 0)
		{
			BlockAllocations.push_back(new BlockAllocation());
		}

		byte* MemoryToUse = nullptr;

		if (byte* Memory = BlockAllocations.back()->Allocate(sizeof(T), alignof(T)))
		{
			MemoryToUse = Memory;
		}
		else
		{
			BlockAllocations.push_back(new BlockAllocation());

			if (byte* MemoryTry2 = BlockAllocations.back()->Allocate(sizeof(T), alignof(T)))
			{
				MemoryToUse = MemoryTry2;
			}
			else
			{
				assert(false && "Enksjdgbse");
			}
		}

		assert(MemoryToUse != nullptr);

		T* NewNode = new (MemoryToUse) T();
		NewNode->Type = T::StaticType;
		AllocatedNodes.push_back(NewNode);

		return NewNode;
	}

	// Constants
	// Vertex Attribs
	// Vertex->Pixel raster variables

	std::string SourceCode;
	ID3DBlob* ByteCodeBlob = nullptr;
	ShaderMetadata ShaderMeta;

	~FuzzShaderAST()
	{
		// TODO: If we ever start drawing, this will need to be released after fence completes
		ByteCodeBlob->Release();

		for (auto* Node : AllocatedNodes)
		{
			if (Node->Type == FuzzShaderASTNode::NodeType::StatementBlock)
			{
				auto* StmtBlock = (FuzzShaderStatementBlock*)Node;
				StmtBlock->~FuzzShaderStatementBlock();
			}
			else if (Node->Type == FuzzShaderASTNode::NodeType::ReadVariable)
			{
				auto* ReadVar = (FuzzShaderReadVariable*)Node;
				ReadVar->~FuzzShaderReadVariable();
			}
			else if (Node->Type == FuzzShaderASTNode::NodeType::Assignment)
			{
				auto* Assnmt = (FuzzShaderAssignment*)Node;
				Assnmt->~FuzzShaderAssignment();
			}
			else if (Node->Type == FuzzShaderASTNode::NodeType::TextureAccess)
			{
				auto* Tex = (FuzzShaderTextureAccess*)Node;
				Tex->~FuzzShaderTextureAccess();
			}
			else if (Node->Type == FuzzShaderASTNode::NodeType::FuncCall)
			{
				auto* Tex = (FuzzShaderFuncCall*)Node;
				Tex->~FuzzShaderFuncCall();
			}
		}

		for (const auto& Block : BlockAllocations)
		{
			delete Block;
		}

		BlockAllocations.clear();
	}
};

struct FuzzShaderBuiltinFuncInfo {
	const char* Name = "";
	int32 Arity = 0;
	int32 OutputSize = 0;
};

FuzzShaderBuiltinFuncInfo BuiltinShaderFuncInfo[] = {
	{ "dot", 2, 1},
	{ "dst", 2, 4},
	{ "any", 1, 1},
	{ "all", 1, 1},
	{ "abs", 1, 4},
	{ "saturate", 1, 4},
	{ "clamp", 3, 4},
	{ "ceil", 1, 4},
	{ "sin", 1, 4},
	{ "cos", 1, 4},
	{ "atan2", 2, 4},
};

FuzzShaderASTNode* GenerateFuzzingShaderValue(ShaderFuzzingState* Fuzzer, FuzzShaderAST* OutShaderAST, int32 CurrentDepth)
{
	float Decider = Fuzzer->GetFloat01();

	const int32 MaxDepth = 8;

	if (CurrentDepth < MaxDepth && Decider < 0.1)
	{
		// Binary Op
		auto* BinaryOp = OutShaderAST->AllocateNode<FuzzShaderBinaryOperator>();
		BinaryOp->Op = (FuzzShaderBinaryOperator::Operator)Fuzzer->GetIntInRange(0, (int32)FuzzShaderBinaryOperator::Operator::Count - 1);
		BinaryOp->LHS = GenerateFuzzingShaderValue(Fuzzer, OutShaderAST, CurrentDepth + 1);
		BinaryOp->RHS = GenerateFuzzingShaderValue(Fuzzer, OutShaderAST, CurrentDepth + 1);

		return BinaryOp;
	}
	else if (CurrentDepth < MaxDepth && Decider < 0.5)
	{
		// Func call
		auto* FuncCall = OutShaderAST->AllocateNode<FuzzShaderFuncCall>();

		constexpr int32 NumBuiltins = ARRAY_COUNTOF(BuiltinShaderFuncInfo);

		FuzzShaderBuiltinFuncInfo* BuiltinInfo = &BuiltinShaderFuncInfo[Fuzzer->GetIntInRange(0, NumBuiltins - 1)];

		FuncCall->FuncName = BuiltinInfo->Name;
		FuncCall->OutputSize = BuiltinInfo->OutputSize;
		for (int32 i = 0; i < BuiltinInfo->Arity; i++)
		{
			FuncCall->Arguments.push_back(GenerateFuzzingShaderValue(Fuzzer, OutShaderAST, CurrentDepth + 1));
		}

		return FuncCall;
	}
	else if (CurrentDepth < MaxDepth && Decider < 0.6 && OutShaderAST->BoundTextures.size() > 0)
	{
		auto* Tex = OutShaderAST->AllocateNode<FuzzShaderTextureAccess>();
		Tex->TextureName = OutShaderAST->BoundTextures[Fuzzer->GetIntInRange(0, OutShaderAST->BoundTextures.size() - 1)].ResourceName;
		Tex->SamplerName = OutShaderAST->BoundTextures[Fuzzer->GetIntInRange(0, OutShaderAST->BoundTextures.size() - 1)].SamplerName;
		Tex->UV = GenerateFuzzingShaderValue(Fuzzer, OutShaderAST, CurrentDepth + 1);

		return Tex;
	}
	else if (Decider < 0.98 && OutShaderAST->GetNumVariablesInScope() > 0)
	{
		// Variable
		auto* ReadVar = OutShaderAST->AllocateNode<FuzzShaderReadVariable>();
		
		int RandomVarIndex = Fuzzer->GetIntInRange(0, OutShaderAST->GetNumVariablesInScope() - 1);
		
		ReadVar->VariableName = OutShaderAST->GetNthVariableInScope(RandomVarIndex);

		return ReadVar;
	}
	else
	{
		// Literal
		auto* Literal = OutShaderAST->AllocateNode<FuzzShaderLiteral>();

		for (int i = 0; i < 4; i++)
		{
			Literal->Values[i] = Fuzzer->GetFloat01();
		}

		return Literal;
	}
}

std::string GetRandomShaderVariableName(ShaderFuzzingState* Fuzzer, const char* Prefix)
{
	// TODO: We might want to add more bits just to avoid any birthday attacks...I mean it's not cryptography but still would
	// be a false positive in fuzzing so yanno
	char Buffer[256] = {};
	int a = Fuzzer->GetIntInRange(0, 64 * 1024);
	int b = Fuzzer->GetIntInRange(0, 64 * 1024);
	int c = Fuzzer->GetIntInRange(0, 64 * 1024);
	snprintf(Buffer, sizeof(Buffer), "%s_%d_%d_%d", Prefix, a, b, c);

	return Buffer;
}

FuzzShaderAssignment* GenerateFuzzingShaderAssignment(ShaderFuzzingState* Fuzzer, FuzzShaderAST* OutShaderAST)
{
	auto* NewStmt = OutShaderAST->AllocateNode<FuzzShaderAssignment>();

	NewStmt->VariableName = GetRandomShaderVariableName(Fuzzer, "tempvar");
	NewStmt->Value = GenerateFuzzingShaderValue(Fuzzer, OutShaderAST, 0);

	OutShaderAST->VariablesInScope.back().emplace(NewStmt->VariableName, NewStmt);

	return NewStmt;
}

void GenerateResourceBindingForShader(ShaderFuzzingState* Fuzzer, FuzzShaderAST* OutShaderAST)
{
	int32 NumRootConstants = Fuzzer->GetIntInRange(0, 4);
	int32 NumRootCBVs = Fuzzer->GetIntInRange(0, 6);
	int32 NumBoundTextures = Fuzzer->GetIntInRange(0, 4);
	
	for (int32 i = 0; i < NumRootConstants; i++)
	{
		FuzzShaderRootConstants Constants;
		Constants.ConstantCount = 1; // TODO:
		Constants.SlotIndex = i;
		Constants.VarName = GetRandomShaderVariableName(Fuzzer, "root_inline");

		OutShaderAST->VariablesInScope.back().emplace(Constants.VarName, nullptr);

		OutShaderAST->RootConstants.push_back(Constants);
	}

	for (int32 i = 0; i < NumRootCBVs; i++)
	{
		FuzzShaderRootCBV CBVBind;
		CBVBind.ConstantCount = Fuzzer->GetIntInRange(1, 6); // TODO:
		CBVBind.SlotIndex = NumRootConstants + i;
		CBVBind.VarName = GetRandomShaderVariableName(Fuzzer, "root_cbv");

		for (int32 VarIdx = 0; VarIdx < CBVBind.ConstantCount; VarIdx++)
		{
			char FinalName[1024];
			snprintf(FinalName, sizeof(FinalName), "%s_var%d", CBVBind.VarName.c_str(), VarIdx);
			OutShaderAST->VariablesInScope.back().emplace(FinalName, nullptr);
		}

		OutShaderAST->RootCBVs.push_back(CBVBind);
	}

	for (int32 i = 0; i < NumBoundTextures; i++)
	{
		FuzzShaderTextureBinding TextureBinding;
		TextureBinding.ResourceName = GetRandomShaderVariableName(Fuzzer, "tex");
		TextureBinding.SamplerName = GetRandomShaderVariableName(Fuzzer, "sampler");
		TextureBinding.SlotIndex = i;

		OutShaderAST->BoundTextures.push_back(TextureBinding);
	}
}

void GenerateFuzzingShader(ShaderFuzzingState* Fuzzer, FuzzShaderAST* OutShaderAST)
{
	OutShaderAST->VariablesInScope.emplace_back();

	GenerateResourceBindingForShader(Fuzzer, OutShaderAST);

	if (OutShaderAST->Type == D3DShaderType::Pixel)
	{
		for (const auto& Var : OutShaderAST->InterStageVars)
		{
			// HACK: Referencing the struct I guess
			OutShaderAST->VariablesInScope.back().emplace(std::string("input.") + Var.VarName, nullptr);
		}
	}
	else if (OutShaderAST->Type == D3DShaderType::Vertex)
	{
		for (const auto& Var : OutShaderAST->IAVars)
		{
			// HACK: Referencing the struct I guess
			OutShaderAST->VariablesInScope.back().emplace(std::string("input.") + Var.VarName, nullptr);
		}
	}
	// TODO: Compute...idk...
	else
	{
		assert(false && "segsdg");
	}

	auto* RootNode = OutShaderAST->AllocateNode<FuzzShaderStatementBlock>();
	OutShaderAST->RootASTNode = RootNode;

	//----------------------------------------------------------

	OutShaderAST->VariablesInScope.emplace_back();

	int NumRootStatements = Fuzzer->GetIntInRange(4, 200);

	for (int i = 0; i < NumRootStatements; i++)
	{
		float Decider = Fuzzer->GetFloat01();

		if (Decider < 0.8f || true)
		{
			auto* NewStmt = GenerateFuzzingShaderAssignment(Fuzzer, OutShaderAST);
			RootNode->Statements.push_back(NewStmt);
		}
		else
		{
			assert(false && "asdasdgf");
			// TODO: Recursion
			//auto* NewBlock = OutShaderAST->AllocateNode<FuzzShaderStatementBlock>();
		}

		//RootNode->Statements.push_back();
	}

	//---
	// TODO: This will be a pain to explicate from the above, which could be a generic block statement code...
	{
		if (OutShaderAST->Type == D3DShaderType::Pixel)
		{
			auto* NewStmt = GenerateFuzzingShaderAssignment(Fuzzer, OutShaderAST);
			NewStmt->IsPredeclared = true;
			NewStmt->VariableName = "result";
			RootNode->Statements.push_back(NewStmt);
		}
		else if (OutShaderAST->Type == D3DShaderType::Vertex)
		{
			for (const auto& Var : OutShaderAST->InterStageVars)
			{
				// HACK: Make sure the variable doesn't persist
				OutShaderAST->VariablesInScope.emplace_back();
				auto* NewStmt = GenerateFuzzingShaderAssignment(Fuzzer, OutShaderAST);
				OutShaderAST->VariablesInScope.pop_back();

				NewStmt->IsPredeclared = true;
				NewStmt->VariableName = std::string("result.") + Var.VarName;

				// If we want to ensure better pixel coverage
				if (Fuzzer->Config->EnsureBetterPixelCoverage != 0 && Var.Semantic == ShaderSemantic::SV_POSITION)
				{
					auto PositionIAVar = OutShaderAST->IAVars[0];
					// NOTE: We assume the 0th IA var is position. We could do a search, since we know it's here, but for now w/e
					ASSERT(PositionIAVar.Semantic == ShaderSemantic::POSITION);

					auto* OuterAdd = OutShaderAST->AllocateNode<FuzzShaderBinaryOperator>();
					OuterAdd->Op = FuzzShaderBinaryOperator::Operator::Add;

					auto* IAVarRead = OutShaderAST->AllocateNode<FuzzShaderReadVariable>();
					IAVarRead->VariableName = std::string("input.") + PositionIAVar.VarName;
					OuterAdd->LHS = IAVarRead;

					auto* InnerMul = OutShaderAST->AllocateNode<FuzzShaderBinaryOperator>();
					InnerMul->Op = FuzzShaderBinaryOperator::Operator::Multiply;
					InnerMul->LHS = NewStmt->Value;

					auto* SmallScale = OutShaderAST->AllocateNode<FuzzShaderLiteral>();
					SmallScale->Values[0] = 0.00001f;
					SmallScale->Values[1] = 0.00001f;
					SmallScale->Values[2] = 1.0f;
					SmallScale->Values[3] = 1.0f;
					InnerMul->RHS = SmallScale;

					OuterAdd->RHS = InnerMul;

					NewStmt->Value = OuterAdd;
				}

				RootNode->Statements.push_back(NewStmt);
			}
		}
	}
	//---

	OutShaderAST->VariablesInScope.pop_back();

	//--------------------------------------

	OutShaderAST->VariablesInScope.pop_back();

	assert(OutShaderAST->VariablesInScope.size() == 0);
}

#define AST_SOURCE_LIMIT (16 * 1024)

void ConvertShaderASTNodeToSourceCode(FuzzShaderAST* ShaderAST, FuzzShaderASTNode* Node, StringBuffer* StrBuf, ShaderFuzzConfig* Config)
{
	// TODO: Do Maybe cast construction w/ templates or w/e? Since we have the static type stuff
	if (Node->Type == FuzzShaderASTNode::NodeType::Assignment)
	{
		auto* Assnmt = static_cast<FuzzShaderAssignment*>(Node);

		StrBuf->AppendFormat("\t%s%s = ", (Assnmt->IsPredeclared ? "" : "float4 "), Assnmt->VariableName.c_str());
		ConvertShaderASTNodeToSourceCode(ShaderAST, Assnmt->Value, StrBuf, Config);
		StrBuf->AppendFormat(";\n");
	}
	else if (Node->Type == FuzzShaderASTNode::NodeType::BinaryOperator)
	{
		auto* Bin = static_cast<FuzzShaderBinaryOperator*>(Node);

		StrBuf->AppendFormat("(");
		ConvertShaderASTNodeToSourceCode(ShaderAST, Bin->LHS, StrBuf, Config);

		if (Bin->Op == FuzzShaderBinaryOperator::Operator::Add)
		{
			StrBuf->AppendFormat(" + ");
		}
		else if (Bin->Op == FuzzShaderBinaryOperator::Operator::Subtract)
		{
			StrBuf->AppendFormat(" - ");
		}
		else if (Bin->Op == FuzzShaderBinaryOperator::Operator::Multiply)
		{
			StrBuf->AppendFormat(" * ");
		}
		else if (Bin->Op == FuzzShaderBinaryOperator::Operator::Divide)
		{
			StrBuf->AppendFormat(" / ");
		}
		else
		{
			assert(false && "skjdfjk");
		}

		ConvertShaderASTNodeToSourceCode(ShaderAST, Bin->RHS, StrBuf, Config);
		StrBuf->AppendFormat(")");
	}
	else if (Node->Type == FuzzShaderASTNode::NodeType::TextureAccess)
	{
		auto* Tex = static_cast<FuzzShaderTextureAccess*>(Node);

		// TODO: SampleLevel is required for vertex shader, but Sample() could be done in Pixel shaders
		StrBuf->AppendFormat("(%s.SampleLevel(%s, (", Tex->TextureName.c_str(), Tex->SamplerName.c_str());
		ConvertShaderASTNodeToSourceCode(ShaderAST, Tex->UV, StrBuf, Config);
		StrBuf->Append(").xy, 0))");

		//MyTexture.Sample(MySampler, UV)
	}
	else if (Node->Type == FuzzShaderASTNode::NodeType::ReadVariable)
	{
		auto* ReadVar = static_cast<FuzzShaderReadVariable*>(Node);
		StrBuf->Append(ReadVar->VariableName.c_str());
	}
	else if (Node->Type == FuzzShaderASTNode::NodeType::Literal)
	{
		auto* Lit = static_cast<FuzzShaderLiteral*>(Node);
		StrBuf->AppendFormat("float4(%f, %f, %f, %f)", Lit->Values[0], Lit->Values[1], Lit->Values[2], Lit->Values[3]);
	}
	else if (Node->Type == FuzzShaderASTNode::NodeType::FuncCall)
	{
		auto* FuncCall = static_cast<FuzzShaderFuncCall*>(Node);
		int32 OutputSize = FuncCall->OutputSize;
		ASSERT(OutputSize <= 4);

		if (OutputSize < 4)
		{
			StrBuf->Append("float4(");
		}

		StrBuf->AppendFormat("%s(", FuncCall->FuncName.c_str());
		for (int32 i = 0; i < FuncCall->Arguments.size(); i++)
		{
			if (i > 0)
			{
				StrBuf->Append(", ");
			}
			ConvertShaderASTNodeToSourceCode(ShaderAST, FuncCall->Arguments[i], StrBuf, Config);
		}
		StrBuf->Append(")");

		if (OutputSize < 4)
		{
			for (int32 i = OutputSize; i < 4; i++)
			{
				StrBuf->Append(", 1.0");
			}
			StrBuf->Append(")");
		}

	}
	else if (Node->Type == FuzzShaderASTNode::NodeType::StatementBlock)
	{
		auto* Block = static_cast<FuzzShaderStatementBlock*>(Node);
		StrBuf->AppendFormat("{\n");

		// TODO: Should be in AST generation, not here
		if (Node == ShaderAST->RootASTNode)
		{
			if (ShaderAST->Type == D3DShaderType::Vertex)
			{
				StrBuf->AppendFormat("\tPSInput result;\n");
			}
			else if (ShaderAST->Type == D3DShaderType::Pixel)
			{
				StrBuf->AppendFormat("\tfloat4 result;\n");
			}
		}

		for (auto Stmt : Block->Statements)
		{
			ConvertShaderASTNodeToSourceCode(ShaderAST, Stmt, StrBuf, Config);
		}

		// TODO: Should be in AST generation, not here
		if (Node == ShaderAST->RootASTNode)
		{
			// TODO: This should really be in AST generation, though we can't handle non-float4 types
			// I don't esp. like dragging the config in here... :/
			if (ShaderAST->Type == D3DShaderType::Pixel && (Config->ForcePixelOutputAlphaToOne != 0))
			{
				StrBuf->Append("\tresult.a = 1.0f;\n");
			}

			StrBuf->AppendFormat("\treturn result;\n");
		}

		StrBuf->AppendFormat("}\n");
	}
	else
	{
		ASSERT(false && "sdfljbasgfdjk");
	}
}

void ConvertShaderASTToSourceCode(FuzzShaderAST* ShaderAST, ShaderFuzzConfig* Config)
{
	// TODO: Maybe move this to heap and make it dynamic, idk
	StringBuffer StrBuf(1024 * 1024);

	for (const auto& RootConstant : ShaderAST->RootConstants)
	{
		StrBuf.AppendFormat("float4 %s;\n", RootConstant.VarName.c_str());
	}

	for (const auto& RootCBV : ShaderAST->RootCBVs)
	{
		StrBuf.AppendFormat("cbuffer %s {\n", RootCBV.VarName.c_str());
		for (int32 VarIdx = 0; VarIdx < RootCBV.ConstantCount; VarIdx++)
		{
			StrBuf.AppendFormat("\tfloat4 %s_var%d;\n", RootCBV.VarName.c_str(), VarIdx);
		}
		StrBuf.Append("};\n");
	}

	for (const auto& TextureBind : ShaderAST->BoundTextures)
	{
		StrBuf.AppendFormat("Texture2D %s;\n", TextureBind.ResourceName.c_str());
		StrBuf.AppendFormat("SamplerState %s;\n", TextureBind.SamplerName.c_str());
	}


	if (ShaderAST->Type == D3DShaderType::Vertex)
	{
		StrBuf.Append("struct VSInput {\n");
		for (const auto& Var : ShaderAST->IAVars)
		{
			StrBuf.AppendFormat("float4 %s : %s;\n", Var.VarName.c_str(), GetSemanticNameFromSemantic(Var.Semantic));
		}
		StrBuf.Append("};\n");
	}

	// VS and PS both need to know this
	{
		StrBuf.Append("struct PSInput {\n");
		for (const auto& Var : ShaderAST->InterStageVars)
		{
			StrBuf.AppendFormat("float4 %s : %s;\n", Var.VarName.c_str(), GetSemanticNameFromSemantic(Var.Semantic));
		}
		StrBuf.Append("};\n");
	}

	if (ShaderAST->Type == D3DShaderType::Vertex)
	{
		StrBuf.Append("PSInput Main(VSInput input)\n");
	}
	else if (ShaderAST->Type == D3DShaderType::Pixel)
	{
		StrBuf.Append("float4 Main(PSInput input) : SV_TARGET\n");
	}
	else
	{
		assert(false && "afsdgf");
	}

	ConvertShaderASTNodeToSourceCode(ShaderAST, ShaderAST->RootASTNode, &StrBuf, Config);

	StrBuf.Append("\n");


	ShaderAST->SourceCode = StrBuf.buffer;
}

void VerifyShaderCompilation(FuzzShaderAST* ShaderAST)
{
	char ShaderSourceName[256] = {};
	snprintf(ShaderSourceName, sizeof(ShaderSourceName), "<SHADER_FUZZ_FILE>");

	ShaderAST->ByteCodeBlob = CompileShaderCode(ShaderAST->SourceCode.c_str(), ShaderAST->Type, ShaderSourceName, "Main", &ShaderAST->ShaderMeta);
}

struct RootSigResourceDesc
{
	struct CBVDesc {
		int32 RootSigSlot = 0;
		int32 BufferSize = 0;
	};

	struct TexDesc {
		int32 RootSigSlot = 0;
	};

	std::vector<CBVDesc> CBVDescs;
	std::vector<TexDesc> TexDescs;

	void AddCBVDesc(int32 RootSigSlot, int32 BufferSize)
	{
		CBVDesc Desc;
		Desc.RootSigSlot = RootSigSlot;
		Desc.BufferSize = BufferSize;
		CBVDescs.push_back(Desc);
	}

	void AddTexDesc(int32 RootSigSlot)
	{
		TexDesc Desc;
		Desc.RootSigSlot = RootSigSlot;
		TexDescs.push_back(Desc);
	}
};


ID3D12RootSignature* CreateGraphicsRootSignatureFromVertexShaderMeta(ShaderFuzzingState* Fuzzer, FuzzShaderAST* VertexShader, FuzzShaderAST* PixelShader, RootSigResourceDesc* OutRootSigResDesc)
{
	D3D12_ROOT_SIGNATURE_DESC RootSigDesc = {};
	RootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	std::vector<D3D12_ROOT_PARAMETER> RootParams;
	std::vector<D3D12_STATIC_SAMPLER_DESC> RootStaticSamplers;

	// TODO
	std::vector<D3D12_DESCRIPTOR_RANGE> DescriptorRanges;

	{
		int32 NumVertexSRVs = VertexShader->ShaderMeta.NumSRVs;
		int32 NumPixelSRVs = PixelShader->ShaderMeta.NumSRVs;
		int32 NumVertexCBVs = VertexShader->ShaderMeta.NumCBVs;
		int32 NumPixelCBVs = PixelShader->ShaderMeta.NumCBVs;

		int32 NumSRVs = max(NumPixelSRVs, NumVertexSRVs);
		int32 NumCBVs = max(NumPixelCBVs, NumVertexCBVs);

		int32 TotalRootParams = NumSRVs + NumCBVs;
		RootParams.resize(TotalRootParams);

		DescriptorRanges.resize(TotalRootParams);

		int32 RootParamIdx = 0;
		for (int32 SRVIdx = 0; SRVIdx < NumSRVs; SRVIdx++)
		{
			RootParams[RootParamIdx].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			RootParams[RootParamIdx].DescriptorTable.NumDescriptorRanges = 1;

			D3D12_DESCRIPTOR_RANGE& pDescriptorRange = DescriptorRanges[SRVIdx];
			pDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			pDescriptorRange.BaseShaderRegister = SRVIdx;
			pDescriptorRange.NumDescriptors = 1;
			pDescriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

			RootParams[RootParamIdx].DescriptorTable.pDescriptorRanges = &pDescriptorRange;
			if (SRVIdx < NumVertexSRVs && SRVIdx < NumPixelSRVs)
			{
				RootParams[RootParamIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			}
			else if (SRVIdx < NumVertexSRVs)
			{
				RootParams[RootParamIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
			}
			else if (SRVIdx < NumPixelSRVs)
			{
				RootParams[RootParamIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			}
			else
			{
				assert(false && "asjsfauk");
			}

			OutRootSigResDesc->AddTexDesc(RootParamIdx);

			RootParamIdx++;
		}

		for (int32 CBVIdx = 0; CBVIdx < NumCBVs; CBVIdx++)
		{
			RootParams[RootParamIdx].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
			RootParams[RootParamIdx].Descriptor.RegisterSpace = 0;
			RootParams[RootParamIdx].Descriptor.ShaderRegister = CBVIdx;
			
			int32 CBVSize = 0;

			if (CBVIdx < NumVertexCBVs && CBVIdx < NumPixelCBVs)
			{
				RootParams[RootParamIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
				CBVSize = max(VertexShader->ShaderMeta.CBVSizes[CBVIdx], PixelShader->ShaderMeta.CBVSizes[CBVIdx]);
			}
			else if (CBVIdx < NumVertexCBVs)
			{
				RootParams[RootParamIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
				CBVSize = VertexShader->ShaderMeta.CBVSizes[CBVIdx];
			}
			else if (CBVIdx < NumPixelCBVs)
			{
				RootParams[RootParamIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
				CBVSize = PixelShader->ShaderMeta.CBVSizes[CBVIdx];
			}
			else
			{
				assert(false && "asjsfauk");
			}

			OutRootSigResDesc->AddCBVDesc(RootParamIdx, CBVSize);

			RootParamIdx++;
		}

		int32 NumVertexSamplers = VertexShader->ShaderMeta.NumStaticSamplers;
		int32 NumPixelSamplers = PixelShader->ShaderMeta.NumStaticSamplers;
		int32 TotalStaticSamplers = max(NumVertexSamplers, NumPixelSamplers);
		RootStaticSamplers.resize(TotalStaticSamplers);

		//LOG("    VS=(%d CBV, %d SRV, %d Samplers)", NumVertexCBVs, NumVertexSRVs, NumVertexSamplers);
		//LOG("    PS=(%d CBV, %d SRV, %d Samplers)", NumPixelCBVs, NumPixelSRVs, NumPixelSamplers);

		for (int32 SamplerIdx = 0; SamplerIdx < TotalStaticSamplers; SamplerIdx++)
		{
			static const D3D12_FILTER TextureFilters[] = {
				D3D12_FILTER_MIN_MAG_MIP_POINT,
				D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,
				D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
				D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR,
				D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT,
				D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
				D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
				D3D12_FILTER_MIN_MAG_MIP_LINEAR,
				D3D12_FILTER_ANISOTROPIC,
			};

			static const D3D12_TEXTURE_ADDRESS_MODE TextureAddrModes[] = {
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,
				D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_BORDER,
				D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE
			};

			RootStaticSamplers[SamplerIdx].Filter = TextureFilters[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(TextureFilters) - 1)];
			RootStaticSamplers[SamplerIdx].AddressU = TextureAddrModes[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(TextureAddrModes) - 1)];;
			RootStaticSamplers[SamplerIdx].AddressV = TextureAddrModes[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(TextureAddrModes) - 1)];;
			RootStaticSamplers[SamplerIdx].AddressW = TextureAddrModes[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(TextureAddrModes) - 1)];;
			RootStaticSamplers[SamplerIdx].MipLODBias = 0;
			RootStaticSamplers[SamplerIdx].MaxAnisotropy = 0;
			RootStaticSamplers[SamplerIdx].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
			RootStaticSamplers[SamplerIdx].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
			RootStaticSamplers[SamplerIdx].MinLOD = 0.0f;
			RootStaticSamplers[SamplerIdx].MaxLOD = D3D12_FLOAT32_MAX;
			RootStaticSamplers[SamplerIdx].ShaderRegister = SamplerIdx;
			RootStaticSamplers[SamplerIdx].RegisterSpace = 0;

			if (SamplerIdx < NumVertexSamplers && SamplerIdx < NumPixelSamplers)
			{
				RootStaticSamplers[SamplerIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			}
			else if (SamplerIdx < NumVertexSamplers)
			{
				RootStaticSamplers[SamplerIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
			}
			else if (SamplerIdx < NumPixelSamplers)
			{
				RootStaticSamplers[SamplerIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			}
			else
			{
				assert(false && "asjsfauk");
			}
		}
	}

	RootSigDesc.NumParameters = RootParams.size();
	RootSigDesc.pParameters = RootParams.data();

	RootSigDesc.NumStaticSamplers = RootStaticSamplers.size();
	RootSigDesc.pStaticSamplers = RootStaticSamplers.data();

	ID3DBlob* RootSigBlob = nullptr;
	ID3DBlob* RootSigErrorBlob = nullptr;

	HRESULT hr = D3D12SerializeRootSignature(&RootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &RootSigBlob, &RootSigErrorBlob);

	if (!SUCCEEDED(hr))
	{
		const char* ErrStr = (const char*)RootSigErrorBlob->GetBufferPointer();
		int32 ErrStrLen = RootSigErrorBlob->GetBufferSize();
		LOG("Root Sig Err: '%.*s'", ErrStrLen, ErrStr);
	}

	ASSERT(SUCCEEDED(hr));

	ID3D12RootSignature* RootSig = nullptr;
	hr = Fuzzer->D3DDevice->CreateRootSignature(0, RootSigBlob->GetBufferPointer(), RootSigBlob->GetBufferSize(), IID_PPV_ARGS(&RootSig));

	ASSERT(SUCCEEDED(hr));

	return RootSig;
}

// TODO: Random, taking FuzzerState
static D3D12_RASTERIZER_DESC GetDefaultRasterizerDesc()
{
	D3D12_RASTERIZER_DESC Desc = {};
	Desc.FillMode = D3D12_FILL_MODE_SOLID;
	Desc.CullMode = D3D12_CULL_MODE_BACK;
	Desc.FrontCounterClockwise = FALSE;
	Desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	Desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	Desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	Desc.DepthClipEnable = TRUE;
	Desc.MultisampleEnable = FALSE;
	Desc.AntialiasedLineEnable = FALSE;
	Desc.ForcedSampleCount = 0;
	Desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	return Desc;
}

static D3D12_RASTERIZER_DESC GetFuzzRasterizerDesc(ShaderFuzzingState* Fuzzer)
{
	D3D12_RASTERIZER_DESC Desc = {};

	static const D3D12_FILL_MODE FillModes[] = {
		D3D12_FILL_MODE_WIREFRAME,
		D3D12_FILL_MODE_SOLID
	};

	static const D3D12_CULL_MODE CullModes[] = {
		D3D12_CULL_MODE_NONE,
		D3D12_CULL_MODE_FRONT,
		D3D12_CULL_MODE_BACK
	};

	static const D3D12_CONSERVATIVE_RASTERIZATION_MODE ConservativeRasterModes[] = {
		D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
		D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON
	};

	Desc.FillMode = FillModes[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(FillModes) - 1)];
	Desc.CullMode = CullModes[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(CullModes) - 1)];
	Desc.FrontCounterClockwise = (Fuzzer->GetFloat01() > 0.5f);
	Desc.DepthBias = Fuzzer->GetFloat01();
	Desc.DepthBiasClamp = Fuzzer->GetFloat01();
	Desc.SlopeScaledDepthBias = Fuzzer->GetFloat01();
	Desc.DepthClipEnable = TRUE;
	Desc.MultisampleEnable = FALSE;
	Desc.AntialiasedLineEnable = FALSE;
	Desc.ForcedSampleCount = 0;
	Desc.ConservativeRaster = ConservativeRasterModes[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(ConservativeRasterModes) - 1)];

	if (Fuzzer->Config->AllowConservativeRasterization == 0)
	{
		Desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	}

	// D3D12 ERROR: ID3D12Device::CreateRasterizerState: FillMode must be D3D12_FILL_MODE_SOLID when ConservativeRaster is D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON: FillMode = D3D12_FILL_MODE_WIREFRAME, ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON. [ STATE_CREATION ERROR #95: CREATERASTERIZERSTATE_INVALIDFILLMODE]
	if (Desc.ConservativeRaster == D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON)
	{
		Desc.FillMode = D3D12_FILL_MODE_SOLID;
	}

	// If we're trying to get good pixel shader coverage, then make sure we do solid not wireframe,
	// and turn off culling
	if (Fuzzer->Config->EnsureBetterPixelCoverage)
	{
		Desc.FillMode = D3D12_FILL_MODE_SOLID;
		Desc.CullMode = D3D12_CULL_MODE_NONE;
	}

	return Desc;
}

// TODO: Random, taking FuzzerState
static D3D12_BLEND_DESC GetDefaultBlendStateDesc() {
	D3D12_BLEND_DESC Desc = {};
	Desc.AlphaToCoverageEnable = FALSE;
	Desc.IndependentBlendEnable = FALSE;

	const D3D12_RENDER_TARGET_BLEND_DESC DefaultRenderTargetBlendDesc =
	{
		FALSE,FALSE,
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_LOGIC_OP_NOOP,
		D3D12_COLOR_WRITE_ENABLE_ALL,
	};

	for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
		Desc.RenderTarget[i] = DefaultRenderTargetBlendDesc;
	}

	return Desc;
}

static D3D12_BLEND_DESC GetFuzzBlendStateDesc(ShaderFuzzingState* Fuzzer) {
	// TODO
	D3D12_BLEND_DESC Desc = {};
	Desc.AlphaToCoverageEnable = FALSE;
	Desc.IndependentBlendEnable = FALSE;

	const int32 BlendEnabledDecider = Fuzzer->GetIntInRange(0, 2);

	const static bool IsLogicOpsEnabled = false;

	const bool BlendEnable = (BlendEnabledDecider % 2) != 0;
	const bool LogicOpEnable = IsLogicOpsEnabled && (BlendEnabledDecider / 2) != 0;

	ASSERT(!(LogicOpEnable && BlendEnable));

	const static D3D12_LOGIC_OP LogicOps[] = {
		D3D12_LOGIC_OP_CLEAR,
		D3D12_LOGIC_OP_SET,
		D3D12_LOGIC_OP_COPY,
		D3D12_LOGIC_OP_COPY_INVERTED,
		D3D12_LOGIC_OP_NOOP,
		D3D12_LOGIC_OP_INVERT,
		D3D12_LOGIC_OP_AND,
		D3D12_LOGIC_OP_NAND,
		D3D12_LOGIC_OP_OR,
		D3D12_LOGIC_OP_NOR,
		D3D12_LOGIC_OP_XOR,
		D3D12_LOGIC_OP_EQUIV,
		D3D12_LOGIC_OP_AND_REVERSE,
		D3D12_LOGIC_OP_AND_INVERTED,
		D3D12_LOGIC_OP_OR_REVERSE,
		D3D12_LOGIC_OP_OR_INVERTED
	};

	const static D3D12_BLEND BlendValuesSrcCol[] = {
		D3D12_BLEND_ZERO,
		D3D12_BLEND_ONE,
		D3D12_BLEND_SRC_COLOR,
		D3D12_BLEND_INV_SRC_COLOR,
		D3D12_BLEND_BLEND_FACTOR,
		D3D12_BLEND_INV_BLEND_FACTOR,
		D3D12_BLEND_SRC1_COLOR,
		D3D12_BLEND_INV_SRC1_COLOR
	};

	const static D3D12_BLEND BlendValuesSrcA[] = {
		D3D12_BLEND_ZERO,
		D3D12_BLEND_ONE,
		D3D12_BLEND_SRC_ALPHA,
		D3D12_BLEND_INV_SRC_ALPHA,
		D3D12_BLEND_SRC1_ALPHA,
		D3D12_BLEND_INV_SRC1_ALPHA,
		D3D12_BLEND_BLEND_FACTOR,
		D3D12_BLEND_INV_BLEND_FACTOR
	};

	const static D3D12_BLEND BlendValuesDstCol[] = {
		D3D12_BLEND_ZERO,
		D3D12_BLEND_ONE,
		D3D12_BLEND_SRC_COLOR,
		D3D12_BLEND_INV_SRC_COLOR,
		D3D12_BLEND_DEST_COLOR,
		D3D12_BLEND_INV_DEST_COLOR,
		D3D12_BLEND_BLEND_FACTOR,
		D3D12_BLEND_INV_BLEND_FACTOR
	};

	const static D3D12_BLEND BlendValuesDstA[] = {
		D3D12_BLEND_ZERO,
		D3D12_BLEND_ONE,
		D3D12_BLEND_DEST_ALPHA,
		D3D12_BLEND_INV_DEST_ALPHA,
		D3D12_BLEND_BLEND_FACTOR,
		D3D12_BLEND_INV_BLEND_FACTOR
	};

	const static D3D12_BLEND_OP BlendOps[] = {
		D3D12_BLEND_OP_ADD,
		D3D12_BLEND_OP_SUBTRACT,
		D3D12_BLEND_OP_REV_SUBTRACT,
		D3D12_BLEND_OP_MIN,
		D3D12_BLEND_OP_MAX
	};

	const static D3D12_BLEND_OP BlendOpsNoMinMax[] = {
		D3D12_BLEND_OP_ADD,
		D3D12_BLEND_OP_SUBTRACT,
		D3D12_BLEND_OP_REV_SUBTRACT
	};

	const static D3D12_COLOR_WRITE_ENABLE ColorWriteEnables[] = {
		D3D12_COLOR_WRITE_ENABLE_RED,
		D3D12_COLOR_WRITE_ENABLE_GREEN,
		D3D12_COLOR_WRITE_ENABLE_BLUE,
		D3D12_COLOR_WRITE_ENABLE_ALPHA,
		D3D12_COLOR_WRITE_ENABLE_ALL
	};

	// D3D12 ERROR : ID3D12Device::CreateBlendState : SrcBlendAlpha[0] is trying to use a D3D11_BLEND value(0xa) that manipulates color, which is invalid.[STATE_CREATION ERROR #114: CREATEBLENDSTATE_INVALIDSRCBLENDALPHA]
	// D3D12 ERROR : ID3D12Device::CreateBlendState : DestBlendAlpha[0] is trying to use a D3D11_BLEND value(0x11) that manipulates color, which is invalid.[STATE_CREATION ERROR #115: CREATEBLENDSTATE_INVALIDDESTBLENDALPHA]

	// D3D12 ERROR : ID3D12Device::CreateBlendState : DestBlendAlpha[0] is trying to use a D3D11_BLEND value(0xa) that manipulates color, which is invalid.[STATE_CREATION ERROR #115: CREATEBLENDSTATE_INVALIDDESTBLENDALPHA]
	// D3D12 ERROR : ID3D12Device::CreateBlendState : MIN or MAX are invalid for BlendOpAlpha when Dual - Source blending.[STATE_CREATION ERROR #116: CREATEBLENDSTATE_INVALIDBLENDOPALPHA]

	D3D12_RENDER_TARGET_BLEND_DESC DefaultRenderTargetBlendDesc =
	{
		BlendEnable,LogicOpEnable,
		BlendValuesSrcCol[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(BlendValuesSrcCol) - 1)], BlendValuesDstCol[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(BlendValuesDstCol) - 1)], BlendOps[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(BlendOps) - 1)],
		BlendValuesSrcA[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(BlendValuesSrcA) - 1)], BlendValuesDstA[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(BlendValuesDstA) - 1)], BlendOps[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(BlendOps) - 1)],
		LogicOps[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(LogicOps) - 1)],
		// These are flags, so we actually want to have any combination of the bits
		Fuzzer->GetIntInRange(1, D3D12_COLOR_WRITE_ENABLE_ALL),
	};

	// Cannot use min-max if we have multiple sources
	if (DefaultRenderTargetBlendDesc.SrcBlend == D3D12_BLEND_SRC1_COLOR || DefaultRenderTargetBlendDesc.SrcBlend == D3D12_BLEND_INV_SRC1_COLOR)
	{
		DefaultRenderTargetBlendDesc.BlendOp = BlendOpsNoMinMax[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(BlendOpsNoMinMax) - 1)];
	}

	if (DefaultRenderTargetBlendDesc.SrcBlendAlpha == D3D12_BLEND_SRC1_ALPHA || DefaultRenderTargetBlendDesc.SrcBlendAlpha == D3D12_BLEND_INV_SRC1_ALPHA)
	{
		DefaultRenderTargetBlendDesc.BlendOpAlpha = BlendOpsNoMinMax[Fuzzer->GetIntInRange(0, ARRAY_COUNTOF(BlendOpsNoMinMax) - 1)];
	}

	for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
		Desc.RenderTarget[i] = DefaultRenderTargetBlendDesc;
	}

	return Desc;
}

void FillOutPSOInputElements(std::vector<D3D12_INPUT_ELEMENT_DESC>* OutInputElementDescs, FuzzShaderAST* VertexShader)
{
	OutInputElementDescs->empty();
	OutInputElementDescs->reserve(VertexShader->ShaderMeta.NumParams);

	for (int32 IAVarIdx = 0; IAVarIdx < VertexShader->ShaderMeta.NumParams; IAVarIdx++)
	{
		auto IAVarMetadata = VertexShader->ShaderMeta.InputParamMetadata[IAVarIdx];

		D3D12_INPUT_ELEMENT_DESC InputElement = {};
		InputElement.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		// TODO: Assumes every param is 16 bytes, which...fair assumption given above
		InputElement.AlignedByteOffset = 0;// Var.ParamIdx * 16;
		InputElement.InputSlot = IAVarMetadata.ParamIndex;
		InputElement.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		InputElement.SemanticName = GetSemanticNameFromSemantic(IAVarMetadata.Semantic);
		InputElement.SemanticIndex = IAVarMetadata.SemanticIndex;

		OutInputElementDescs->push_back(InputElement);
	}
}


void VerifyGraphicsPSOCompilation(ShaderFuzzingState* Fuzzer, FuzzShaderAST* VertexShader, FuzzShaderAST* PixelShader, ID3D12RootSignature** OutRootSig, ID3D12PipelineState** OutPSO, RootSigResourceDesc* OutRootSigDesc)
{
	// Determine root signature
	ID3D12RootSignature* RootSig = CreateGraphicsRootSignatureFromVertexShaderMeta(Fuzzer, VertexShader, PixelShader, OutRootSigDesc);

	// Create PSO description
	std::vector<D3D12_INPUT_ELEMENT_DESC> InputElementDescs;
	//{
	//	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	//	{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	//};

	FillOutPSOInputElements(&InputElementDescs, VertexShader);

	D3D12_SHADER_BYTECODE VertexShaderByteCode;
	VertexShaderByteCode.pShaderBytecode = VertexShader->ByteCodeBlob->GetBufferPointer();
	VertexShaderByteCode.BytecodeLength = VertexShader->ByteCodeBlob->GetBufferSize();

	D3D12_SHADER_BYTECODE PixelShaderByteCode;
	PixelShaderByteCode.pShaderBytecode = PixelShader->ByteCodeBlob->GetBufferPointer();
	PixelShaderByteCode.BytecodeLength = PixelShader->ByteCodeBlob->GetBufferSize();

	D3D12_GRAPHICS_PIPELINE_STATE_DESC PSODesc = {};
	PSODesc.InputLayout = { InputElementDescs.data(), (UINT)InputElementDescs.size() };
	PSODesc.pRootSignature = RootSig;
	PSODesc.VS = VertexShaderByteCode;
	PSODesc.PS = PixelShaderByteCode;
	PSODesc.RasterizerState = GetFuzzRasterizerDesc(Fuzzer);
	PSODesc.BlendState = GetFuzzBlendStateDesc(Fuzzer);

	if (Fuzzer->Config->DisableBlendingState != 0)
	{
		PSODesc.BlendState = GetDefaultBlendStateDesc();
	}

	PSODesc.DepthStencilState.DepthEnable = FALSE;
	PSODesc.DepthStencilState.StencilEnable = FALSE;
	PSODesc.SampleMask = UINT_MAX;
	PSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	PSODesc.NumRenderTargets = 1;
	PSODesc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
	PSODesc.SampleDesc.Count = 1;

	// Compile PSO
	ID3D12PipelineState* PSO = nullptr;
	Fuzzer->D3DDevice->CreateGraphicsPipelineState(&PSODesc, IID_PPV_ARGS(&PSO));

	ASSERT(PSO != nullptr);

	*OutRootSig = RootSig;
	*OutPSO = PSO;

	// TODO: If we're drawing stuff we need to keep these around
	// and maybe we'd want to cache them, idk
	//RootSig->Release();
	//PSO->Release();
}

void VerifyComputePSOCompilation(ShaderFuzzingState* Fuzzer, FuzzShaderAST* ComputeShader)
{
	// TODO: fancy
}

void CreateInterstageVarsForVertexAndPixelShaders(ShaderFuzzingState* Fuzzer, FuzzShaderAST* VertexShader, FuzzShaderAST* PixelShader)
{
	int32 SemanticVarCounts[(int32)ShaderSemantic::Count] = {};

	// Always include Position as IA var
	{
		FuzzShaderSemanticVar PosVar;
		PosVar.ParamIdx = 0;
		PosVar.Semantic = ShaderSemantic::POSITION;
		PosVar.VarName = GetRandomShaderVariableName(Fuzzer, "iaparam");
		SemanticVarCounts[(int32)PosVar.Semantic]++;
		VertexShader->IAVars.push_back(PosVar);
	}

	int32 NumAdditionalIAVars = Fuzzer->GetIntInRange(0, 4);
	for (int32 i = 0; i < NumAdditionalIAVars; i++)
	{
		FuzzShaderSemanticVar NewVar;
		NewVar.ParamIdx = i + 1;
		NewVar.Semantic = (ShaderSemantic)Fuzzer->GetIntInRange((int32)ShaderSemantic::IA_FIRST, (int32)ShaderSemantic::IA_LAST);
		// Cannot duplicate semantics in Input assembler vars
		if (SemanticVarCounts[(int32)NewVar.Semantic] == 0)
		{
			NewVar.VarName = GetRandomShaderVariableName(Fuzzer, "iaparam");
			SemanticVarCounts[(int32)NewVar.Semantic]++;

			VertexShader->IAVars.push_back(NewVar);
		}
	}

	for (int32 i = 0; i < (int32)ShaderSemantic::Count; i++)
	{
		SemanticVarCounts[i] = 0;
	}

	// Alwyas include SV_Position as inter-stage var
	{
		FuzzShaderSemanticVar SVPosVar;
		SVPosVar.ParamIdx = 0;
		SVPosVar.Semantic = ShaderSemantic::SV_POSITION;
		SVPosVar.VarName = GetRandomShaderVariableName(Fuzzer, "param");
		SemanticVarCounts[(int32)SVPosVar.Semantic]++;
		VertexShader->InterStageVars.push_back(SVPosVar);
		PixelShader->InterStageVars.push_back(SVPosVar);
	}


	int32 NumAdditionalInterstageVars = Fuzzer->GetIntInRange(0, 4);
	for (int32 i = 0; i < NumAdditionalInterstageVars; i++)
	{
		FuzzShaderSemanticVar NewVar;
		NewVar.ParamIdx = VertexShader->InterStageVars.size();
		NewVar.Semantic = (ShaderSemantic)Fuzzer->GetIntInRange((int32)ShaderSemantic::INTER_FIRST, (int32)ShaderSemantic::INTER_LAST);
		if (SemanticVarCounts[(int32)NewVar.Semantic] == 0)
		{
			SemanticVarCounts[(int32)NewVar.Semantic]++;
			NewVar.VarName = GetRandomShaderVariableName(Fuzzer, "param");
			VertexShader->InterStageVars.push_back(NewVar);
			PixelShader->InterStageVars.push_back(NewVar);
		}
	}
}

void SetSeedOnFuzzer(ShaderFuzzingState* Fuzzer, uint64_t Seed)
{
	Fuzzer->RNGState.seed(Seed);
	Fuzzer->InitialFuzzSeed = Seed;
}

void CopyTextureResource(ID3D12GraphicsCommandList* CommandList, ID3D12Resource* TextureUploadResource, ID3D12Resource* TextureResource, int32 Width, int32 Height, int32 Pitch)
{
	D3D12_TEXTURE_COPY_LOCATION CopyLocSrc = {}, CopyLocDst = {};
	CopyLocSrc.pResource = TextureUploadResource;
	CopyLocSrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	CopyLocSrc.PlacedFootprint.Offset = 0;
	CopyLocSrc.PlacedFootprint.Footprint.Width = Width;
	CopyLocSrc.PlacedFootprint.Footprint.Height = Height;
	CopyLocSrc.PlacedFootprint.Footprint.Depth = 1;
	CopyLocSrc.PlacedFootprint.Footprint.RowPitch = Pitch;
	CopyLocSrc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	CopyLocDst.pResource = TextureResource;
	CopyLocDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	CopyLocDst.SubresourceIndex = 0;

	CommandList->CopyTextureRegion(&CopyLocDst, 0, 0, 0, &CopyLocSrc, nullptr);
}


uint64 TotalPSCalls = 0;
uint64 TotalFuzzCases = 0;

void GenerateDrawingCommandsOnCommandList(ShaderFuzzingState* Fuzzer, ID3D12GraphicsCommandList* CommandList, ID3D12PipelineState* PSO,
	ID3D12RootSignature* RootSig, RootSigResourceDesc RootSigDesc,
	const ShaderMetadata& VertMeta, const ShaderMetadata& PixelMeta,
	std::vector<uint64>& AllResourcesInUse, std::unordered_map<uint64, int32>& AllHeapsInUseAndCounts,
	std::vector<ID3D12DescriptorHeap*>& SRVDescriptorHeaps)
{
	std::vector<ResourceLifecycleManager::ResourceToTransition> BufferedResourceTransitions;

	auto TransitionResource = [&](uint64 ResID, D3D12_RESOURCE_STATES States)
	{
		ResourceLifecycleManager::ResourceToTransition ResTrans;
		ResTrans.ResourceID = ResID;
		ResTrans.NextState = States;

		BufferedResourceTransitions.push_back(ResTrans);
	};

	auto FlushResourceTransitions = [&]()
	{
		Fuzzer->D3DPersist->ResourceMgr.PerformResourceTransitions(BufferedResourceTransitions, CommandList);
		BufferedResourceTransitions.clear();
	};

	const int32 RTWidth = Fuzzer->Config->RTWidth;
	const int32 RTHeight = Fuzzer->Config->RTHeight;

	// Create resources (including backbuffer?)
	uint64 BackBufferResourceID = 0;
	ID3D12Resource* BackBufferResource = nullptr;
	{
		ResourceLifecycleManager::ResourceDesc BackBufferDesc = {};
		BackBufferDesc.NodeVisibilityMask = 0x01;
		BackBufferDesc.IsUploadHeap = false;
		BackBufferDesc.ResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		BackBufferDesc.ResDesc.Width = RTWidth;
		BackBufferDesc.ResDesc.Height = RTHeight;
		BackBufferDesc.ResDesc.DepthOrArraySize = 1;
		BackBufferDesc.ResDesc.SampleDesc.Count = 1;
		BackBufferDesc.ResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		BackBufferDesc.ResDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

		BackBufferResourceID = Fuzzer->D3DPersist->ResourceMgr.AcquireResource(BackBufferDesc, &BackBufferResource);
		AllResourcesInUse.push_back(BackBufferResourceID);

		BackBufferResource->SetName(L"Backbuffer");

		TransitionResource(BackBufferResourceID, D3D12_RESOURCE_STATE_RENDER_TARGET);
		FlushResourceTransitions();
	}

	CommandList->SetPipelineState(PSO);
	CommandList->SetGraphicsRootSignature(RootSig);

	{
		D3D12_VIEWPORT Viewport = {};
		Viewport.MinDepth = 0;
		Viewport.MaxDepth = 1;
		Viewport.TopLeftX = 0;
		Viewport.TopLeftY = 0;
		Viewport.Width = RTWidth;
		Viewport.Height = RTHeight;
		CommandList->RSSetViewports(1, &Viewport);

		D3D12_RECT ScissorRect = {};
		ScissorRect.left = 0;
		ScissorRect.right = RTWidth;
		ScissorRect.top = 0;
		ScissorRect.bottom = RTHeight;
		CommandList->RSSetScissorRects(1, &ScissorRect);

		D3D12_DESCRIPTOR_HEAP_DESC DescriptorHeapDesc = {};
		DescriptorHeapDesc.NumDescriptors = 1;
		DescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

		ID3D12DescriptorHeap* DescriptorHeap = nullptr;
		Fuzzer->D3DDevice->CreateDescriptorHeap(&DescriptorHeapDesc, IID_PPV_ARGS(&DescriptorHeap));
		// TODO: Leaking DescriptorHeap

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		Fuzzer->D3DDevice->CreateRenderTargetView(BackBufferResource, nullptr, rtvHandle);

		CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

		if (Fuzzer->Config->ShouldClearRTVBeforeCase)
		{
			float ClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
			CommandList->ClearRenderTargetView(rtvHandle, ClearColor, 0, nullptr);
		}
	}

	// Setup resources (resource transitions?)

	// These two need to correspond
	std::vector<int32> DescriptorHeapRootSigSlot;

	for (auto TexDesc : RootSigDesc.TexDescs)
	{
		const int32 TextureWidth = (1 << Fuzzer->GetIntInRange(6, 8));
		const int32 TextureHeight = TextureWidth;
		const int bpp = 4; // Assuming 32-bit format

		const int32 BufferSize = TextureWidth * TextureHeight * bpp;

		D3D12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, TextureWidth, TextureHeight, 1, 1);
		D3D12_RESOURCE_DESC UploadResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(BufferSize);

		ID3D12Resource* TextureResource = nullptr;
		ID3D12Resource* TextureUploadResource = nullptr;

		ResourceLifecycleManager::ResourceDesc ResDesc, UploadResDesc;
		ResDesc.ResDesc = ResourceDesc;
		ResDesc.IsUploadHeap = false;
		UploadResDesc.ResDesc = UploadResourceDesc;
		UploadResDesc.IsUploadHeap = true;

		uint64 ResID = 0;
		if (Fuzzer->GetFloat01() < Fuzzer->Config->PlacedResourceChance)
		{
			ResDesc.Type = ResourceLifecycleManager::ResourceType::Placed;

			uint64 HeapID = 0;
			ResID = Fuzzer->D3DPersist->ResourceMgr.AcquirePlacedResource(ResDesc, &TextureResource, &HeapID);

			AllHeapsInUseAndCounts[HeapID]++;
		}
		else
		{
			ResID = Fuzzer->D3DPersist->ResourceMgr.AcquireResource(ResDesc, &TextureResource);
		}

		uint64 UploadResID = Fuzzer->D3DPersist->ResourceMgr.AcquireResource(UploadResDesc, &TextureUploadResource);

		void* pTexturePixelData = nullptr;
		D3D12_RANGE readRange = {};        // We do not intend to read from this resource on the CPU.
		HRESULT hr = TextureUploadResource->Map(0, &readRange, &pTexturePixelData);
		ASSERT(SUCCEEDED(hr));

		SetRandomBytes(Fuzzer, pTexturePixelData, BufferSize);

		TextureUploadResource->Unmap(0, nullptr);

		TransitionResource(ResID, D3D12_RESOURCE_STATE_COPY_DEST);
		FlushResourceTransitions();

		// TODO: Resource barriers before and after
		CopyTextureResource(CommandList, TextureUploadResource, TextureResource, TextureWidth, TextureHeight, TextureWidth * bpp);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		ID3D12DescriptorHeap* TextureSRVHeap = nullptr;

		// TODO: Descriptor heap needs to go somewhere, maybe on texture?
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = 1;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		hr = Fuzzer->D3DDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&TextureSRVHeap));
		// TODO: Leaking TextureSRVHeap

		ASSERT(SUCCEEDED(hr));

		Fuzzer->D3DDevice->CreateShaderResourceView(TextureResource, &srvDesc, TextureSRVHeap->GetCPUDescriptorHandleForHeapStart());

		SRVDescriptorHeaps.push_back(TextureSRVHeap);
		DescriptorHeapRootSigSlot.push_back(TexDesc.RootSigSlot);

		AllResourcesInUse.push_back(ResID);
		AllResourcesInUse.push_back(UploadResID);

		// TODO: Compute which one?
		TransitionResource(ResID, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}

	// TODO: Set descriptor heaps and stuff
	ASSERT(SRVDescriptorHeaps.size() == DescriptorHeapRootSigSlot.size());
	for (int32 i = 0; i < SRVDescriptorHeaps.size(); i++)
	{
		//CommandList->SetDescriptorHeaps(SRVDescriptorHeaps.size(), SRVDescriptorHeaps.data());
		CommandList->SetDescriptorHeaps(1, &SRVDescriptorHeaps[i]);
		CommandList->SetGraphicsRootDescriptorTable(DescriptorHeapRootSigSlot[i], SRVDescriptorHeaps[i]->GetGPUDescriptorHandleForHeapStart());
	}

	for (auto CBVDesc : RootSigDesc.CBVDescs)
	{
		D3D12_RESOURCE_DESC CBVResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(CBVDesc.BufferSize);

		ResourceLifecycleManager::ResourceDesc CBVResDesc;
		CBVResDesc.ResDesc = CBVResourceDesc;
		CBVResDesc.IsUploadHeap = true;

		ID3D12Resource* D3DResource = nullptr;
		auto ResID = Fuzzer->D3DPersist->ResourceMgr.AcquireResource(CBVResDesc, &D3DResource);

		void* pBufferData = nullptr;
		D3D12_RANGE readRange = {};        // We do not intend to read from this resource on the CPU.
		HRESULT hr = D3DResource->Map(0, &readRange, &pBufferData);
		ASSERT(SUCCEEDED(hr));

		if (Fuzzer->Config->CBVUploadRandomFloatData != 0)
		{
			float* pFloatData = (float*)pBufferData;
			for (int32 i = 0; i < CBVDesc.BufferSize / 4; i++)
			{
				pFloatData[i] = Fuzzer->GetFloatInRange(-100.0f, 100.0f);
			}
		}
		else
		{
			SetRandomBytes(Fuzzer, pBufferData, CBVDesc.BufferSize);
		}

		D3DResource->Unmap(0, nullptr);

		CommandList->SetGraphicsRootConstantBufferView(CBVDesc.RootSigSlot, D3DResource->GetGPUVirtualAddress());

		//TransitionResource(ResID, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

		AllResourcesInUse.push_back(ResID);
	}

	// Set resources in cmd list and IA vertex stuff

	const int32 VertexCount = Fuzzer->GetIntInRange(30, 100);

	for (int32 IAParamIdx = 0; IAParamIdx < VertMeta.NumParams; IAParamIdx++)
	{
		auto ParamMeta = VertMeta.InputParamMetadata[IAParamIdx];
		int32 BufferSize = VertexCount * 16;

		D3D12_RESOURCE_DESC VertResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(BufferSize);

		ResourceLifecycleManager::ResourceDesc VertDesc;
		VertDesc.ResDesc = VertResourceDesc;
		VertDesc.IsUploadHeap = true;

		ID3D12Resource* VertResource = nullptr;
		uint64 ResID = Fuzzer->D3DPersist->ResourceMgr.AcquireResource(VertDesc, &VertResource);

		void* pVertData = nullptr;
		D3D12_RANGE readRange = {};        // We do not intend to read from this resource on the CPU.
		HRESULT hr = VertResource->Map(0, &readRange, &pVertData);
		ASSERT(SUCCEEDED(hr));

		float* pFloatData = (float*)pVertData;
		if (ParamMeta.Semantic == ShaderSemantic::POSITION)
		{
			for (int32 i = 0; i < 4 * VertexCount; i += 4)
			{
				pFloatData[i + 0] = Fuzzer->GetFloatInRange(-1.0f, 1.0f);
				pFloatData[i + 1] = Fuzzer->GetFloatInRange(-1.0f, 1.0f);
				pFloatData[i + 2] = Fuzzer->GetFloat01();
				pFloatData[i + 3] = Fuzzer->GetFloat01();
			}
		}
		else
		{
			for (int32 i = 0; i < 4 * VertexCount; i++)
			{
				pFloatData[i] = Fuzzer->GetFloatInRange(-100.0f, 100.0f);
			}
		}

		VertResource->Unmap(0, nullptr);

		D3D12_VERTEX_BUFFER_VIEW vtbView = {};
		vtbView.BufferLocation = VertResource->GetGPUVirtualAddress();
		vtbView.SizeInBytes = BufferSize;
		vtbView.StrideInBytes = 16;// *VertShader.ShaderMeta.NumParams; // I think????

		CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		CommandList->IASetVertexBuffers(ParamMeta.ParamIndex, 1, &vtbView);

		//TransitionResource(ResID, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

		AllResourcesInUse.push_back(ResID);
	}

	FlushResourceTransitions();

#if defined(WITH_PIPELINE_STATS_QUERY)
	ID3D12QueryHeap* QueryHeap = nullptr;

	D3D12_QUERY_HEAP_DESC QueryHeapDesc = {};
	QueryHeapDesc.Count = 1;
	QueryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
	Fuzzer->D3DDevice->CreateQueryHeap(&QueryHeapDesc, IID_PPV_ARGS(&QueryHeap));

	ASSERT(QueryHeap != nullptr);

	CommandList->BeginQuery(QueryHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
#endif

	CommandList->DrawInstanced(VertexCount, 1, 0, 0);

#if defined(WITH_PIPELINE_STATS_QUERY)
	CommandList->EndQuery(QueryHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);

	ID3D12Resource* DestBuffer = nullptr;
	{
		D3D12_HEAP_PROPERTIES HeapProps = {};
		HeapProps.Type = D3D12_HEAP_TYPE_READBACK;
		HeapProps.CreationNodeMask = 1;
		HeapProps.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC QueryBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));

		Fuzzer->D3DDevice->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &QueryBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&DestBuffer));
	}

	CommandList->ResolveQueryData(QueryHeap, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1, DestBuffer, 0);
#endif

	const bool ShouldReadbackImage = Fuzzer->Config->ShouldReadbackImage;

	if (ShouldReadbackImage)
	{
		ASSERT(Fuzzer->D3DPersist->RTReadback != nullptr);

		D3D12_TEXTURE_COPY_LOCATION CopyLocSrc = {}, CopyLocDst = {};
		CopyLocDst.pResource = Fuzzer->D3DPersist->RTReadback;
		CopyLocDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		CopyLocDst.PlacedFootprint.Offset = 0;
		CopyLocDst.PlacedFootprint.Footprint.Width = RTWidth;
		CopyLocDst.PlacedFootprint.Footprint.Height = RTHeight;
		CopyLocDst.PlacedFootprint.Footprint.Depth = 1;
		CopyLocDst.PlacedFootprint.Footprint.RowPitch = RTWidth * 4;
		CopyLocDst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

		CopyLocSrc.pResource = BackBufferResource;
		CopyLocSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		CopyLocSrc.SubresourceIndex = 0;

		TransitionResource(BackBufferResourceID, D3D12_RESOURCE_STATE_COPY_SOURCE);
		FlushResourceTransitions();

		CommandList->CopyTextureRegion(&CopyLocDst, 0, 0, 0, &CopyLocSrc, nullptr);
	}

	CommandList->Close();
}

void PostExecuteResourceTeardown(ShaderFuzzingState* Fuzzer, const std::vector<uint64>& AllResourcesInUse, const std::unordered_map<uint64, int32>& AllHeapsInUseAndCounts)
{
	for (auto ResID : AllResourcesInUse)
	{
		Fuzzer->D3DPersist->ResourceMgr.RelinquishResource(ResID);
	}

	for (auto HeapIDAndCount : AllHeapsInUseAndCounts)
	{
		uint64 HeapID = HeapIDAndCount.first;
		int32 Count = HeapIDAndCount.second;
		for (int32 i = 0; i < Count; i++)
		{
			Fuzzer->D3DPersist->ResourceMgr.RelinquishHeap(HeapID);
		}
	}

	float ResDeleteChance = Fuzzer->Config->ResourceDeletionChance;
	if (ResDeleteChance > 0.0f)
	{
		for (auto ResID : AllResourcesInUse)
		{
			if (ResDeleteChance >= 1.0f || Fuzzer->GetFloat01() < ResDeleteChance)
			{
				Fuzzer->D3DPersist->ResourceMgr.RequestResourceDestroyed(ResID);
			}
		}
	}

	float HeapDeleteChance = Fuzzer->Config->HeapDeletionChance;
	if (HeapDeleteChance > 0.0f)
	{
		for (auto HeapIDAndCount : AllHeapsInUseAndCounts)
		{
			uint64 HeapID = HeapIDAndCount.first;
			if (HeapDeleteChance >= 1.0f || Fuzzer->GetFloat01() < HeapDeleteChance)
			{
				Fuzzer->D3DPersist->ResourceMgr.RequestHeapDestroyed(HeapID);
			}
		}
	}

	// TODO: Is this safe to do here, or do we need to have some sort of recycling system that tracks
	// if there are any outstanding uses of the heap on the command queue?
	Fuzzer->D3DPersist->ResourceMgr.ResetAllHeapOffsets();
}

void DoIterationsWithFuzzer(ShaderFuzzingState* Fuzzer, int32_t NumIterations)
{
	for (int32_t Iteration = 0; Iteration < NumIterations; Iteration++)
	{

		FuzzShaderAST VertShader, PixelShader;
		VertShader.Type = D3DShaderType::Vertex;
		PixelShader.Type = D3DShaderType::Pixel;

		//--------------------
		// HLSL AST Fuzzer path
		//CreateInterstageVarsForVertexAndPixelShaders(Fuzzer, &VertShader, &PixelShader);
		//
		//// Set types
		//GenerateFuzzingShader(Fuzzer, &VertShader);
		//GenerateFuzzingShader(Fuzzer, &PixelShader);
		//
		//ConvertShaderASTToSourceCode(&VertShader, Fuzzer->Config);
		//ConvertShaderASTToSourceCode(&PixelShader, Fuzzer->Config);
		//
		//VerifyShaderCompilation(&VertShader);
		//VerifyShaderCompilation(&PixelShader);
		//--------------------
		// TODO: DXBC Bytecode Fuzzer path

		FuzzDXBCState DXBCState;
		GenerateShaderDXBC(&DXBCState);

		VertShader.ByteCodeBlob = DXBCState.VSBlob;
		PixelShader.ByteCodeBlob = DXBCState.PSBlob;

		ReflectShaderIntoShaderMetadata(VertShader.ByteCodeBlob, &VertShader.ShaderMeta);
		ReflectShaderIntoShaderMetadata(PixelShader.ByteCodeBlob, &PixelShader.ShaderMeta);

		//--------------------

		ID3D12RootSignature* RootSig = nullptr;
		ID3D12PipelineState* PSO = nullptr;
		RootSigResourceDesc RootSigDesc;

		VerifyGraphicsPSOCompilation(Fuzzer, &VertShader, &PixelShader, &RootSig, &PSO, &RootSigDesc);

		ASSERT(PSO != nullptr);

		ID3D12CommandAllocator* CommandAllocator = Fuzzer->D3DPersist->CmdListMgr.GetOpenCommandAllocator();
		if (CommandAllocator == nullptr)
		{
			ASSERT(SUCCEEDED(Fuzzer->D3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator))));
		}

		ID3D12GraphicsCommandList* CommandList = Fuzzer->D3DPersist->CmdListMgr.GetOpenCommandList(CommandAllocator);
		if (CommandList == nullptr)
		{
			ASSERT(SUCCEEDED(Fuzzer->D3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocator, 0, IID_PPV_ARGS(&CommandList))));
		}

		std::vector<uint64> AllResourcesInUse;
		std::unordered_map<uint64, int32> AllHeapsInUseAndCounts;

		std::vector<ID3D12DescriptorHeap*> SRVDescriptorHeaps;

		GenerateDrawingCommandsOnCommandList(Fuzzer, CommandList, PSO, RootSig, RootSigDesc, VertShader.ShaderMeta, PixelShader.ShaderMeta, AllResourcesInUse, AllHeapsInUseAndCounts, SRVDescriptorHeaps);

		PostExecuteResourceTeardown(Fuzzer, AllResourcesInUse, AllHeapsInUseAndCounts);

		ID3D12CommandList* CommandLists[] = { CommandList };
		if (Fuzzer->Config->LockMutexAroundExecCmdList != 0)
		{
			ASSERT(Fuzzer->D3DPersist->ExecuteCommandListMutex != nullptr);

			std::lock_guard<std::mutex> Lock(*Fuzzer->D3DPersist->ExecuteCommandListMutex);

			Fuzzer->D3DPersist->CmdQueue->ExecuteCommandLists(1, CommandLists);
		}
		else
		{
			Fuzzer->D3DPersist->CmdQueue->ExecuteCommandLists(1, CommandLists);
		}
		uint64 ValueSignaled = Fuzzer->D3DPersist->ExecFenceToSignal;
		Fuzzer->D3DPersist->CmdQueue->Signal(Fuzzer->D3DPersist->ExecFence, ValueSignaled);

		Fuzzer->D3DPersist->CmdListMgr.CheckIfFenceFinished(Fuzzer->D3DPersist->ExecFence->GetCompletedValue());
		Fuzzer->D3DPersist->ResourceMgr.CheckIfFenceFinished(Fuzzer->D3DPersist->ExecFence->GetCompletedValue());

		Fuzzer->D3DPersist->CmdListMgr.NowDoneWithCommandList(CommandList);
		Fuzzer->D3DPersist->CmdListMgr.NowDoneWithCommandAllocator(CommandAllocator);
		Fuzzer->D3DPersist->CmdListMgr.OnFrameFenceSignaled(ValueSignaled);

		Fuzzer->D3DPersist->ResourceMgr.OnFrameFenceSignaled(ValueSignaled);

		Fuzzer->D3DPersist->ResourceMgr.DeferredDelete(PSO, ValueSignaled);
		Fuzzer->D3DPersist->ResourceMgr.DeferredDelete(RootSig, ValueSignaled);

		for (auto* DescriptorHeap : SRVDescriptorHeaps)
		{
			Fuzzer->D3DPersist->ResourceMgr.DeferredDelete(DescriptorHeap, ValueSignaled);
		}

#if defined(WITH_PIPELINE_STATS_QUERY)
		// If we're synchronous
		HANDLE hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		Fuzzer->D3DPersist->ExecFence->SetEventOnCompletion(ValueSignaled, hEvent);
		WaitForSingleObject(hEvent, INFINITE);
		CloseHandle(hEvent);


		D3D12_RANGE PipelineRange;
		PipelineRange.Begin = 0;
		PipelineRange.End = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
		D3D12_QUERY_DATA_PIPELINE_STATISTICS* StatisticsPtr = nullptr;
		DestBuffer->Map(0, &PipelineRange, reinterpret_cast<void**>(&StatisticsPtr));

		D3D12_QUERY_DATA_PIPELINE_STATISTICS PipelineStats = *StatisticsPtr;

		LOG("IA Verts: %llu VS calls: %llu PS calls: %llu", PipelineStats.IAVertices, PipelineStats.VSInvocations, PipelineStats.PSInvocations);

		//uint64 PrevTotalPSCalls = InterlockedAdd64((volatile LONG64*)&TotalPSCalls, PipelineStats.PSInvocations);
		//uint64 PrevTotalFuzzCases = InterlockedAdd64((volatile LONG64*)&TotalFuzzCases, 1);
		//
		//double PSCallsPerCase = PrevTotalPSCalls;
		//PSCallsPerCase /= PrevTotalFuzzCases;
		//LOG("Avg PS calls per fuzz case: %3.2f", PSCallsPerCase);
#endif

		const bool ShouldReadbackImage = Fuzzer->Config->ShouldReadbackImage;

		if (ShouldReadbackImage)
		{
			// Have to wait for the GPU to finish before we can map the buffer that is filled w/ a copy of the render target
			HANDLE hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			Fuzzer->D3DPersist->ExecFence->SetEventOnCompletion(ValueSignaled, hEvent);
			WaitForSingleObject(hEvent, INFINITE);
			CloseHandle(hEvent);

			auto* RTReadback = Fuzzer->D3DPersist->RTReadback;
			ASSERT(RTReadback != nullptr);

			void* pPixelData = nullptr;
			HRESULT hr = RTReadback->Map(0, nullptr, &pPixelData);
			ASSERT(SUCCEEDED(hr));

			const int32 RTWidth = Fuzzer->Config->RTWidth;
			const int32 RTHeight = Fuzzer->Config->RTHeight;

			char filename[256] = {};
			snprintf(filename, sizeof(filename), "render_output/%s%llu%s.png", Fuzzer->Config->ReadbackImageNamePrepend, Fuzzer->InitialFuzzSeed, Fuzzer->Config->ReadbackImageNameAppend);
			stbi_write_png(filename, RTWidth, RTHeight, 4, pPixelData, 0);

			RTReadback->Unmap(0, nullptr);
		}


		Fuzzer->D3DPersist->ExecFenceToSignal++;


		//LOG("==============\nShader source (vertex):----------");
		//OutputDebugStringA(VertShader.SourceCode.c_str());
		//LOG("---------\nShader source (pixel):---------");
		//OutputDebugStringA(PixelShader.SourceCode.c_str());
		//LOG("================");
	}
}


void SetupFuzzPersistState(D3DDrawingFuzzingPersistentState* Persist, ShaderFuzzConfig* Config, ID3D12Device* Device)
{
	D3D12_COMMAND_QUEUE_DESC CmdQueueDesc = {};
	CmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ASSERT(SUCCEEDED(Device->CreateCommandQueue(&CmdQueueDesc, IID_PPV_ARGS(&Persist->CmdQueue))));

	ASSERT(SUCCEEDED(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Persist->ExecFence))));

	{
		const int32 RTWidth = Config->RTWidth;
		const int32 RTHeight = Config->RTHeight;

		D3D12_HEAP_PROPERTIES HeapProps = {};
		HeapProps.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC RenderReadbackResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(RTWidth * RTHeight * 4);
		HRESULT hr = Device->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &RenderReadbackResourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&Persist->RTReadback));
		ASSERT(SUCCEEDED(hr));
	}
}










