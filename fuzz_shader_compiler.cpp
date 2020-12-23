#include <stdio.h>

#include <Windows.h>

#include <d3d12.h>

#include <d3dcompiler.h>

#include <dxgi1_2.h>

#include "basics.h"

#include "d3d12_ext.h"

#include "fuzz_shader_compiler.h"

#include "string_stack_buffer.h"

#include <assert.h>
#include <unordered_map>

// struct ShaderAST;
// struct ShaderResource;
// struct ShaderInput;

// Generate AST from 

typedef unsigned char byte;

enum struct FuzzShaderType
{
	Vertex,
	Pixel,
	Compute,
	Count
};

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
		TextureLoad,
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

	FuzzShaderASTNode* LHS;
	FuzzShaderASTNode* RHS;
	Operator Op;
};

struct FuzzShaderTextureLoad : FuzzShaderASTNode
{
	static constexpr NodeType StaticType = NodeType::TextureLoad;
};

struct FuzzShaderAssignment : FuzzShaderASTNode
{
	static constexpr NodeType StaticType = NodeType::Assignment;

	std::string VariableName;
	FuzzShaderASTNode* Value;
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

struct FuzzShaderResourceBinding
{
	enum struct ResourceType
	{
		// Texture
		// Sampler
		// ????
	};
};

struct FuzzShaderAST
{
	FuzzShaderType Type;
	// ....

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

std::string GetRandomShaderVariableName(ShaderFuzzingState* Fuzzer)
{
	// TODO: We might want to add more bits just to avoid any birthday attacks...I mean it's not cryptography but still would
	// be a false positive in fuzzing so yanno
	char Buffer[256] = {};
	int a = Fuzzer->GetIntInRange(0, 64 * 1024);
	int b = Fuzzer->GetIntInRange(0, 64 * 1024);
	int c = Fuzzer->GetIntInRange(0, 64 * 1024);
	snprintf(Buffer, sizeof(Buffer), "tempvar_%d_%d_%d", a, b, c);

	return Buffer;
}

FuzzShaderAssignment* GenerateFuzzingShaderAssignment(ShaderFuzzingState* Fuzzer, FuzzShaderAST* OutShaderAST)
{
	auto* NewStmt = OutShaderAST->AllocateNode<FuzzShaderAssignment>();

	NewStmt->VariableName = GetRandomShaderVariableName(Fuzzer);
	NewStmt->Value = GenerateFuzzingShaderValue(Fuzzer, OutShaderAST);

	OutShaderAST->VariablesInScope.back().emplace(NewStmt->VariableName, NewStmt);

	return NewStmt;
}

void GenerateFuzzingShader(ShaderFuzzingState* Fuzzer, FuzzShaderAST* OutShaderAST)
{
	auto* RootNode = OutShaderAST->AllocateNode<FuzzShaderStatementBlock>();
	OutShaderAST->RootASTNode = RootNode;

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

	OutShaderAST->VariablesInScope.pop_back();

	assert(OutShaderAST->VariablesInScope.size() == 0);
}

#define AST_SOURCE_LIMIT (64 * 1024)

void ConvertShaderASTNodeToSourceCode(FuzzShaderAST* ShaderAST, FuzzShaderASTNode* Node, StringStackBuffer<AST_SOURCE_LIMIT>* StrBuf)
{
	// TODO: Do Maybe cast construction w/ templates or w/e? Since we have the static type stuff
	if (Node->Type == FuzzShaderASTNode::NodeType::Assignment)
	{
		auto* Assnmt = static_cast<FuzzShaderAssignment*>(Node);

		StrBuf->AppendFormat("\tfloat4 %s = ", Assnmt->VariableName.c_str());
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
		for (auto Stmt : Block->Statements)
		{
			ConvertShaderASTNodeToSourceCode(ShaderAST, Stmt, StrBuf);
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
	StrBuf.Append("float4 Main(/*TODO: Vertex attribs*/)\n");

	ConvertShaderASTNodeToSourceCode(ShaderAST, ShaderAST->RootASTNode, &StrBuf);

	StrBuf.Append("\n");


	ShaderAST->SourceCode = StrBuf.buffer;
}

void VerifyShaderCompilation(FuzzShaderAST* ShaderAST)
{

}

void VerifyGraphicsPSOCompilation(ShaderFuzzingState* Fuzzer, FuzzShaderAST* VertexShader, FuzzShaderAST* PixelShader)
{

}

void VerifyComputePSOCompilation(ShaderFuzzingState* Fuzzer, FuzzShaderAST* ComputeShader)
{
	// TODO: fancy
}

void SetSeedOnFuzzer(ShaderFuzzingState* Fuzzer, uint64_t Seed)
{
	Fuzzer->RNGState.seed(Seed);
}

void DoIterationsWithFuzzer(ShaderFuzzingState* Fuzzer, int32_t NumIterations)
{
	{
		FuzzShaderAST VertShader;
		VertShader.Type = FuzzShaderType::Vertex;

		// Set types
		GenerateFuzzingShader(Fuzzer, &VertShader);

		ConvertShaderASTToSourceCode(&VertShader);

		LOG("Shader source:----------\n%s\n---------", VertShader.SourceCode.c_str());
	}

	return;

	for (int32_t Iteration = 0; Iteration < NumIterations; Iteration++)
	{
		FuzzShaderAST VertShader, PixelShader;
		VertShader.Type = FuzzShaderType::Vertex;
		PixelShader.Type = FuzzShaderType::Pixel;

		// Set types
		GenerateFuzzingShader(Fuzzer, &VertShader);
		GenerateFuzzingShader(Fuzzer, &PixelShader);

		VerifyShaderCompilation(&VertShader);
		VerifyShaderCompilation(&PixelShader);

		VerifyGraphicsPSOCompilation(Fuzzer, &VertShader, &PixelShader);
	}
}










