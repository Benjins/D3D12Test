#include <stdio.h>

#include <Windows.h>

#include <d3d12.h>

#include <d3dcompiler.h>

#include <dxgi1_2.h>

#include "basics.h"

#include "d3d12_ext.h"

#include "fuzz_shader_compiler.h"

#include "shader_meta.h"

#include "string_stack_buffer.h"

#include <assert.h>
#include <unordered_map>

// struct ShaderAST;
// struct ShaderResource;
// struct ShaderInput;

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

struct FuzzShaderBinaryOperator : FuzzShaderASTNode
{
	enum struct Operator
	{
		Add,
		Subtract,
		Multiple,
		Divide
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
	D3D12_SHADER_BYTECODE ByteCode;
};

FuzzShaderASTNode* GenerateFuzzingShaderValue(ShaderFuzzingState* Fuzzer, FuzzShaderAST* OutShaderAST)
{
	float Decider = Fuzzer->GetFloat01();

	if (Decider < 0.2)
	{
		// Binary Op
		auto* BinaryOp = OutShaderAST->AllocateNode<FuzzShaderBinaryOperator>();
		BinaryOp->Op = FuzzShaderBinaryOperator::Operator::Add;
		BinaryOp->LHS = GenerateFuzzingShaderValue(Fuzzer, OutShaderAST);
		BinaryOp->RHS = GenerateFuzzingShaderValue(Fuzzer, OutShaderAST);

		return BinaryOp;
	}
	else if (Decider < 0.5 && OutShaderAST->BoundTextures.size() > 0)
	{
		auto* Tex = OutShaderAST->AllocateNode<FuzzShaderTextureAccess>();
		Tex->TextureName = OutShaderAST->BoundTextures[Fuzzer->GetIntInRange(0, OutShaderAST->BoundTextures.size() - 1)].ResourceName;
		Tex->SamplerName = OutShaderAST->BoundTextures[Fuzzer->GetIntInRange(0, OutShaderAST->BoundTextures.size() - 1)].SamplerName;
		Tex->UV = GenerateFuzzingShaderValue(Fuzzer, OutShaderAST);

		return Tex;
	}
	else if (Decider < 0.6 && OutShaderAST->GetNumVariablesInScope() > 0)
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
	NewStmt->Value = GenerateFuzzingShaderValue(Fuzzer, OutShaderAST);

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

	int NumRootStatements = Fuzzer->GetIntInRange(2, 20);

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
				auto* NewStmt = GenerateFuzzingShaderAssignment(Fuzzer, OutShaderAST);
				NewStmt->IsPredeclared = true;
				NewStmt->VariableName = std::string("result.") + Var.VarName;
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

void ConvertShaderASTNodeToSourceCode(FuzzShaderAST* ShaderAST, FuzzShaderASTNode* Node, StringStackBuffer<AST_SOURCE_LIMIT>* StrBuf)
{
	// TODO: Do Maybe cast construction w/ templates or w/e? Since we have the static type stuff
	if (Node->Type == FuzzShaderASTNode::NodeType::Assignment)
	{
		auto* Assnmt = static_cast<FuzzShaderAssignment*>(Node);

		StrBuf->AppendFormat("\t%s%s = ", (Assnmt->IsPredeclared ? "" : "float4 "), Assnmt->VariableName.c_str());
		ConvertShaderASTNodeToSourceCode(ShaderAST, Assnmt->Value, StrBuf);
		StrBuf->AppendFormat(";\n");
	}
	else if (Node->Type == FuzzShaderASTNode::NodeType::BinaryOperator)
	{
		auto* Bin = static_cast<FuzzShaderBinaryOperator*>(Node);

		StrBuf->AppendFormat("(");
		ConvertShaderASTNodeToSourceCode(ShaderAST, Bin->LHS, StrBuf);

		if (Bin->Op == FuzzShaderBinaryOperator::Operator::Add)
		{
			StrBuf->AppendFormat(" + ");
		}
		else
		{
			assert(false && "skjdfjk");
		}

		ConvertShaderASTNodeToSourceCode(ShaderAST, Bin->RHS, StrBuf);
		StrBuf->AppendFormat(")");
	}
	else if (Node->Type == FuzzShaderASTNode::NodeType::TextureAccess)
	{
		auto* Tex = static_cast<FuzzShaderTextureAccess*>(Node);

		// TODO: SampleLevel is required for vertex shader, but Sample() could be done in Pixel shaders
		StrBuf->AppendFormat("(%s.SampleLevel(%s, (", Tex->TextureName.c_str(), Tex->SamplerName.c_str());
		ConvertShaderASTNodeToSourceCode(ShaderAST, Tex->UV, StrBuf);
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
			ConvertShaderASTNodeToSourceCode(ShaderAST, Stmt, StrBuf);
		}

		// TODO: Should be in AST generation, not here
		if (Node == ShaderAST->RootASTNode)
		{
			StrBuf->AppendFormat("\treturn result;\n");
		}

		StrBuf->AppendFormat("}\n");
	}
	else
	{
		assert(false && "sdfljbasgfdjk");
	}
}

void ConvertShaderASTToSourceCode(FuzzShaderAST* ShaderAST)
{
	// TODO: Maybe move this to heap and make it dynamic, idk
	StringStackBuffer<AST_SOURCE_LIMIT> StrBuf;

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

	ConvertShaderASTNodeToSourceCode(ShaderAST, ShaderAST->RootASTNode, &StrBuf);

	StrBuf.Append("\n");


	ShaderAST->SourceCode = StrBuf.buffer;
}

void VerifyShaderCompilation(FuzzShaderAST* ShaderAST)
{
	ID3DBlob* ByteCode = nullptr;
	ID3DBlob* ErrorMsg = nullptr;
	UINT CompilerFlags = D3DCOMPILE_DEBUG;
	
	char ShaderSourceName[256] = {};
	snprintf(ShaderSourceName, sizeof(ShaderSourceName), "");

	HRESULT hr = D3DCompile(ShaderAST->SourceCode.c_str(), ShaderAST->SourceCode.size(), ShaderSourceName, nullptr, nullptr, "Main", GetTargetForShaderType(ShaderAST->Type), CompilerFlags, 0, &ByteCode, &ErrorMsg);
	if (SUCCEEDED(hr)) {
		D3D12_SHADER_BYTECODE ByteCodeObj;
		ByteCodeObj.pShaderBytecode = ByteCode->GetBufferPointer();
		ByteCodeObj.BytecodeLength = ByteCode->GetBufferSize();

		// TODO: Grab metadata
	}
	else
	{

		LOG("Compile of '%s' failed, hr = 0x%08X, err msg = '%s'", ShaderSourceName, hr, (ErrorMsg && ErrorMsg->GetBufferPointer()) ? (const char*)ErrorMsg->GetBufferPointer() : "<NONE_GIVEN>");

		LOG("Dumping shader source....");
		OutputDebugStringA(ShaderAST->SourceCode.c_str());

		ASSERT(false && "Fix the damn shaders");
	}
}

void VerifyGraphicsPSOCompilation(ShaderFuzzingState* Fuzzer, FuzzShaderAST* VertexShader, FuzzShaderAST* PixelShader)
{

}

void VerifyComputePSOCompilation(ShaderFuzzingState* Fuzzer, FuzzShaderAST* ComputeShader)
{
	// TODO: fancy
}

void CreateInterstageVarsForVertexAndPixelShaders(ShaderFuzzingState* Fuzzer, FuzzShaderAST* VertexShader, FuzzShaderAST* PixelShader)
{
	// Always include Position as IA var
	{
		FuzzShaderSemanticVar PosVar;
		PosVar.ParamIdx = 0;
		PosVar.Semantic = ShaderSemantic::POSITION;
		PosVar.VarName = GetRandomShaderVariableName(Fuzzer, "iaparam");
		VertexShader->IAVars.push_back(PosVar);
	}

	int32 NumAdditionalIAVars = Fuzzer->GetIntInRange(0, 4);
	for (int32 i = 0; i < NumAdditionalIAVars; i++)
	{
		FuzzShaderSemanticVar NewVar;
		NewVar.ParamIdx = i + 1;
		NewVar.Semantic = (ShaderSemantic)Fuzzer->GetIntInRange((int32)ShaderSemantic::IA_FIRST, (int32)ShaderSemantic::IA_LAST);
		NewVar.VarName = GetRandomShaderVariableName(Fuzzer, "iaparam");
		VertexShader->IAVars.push_back(NewVar);
	}

	// Alwyas include SV_Position as inter-stage var
	{
		FuzzShaderSemanticVar SVPosVar;
		SVPosVar.ParamIdx = 0;
		SVPosVar.Semantic = ShaderSemantic::SV_POSITION;
		SVPosVar.VarName = GetRandomShaderVariableName(Fuzzer, "param");
		VertexShader->InterStageVars.push_back(SVPosVar);
		PixelShader->InterStageVars.push_back(SVPosVar);
	}


	int32 NumAdditionalInterstageVars = Fuzzer->GetIntInRange(0, 4);
	for (int32 i = 0; i < NumAdditionalInterstageVars; i++)
	{
		FuzzShaderSemanticVar NewVar;
		NewVar.ParamIdx = i + 1;
		NewVar.Semantic = (ShaderSemantic)Fuzzer->GetIntInRange((int32)ShaderSemantic::INTER_FIRST, (int32)ShaderSemantic::INTER_LAST);
		NewVar.VarName = GetRandomShaderVariableName(Fuzzer, "param");
		VertexShader->IAVars.push_back(NewVar);
	}
}

void SetSeedOnFuzzer(ShaderFuzzingState* Fuzzer, uint64_t Seed)
{
	Fuzzer->RNGState.seed(Seed);
}

void DoIterationsWithFuzzer(ShaderFuzzingState* Fuzzer, int32_t NumIterations)
{
	{
		FuzzShaderAST VertShader, PixelShader;
		VertShader.Type = D3DShaderType::Vertex;
		PixelShader.Type = D3DShaderType::Pixel;

		CreateInterstageVarsForVertexAndPixelShaders(Fuzzer, &VertShader, &PixelShader);

		// Set types
		GenerateFuzzingShader(Fuzzer, &VertShader);
		GenerateFuzzingShader(Fuzzer, &PixelShader);

		ConvertShaderASTToSourceCode(&VertShader);
		ConvertShaderASTToSourceCode(&PixelShader);

		VerifyShaderCompilation(&VertShader);
		VerifyShaderCompilation(&PixelShader);

		//LOG("==============\nShader source (vertex):----------");
		//OutputDebugStringA(VertShader.SourceCode.c_str());
		//LOG("---------\nShader source (pixel):---------");
		//OutputDebugStringA(PixelShader.SourceCode.c_str());
		//LOG("================");
	}

	return;

	for (int32_t Iteration = 0; Iteration < NumIterations; Iteration++)
	{
		FuzzShaderAST VertShader, PixelShader;
		VertShader.Type = D3DShaderType::Vertex;
		PixelShader.Type = D3DShaderType::Pixel;

		CreateInterstageVarsForVertexAndPixelShaders(Fuzzer, &VertShader, &PixelShader);

		// Set types
		GenerateFuzzingShader(Fuzzer, &VertShader);
		GenerateFuzzingShader(Fuzzer, &PixelShader);

		VerifyShaderCompilation(&VertShader);
		VerifyShaderCompilation(&PixelShader);

		VerifyGraphicsPSOCompilation(Fuzzer, &VertShader, &PixelShader);
	}
}










