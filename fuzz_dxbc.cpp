
#include "fuzz_dxbc.h"

#include "shader_meta.h"

#include "dxbc_hash.h"

#include <vector>

#pragma pack(push)
#pragma pack(1)

struct DXBCFileHeader
{
	char MagicNumbers[4] = {};
	byte CheckSum[16] = {};
	uint32 Unknown = 0;
	uint32 FileSizeInBytes = 0;
	uint32 ChunkCount = 0;
};

static_assert(sizeof(DXBCFileHeader) == 32, "Check DXBCFileHeader alignment/size");

#pragma pack(pop)


// Substantial parts of this code were guided by reverse engineering, and using the code at https://github.com/tgjones/slimshader
// as reference. The code at https://github.com/tgjones/slimshader is from Tim Jones, released under the MIT License

// List adapted from https://github.com/tpn/winsdk-10/blob/master/Include/10.0.16299.0/um/d3d10TokenizedProgramFormat.hpp
enum D3DOpcodeType
{
	D3DOpcodeType_Invalid = -1,
	D3DOpcodeType_ADD = 0,
	D3DOpcodeType_AND,
	D3DOpcodeType_BREAK,
	D3DOpcodeType_BREAKC,
	D3DOpcodeType_CALL,
	D3DOpcodeType_CALLC,
	D3DOpcodeType_CASE,
	D3DOpcodeType_CONTINUE,
	D3DOpcodeType_CONTINUEC,
	D3DOpcodeType_CUT,
	D3DOpcodeType_DEFAULT,
	D3DOpcodeType_DERIV_RTX,
	D3DOpcodeType_DERIV_RTY,
	D3DOpcodeType_DISCARD,
	D3DOpcodeType_DIV,
	D3DOpcodeType_DP2,
	D3DOpcodeType_DP3,
	D3DOpcodeType_DP4,
	D3DOpcodeType_ELSE,
	D3DOpcodeType_EMIT,
	D3DOpcodeType_EMITTHENCUT,
	D3DOpcodeType_ENDIF,
	D3DOpcodeType_ENDLOOP,
	D3DOpcodeType_ENDSWITCH,
	D3DOpcodeType_EQ,
	D3DOpcodeType_EXP,
	D3DOpcodeType_FRC,
	D3DOpcodeType_FTOI,
	D3DOpcodeType_FTOU,
	D3DOpcodeType_GE,
	D3DOpcodeType_IADD,
	D3DOpcodeType_IF,
	D3DOpcodeType_IEQ,
	D3DOpcodeType_IGE,
	D3DOpcodeType_ILT,
	D3DOpcodeType_IMAD,
	D3DOpcodeType_IMAX,
	D3DOpcodeType_IMIN,
	D3DOpcodeType_IMUL,
	D3DOpcodeType_INE,
	D3DOpcodeType_INEG,
	D3DOpcodeType_ISHL,
	D3DOpcodeType_ISHR,
	D3DOpcodeType_ITOF,
	D3DOpcodeType_LABEL,
	D3DOpcodeType_LD,
	D3DOpcodeType_LD_MS,
	D3DOpcodeType_LOG,
	D3DOpcodeType_LOOP,
	D3DOpcodeType_LT,
	D3DOpcodeType_MAD,
	D3DOpcodeType_MIN,
	D3DOpcodeType_MAX,
	D3DOpcodeType_CUSTOMDATA,
	D3DOpcodeType_MOV,
	D3DOpcodeType_MOVC,
	D3DOpcodeType_MUL,
	D3DOpcodeType_NE,
	D3DOpcodeType_NOP,
	D3DOpcodeType_NOT,
	D3DOpcodeType_OR,
	D3DOpcodeType_RESINFO,
	D3DOpcodeType_RET,
	D3DOpcodeType_RETC,
	D3DOpcodeType_ROUND_NE,
	D3DOpcodeType_ROUND_NI,
	D3DOpcodeType_ROUND_PI,
	D3DOpcodeType_ROUND_Z,
	D3DOpcodeType_RSQ,
	D3DOpcodeType_SAMPLE,
	D3DOpcodeType_SAMPLE_C,
	D3DOpcodeType_SAMPLE_C_LZ,
	D3DOpcodeType_SAMPLE_L,
	D3DOpcodeType_SAMPLE_D,
	D3DOpcodeType_SAMPLE_B,
	D3DOpcodeType_SQRT,
	D3DOpcodeType_SWITCH,
	D3DOpcodeType_SINCOS,
	D3DOpcodeType_UDIV,
	D3DOpcodeType_ULT,
	D3DOpcodeType_UGE,
	D3DOpcodeType_UMUL,
	D3DOpcodeType_UMAD,
	D3DOpcodeType_UMAX,
	D3DOpcodeType_UMIN,
	D3DOpcodeType_USHR,
	D3DOpcodeType_UTOF,
	D3DOpcodeType_XOR,
	D3DOpcodeType_DCL_RESOURCE,
	D3DOpcodeType_DCL_CONSTANT_BUFFER,
	D3DOpcodeType_DCL_SAMPLER,
	D3DOpcodeType_DCL_INDEX_RANGE,
	D3DOpcodeType_DCL_GS_OUTPUT_PRIMITIVE_TOPOLOGY,
	D3DOpcodeType_DCL_GS_INPUT_PRIMITIVE,
	D3DOpcodeType_DCL_MAX_OUTPUT_VERTEX_COUNT,
	D3DOpcodeType_DCL_INPUT,
	D3DOpcodeType_DCL_INPUT_SGV,
	D3DOpcodeType_DCL_INPUT_SIV,
	D3DOpcodeType_DCL_INPUT_PS,
	D3DOpcodeType_DCL_INPUT_PS_SGV,
	D3DOpcodeType_DCL_INPUT_PS_SIV,
	D3DOpcodeType_DCL_OUTPUT,
	D3DOpcodeType_DCL_OUTPUT_SGV,
	D3DOpcodeType_DCL_OUTPUT_SIV,
	D3DOpcodeType_DCL_TEMPS,
	D3DOpcodeType_DCL_INDEXABLE_TEMP,
	D3DOpcodeType_DCL_GLOBAL_FLAGS,
	D3DOpcodeType_RESERVED0,
	D3DOpcodeType_NUM
};


enum OperandNumComponents {
	OperandNumComponents_Zero,
	OperandNumComponents_One,
	OperandNumComponents_Four,
	OperandNumComponents_Count,
	// There's also N, but apparently not used atm
};

enum Operand4CompSelection {
	Operand4CompSelection_Mask,
	Operand4CompSelection_Swizzle,
	Operand4CompSelection_Select,
	Operand4CompSelection_Count,
};

enum OperandSourceType
{
	OperandSourceType_TempRegister,
	OperandSourceType_InputRegister,
	OperandSourceType_OutputRegister,
	OperandSourceType_IndexableTempRegister,
	OperandSourceType_Immediate32,
	OperandSourceType_Immediate64,
	OperandSourceType_Sampler,
	OperandSourceType_Resource,
	OperandSourceType_ConstantBuffer,
	OperandSourceType_ImmediateConstantBuffer,
	OperandSourceType_Label,
	OperandSourceType_Count
};

enum OperandSourceIndexDimension
{
	OperandSourceIndexDimension_0D,
	OperandSourceIndexDimension_1D,
	OperandSourceIndexDimension_2D,
	OperandSourceIndexDimension_3D,
	OperandSourceIndexDimension_Count,
};

enum OperandSourceIndexRepr
{
	OperandSourceIndexRepr_Imm32,
	OperandSourceIndexRepr_Imm64,
	OperandSourceIndexRepr_Relative,
	OperandSourceIndexRepr_RelativePlusImm32,
	OperandSourceIndexRepr_RelativePlusImm64,
	OperandSourceIndexRepr_Count,
};

// TODO: There are a lot more, but we're ignoring those for now
enum OperandSemantic
{
	OperandSemantic_Undefined,
	OperandSemantic_Position,
	OperandSemantic_Count,
};

enum SamplerMode {
	SamplerMode_Default,
	SamplerMode_Comparison,
	SamplerMode_Mono
};

enum ResourceDimension {
	ResourceDimension_Unknown,
	ResourceDimension_Buffer,
	ResourceDimension_Texture1D,
	ResourceDimension_Texture2D,
	ResourceDimension_Texture2D_MultiSample,
	ResourceDimension_Texture3D,
	ResourceDimension_TextureCube,
	ResourceDimension_Texture1DArray,
	ResourceDimension_Texture2DArray,
	ResourceDimension_Texture2DArray_MultiSample,
	ResourceDimension_TextureCubeArray,
	ResourceDimension_RawBuffer,
	ResourceDimension_StructuredBuffer,
};

enum ResourceReturnType {
	ResourceReturnType_NA,
	ResourceReturnType_Unorm,
	ResourceReturnType_Snorm,
	ResourceReturnType_SInt,
	ResourceReturnType_UInt,
	ResourceReturnType_Float,
	ResourceReturnType_Mixed,
	ResourceReturnType_Double,
	ResourceReturnType_Continued,
};

enum OpcodeExtensionType
{
	OpcodeExtensionType_None,
	OpcodeExtensionType_SampleControls,
	OpcodeExtensionType_ResourceDim,
	OpcodeExtensionType_ResourceReturnType
};

struct D3DOpcode
{
	struct IODecl
	{
		OperandNumComponents NumComponents = OperandNumComponents_Zero;
		Operand4CompSelection FourCompSelect = Operand4CompSelection_Mask;
		uint8 CompMask = 0x0F;
		uint8 CompSwizzle[4] = { 0, 1, 2, 3 };
		OperandSourceType SrcType = OperandSourceType_TempRegister;
		OperandSourceIndexDimension SrcDimension = OperandSourceIndexDimension_0D;
		OperandSourceIndexRepr SrcIndicesRepr[4] = {};
		uint64 SrcIndicesValues[4] = {};
		double ImmediateValues[4] = {};
	};

	D3DOpcodeType Type = D3DOpcodeType_Invalid;
	union
	{
		struct {
			uint32 IsRefactoringAllowed : 1;
			uint32 EnableDoublePrecision : 1;
		} GlobalFlagsDecl;

		struct {
			IODecl Decl;
		} InputDeclaration;

		struct {
			IODecl Decl;
		} OutputDeclaration;

		struct {
			IODecl Decl;
			OperandSemantic Semantic;
		} OutputDeclarationSIV;

		struct {
			bool IsDynamicIndexed;
			int32 CBVRegIndex;
			int32 CBVSize;
		} CBVDeclaration;

		struct {
			int32 NumTemps;
		} TempRegistersDeclaration;

		struct {
			int32 SamplerRegister;
			SamplerMode SamplerMode;
		} SamplerDeclaration;

		struct {
			IODecl OutputReg;
			IODecl UVReg;
			IODecl TextureReg;
			IODecl SamplerReg;
			IODecl MipLevel;
			ResourceReturnType ReturnType[4];
			ResourceDimension Dimension;
			int32 ResourceStride; // Usually 0?
		} SampleLOp;

		struct {
			int32 TextureRegIndex;
			int32 SampleCount;
			ResourceDimension Dimension;
			ResourceReturnType ReturnType[4];
		} ResourceDeclaration;

		struct {
			IODecl Output;
			IODecl Src1;
			IODecl Src2;
			IODecl Src3;
		} TernaryGeneralOp;

		struct {
			IODecl Output;
			IODecl Src1;
			IODecl Src2;
		} BinaryGeneralOp;

		struct {
			IODecl Output;
			IODecl Src;
		} UnaryGeneralOp;
	};

	D3DOpcode() { }
};


inline bool IsOpcodeGenericTernaryOp(D3DOpcodeType OpcodeType)
{
	return (OpcodeType == D3DOpcodeType_MAD);
}

inline bool IsOpcodeGenericBinaryOp(D3DOpcodeType OpcodeType)
{
	return (OpcodeType == D3DOpcodeType_ADD) || (OpcodeType == D3DOpcodeType_MUL);
}

inline bool IsOpcodeGenericUnaryOp(D3DOpcodeType OpcodeType)
{
	return (OpcodeType == D3DOpcodeType_MOV);
}


static_assert(sizeof(DXBCFileHeader) == 32, "Check packing on DXBCFileHeader");

template<typename T>
inline T GetValueFromCursor(byte** Cursor)
{
	T Val = *(T*)*Cursor;
	(*Cursor) += sizeof(T);
	return Val;
}

template<int Lo, int Hi, typename T = uint32>
inline T GetBitsFromWord(uint32 Val)
{
	uint32 Mask = 0;

	for (int32 i = Lo; i <= Hi; i++)
	{
		Mask |= (1 << i);
	}

	return (T)((Val & Mask) >> Lo);
}

template<int Lo, int Hi, typename T = uint32>
inline void SetBitsFromWord(uint32* Val, T Bits)
{
	uint32 Mask = 0;

	for (int32 i = Lo; i <= Hi; i++)
	{
		Mask |= (1 << i);
	}

	*Val = ((*Val) & ~Mask) | ((Bits << Lo) & Mask);
}

template<typename T = uint32>
inline T GetBitsFromWord(uint32 Val, int32 Lo, int32 Hi)
{
	uint32 Mask = 0;

	for (int32 i = Lo; i <= Hi; i++)
	{
		Mask |= (1 << i);
	}

	return (T)((Val & Mask) >> Lo);
}

template<typename T = uint32>
inline void SetBitsFromWord(uint32* Val, int32 Lo, int32 Hi, T Bits)
{
	uint32 Mask = 0;

	for (int32 i = Lo; i <= Hi; i++)
	{
		Mask |= (1 << i);
	}

	*Val = ((*Val) & ~Mask) | ((Bits << Lo) & Mask);
}

D3DOpcode::IODecl ParseIODeclFromCursor(byte** Cursor)
{
	uint32 SecondDWORD = GetValueFromCursor<uint32>(Cursor);

	D3DOpcode::IODecl Decl;

	Decl.NumComponents = GetBitsFromWord<0, 1, OperandNumComponents>(SecondDWORD);

	if (Decl.NumComponents == OperandNumComponents_Four)
	{
		Decl.FourCompSelect = GetBitsFromWord<2, 3, Operand4CompSelection>(SecondDWORD);

		if (Decl.FourCompSelect == Operand4CompSelection_Mask)
		{
			Decl.CompMask = GetBitsFromWord<4, 7, uint8>(SecondDWORD);
		}
		else if (Decl.FourCompSelect == Operand4CompSelection_Swizzle)
		{
			Decl.CompSwizzle[0] = GetBitsFromWord<4, 5, uint8>(SecondDWORD);
			Decl.CompSwizzle[1] = GetBitsFromWord<6, 7, uint8>(SecondDWORD);
			Decl.CompSwizzle[2] = GetBitsFromWord<8, 9, uint8>(SecondDWORD);
			Decl.CompSwizzle[3] = GetBitsFromWord<10, 11, uint8>(SecondDWORD);
		}
		else if (Decl.FourCompSelect == Operand4CompSelection_Select)
		{
			for (int32 i = 0; i < 4; i++)
			{
				Decl.CompSwizzle[i] = GetBitsFromWord<4, 5, uint8>(SecondDWORD);
			}
		}
		else
		{
			ASSERT(false);
		}
	}

	Decl.SrcType = GetBitsFromWord<12, 19, OperandSourceType>(SecondDWORD);
	Decl.SrcDimension = GetBitsFromWord<20, 21, OperandSourceIndexDimension>(SecondDWORD);
	for (int32 i = 0; i < (int32)Decl.SrcDimension; i++)
	{
		Decl.SrcIndicesRepr[i] = GetBitsFromWord<OperandSourceIndexRepr>(SecondDWORD, 22 + i * 3, 22 + i * 3 + 2);

		if (Decl.SrcIndicesRepr[i] == OperandSourceIndexRepr_Imm32)
		{
			Decl.SrcIndicesValues[i] = GetValueFromCursor<uint32>(Cursor);
		}
		else if (Decl.SrcIndicesRepr[i] == OperandSourceIndexRepr_Imm64)
		{
			Decl.SrcIndicesValues[i] = GetValueFromCursor<uint64>(Cursor);
		}
		else
		{
			ASSERT(false);
		}
	}

	int32 NumComps = 0;
	if (Decl.NumComponents == OperandNumComponents_Zero)
	{
		NumComps = 0;
	}
	else if (Decl.NumComponents == OperandNumComponents_One)
	{
		NumComps = 1;
	}
	else if (Decl.NumComponents == OperandNumComponents_Four)
	{
		NumComps = 4;
	}
	else
	{
		ASSERT(false);
	}

	if (Decl.SrcType == OperandSourceType_Immediate32)
	{
		for (int32 CompIdx = 0; CompIdx < NumComps; CompIdx++)
		{
			Decl.ImmediateValues[CompIdx] = GetValueFromCursor<float>(Cursor);
		}
	}
	else if (Decl.SrcType == OperandSourceType_Immediate64)
	{
		for (int32 CompIdx = 0; CompIdx < NumComps; CompIdx++)
		{
			Decl.ImmediateValues[CompIdx] = GetValueFromCursor<double>(Cursor);
		}
	}

	bool IsExtendedOperand = GetBitsFromWord<31, 31>(SecondDWORD) != 0;
	//ASSERT(!IsExtendedOperand);

	if (IsExtendedOperand)
	{
		// TODO:
		GetValueFromCursor<uint32>(Cursor);
		ASSERT(false);
	}

	return Decl;
}

D3DOpcode GetD3DOpcodeFromCursor(byte** Cursor)
{
	D3DOpcode OpCode;

	byte* OrigCursor = *Cursor;

	uint32 OpcodeStartDWORD = GetValueFromCursor<uint32>(Cursor);
	uint32 OpCodeType = GetBitsFromWord<0, 10>(OpcodeStartDWORD);
	uint32 OpCodeLength = GetBitsFromWord<24, 30>(OpcodeStartDWORD);
	bool IsExtended = GetBitsFromWord<31, 31>(OpcodeStartDWORD) != 0;

	OpCode.Type = (D3DOpcodeType)OpCodeType;

	if (OpCode.Type != D3DOpcodeType_SAMPLE_L)
	{
		ASSERT(!IsExtended);
	}

	if (OpCode.Type == D3DOpcodeType_DCL_GLOBAL_FLAGS)
	{
		OpCode.GlobalFlagsDecl.IsRefactoringAllowed = GetBitsFromWord<11, 11>(OpcodeStartDWORD) != 0;
		OpCode.GlobalFlagsDecl.EnableDoublePrecision = GetBitsFromWord<12, 12>(OpcodeStartDWORD) != 0;
	}
	else if (OpCode.Type == D3DOpcodeType_DCL_INPUT)
	{
		OpCode.InputDeclaration.Decl = ParseIODeclFromCursor(Cursor);
	}
	else if (OpCode.Type == D3DOpcodeType_DCL_OUTPUT)
	{
		OpCode.OutputDeclaration.Decl = ParseIODeclFromCursor(Cursor);
	}
	else if (OpCode.Type == D3DOpcodeType_DCL_OUTPUT_SIV)
	{
		OpCode.OutputDeclarationSIV.Decl = ParseIODeclFromCursor(Cursor);
		uint32 SemanticDWORD = GetValueFromCursor<uint32>(Cursor);
		OpCode.OutputDeclarationSIV.Semantic = GetBitsFromWord<0, 15, OperandSemantic>(SemanticDWORD);
	}
	else if (OpCode.Type == D3DOpcodeType_DCL_TEMPS)
	{
		OpCode.TempRegistersDeclaration.NumTemps = GetValueFromCursor<uint32>(Cursor);
	}
	else if (OpCode.Type == D3DOpcodeType_DCL_CONSTANT_BUFFER)
	{
		OpCode.CBVDeclaration.IsDynamicIndexed = GetBitsFromWord<11, 11>(OpcodeStartDWORD) != 0;
		auto Decl = ParseIODeclFromCursor(Cursor);
		ASSERT(Decl.SrcDimension == OperandSourceIndexDimension_2D);
		OpCode.CBVDeclaration.CBVRegIndex = (uint32)Decl.SrcIndicesValues[0];
		OpCode.CBVDeclaration.CBVSize = (uint32)Decl.SrcIndicesValues[1];
	}
	else if (OpCode.Type == D3DOpcodeType_DCL_SAMPLER)
	{
		OpCode.SamplerDeclaration.SamplerMode = GetBitsFromWord<11, 14, SamplerMode>(OpcodeStartDWORD);
		auto Decl = ParseIODeclFromCursor(Cursor);
		ASSERT(Decl.SrcDimension == OperandSourceIndexDimension_1D);
		OpCode.SamplerDeclaration.SamplerRegister = (uint32)Decl.SrcIndicesValues[0];
	}
	else if (OpCode.Type == D3DOpcodeType_DCL_RESOURCE)
	{
		ResourceDimension ResDim = GetBitsFromWord<11, 15, ResourceDimension>(OpcodeStartDWORD);
		uint32 SampleCount = GetBitsFromWord<16, 22>(OpcodeStartDWORD);
		ASSERT(SampleCount == 0 || SampleCount == 1);

		auto Decl = ParseIODeclFromCursor(Cursor);
		ASSERT(Decl.SrcDimension == OperandSourceIndexDimension_1D);

		OpCode.ResourceDeclaration.TextureRegIndex = (uint32)Decl.SrcIndicesValues[0];
		OpCode.ResourceDeclaration.Dimension = ResDim;
		OpCode.ResourceDeclaration.SampleCount = SampleCount;

		uint32 ReturnTypeDWORD = GetValueFromCursor<uint32>(Cursor);
		for (int32 i = 0; i < 4; i++)
		{
			OpCode.ResourceDeclaration.ReturnType[i] = GetBitsFromWord<ResourceReturnType>(ReturnTypeDWORD, 4 * i, 4 * i + 3);
		}
	}
	else if (IsOpcodeGenericTernaryOp(OpCode.Type))
	{
		OpCode.TernaryGeneralOp.Output = ParseIODeclFromCursor(Cursor);
		OpCode.TernaryGeneralOp.Src1 = ParseIODeclFromCursor(Cursor);
		OpCode.TernaryGeneralOp.Src2 = ParseIODeclFromCursor(Cursor);
		OpCode.TernaryGeneralOp.Src3 = ParseIODeclFromCursor(Cursor);
	}
	else if (IsOpcodeGenericBinaryOp(OpCode.Type))
	{
		OpCode.BinaryGeneralOp.Output = ParseIODeclFromCursor(Cursor);
		OpCode.BinaryGeneralOp.Src1 = ParseIODeclFromCursor(Cursor);
		OpCode.BinaryGeneralOp.Src2 = ParseIODeclFromCursor(Cursor);
	}
	else if (IsOpcodeGenericUnaryOp(OpCode.Type))
	{

		OpCode.UnaryGeneralOp.Output = ParseIODeclFromCursor(Cursor);
		OpCode.UnaryGeneralOp.Src = ParseIODeclFromCursor(Cursor);

		//ASSERT(false);
	}
	else if (OpCode.Type == D3DOpcodeType_SAMPLE_L)
	{
		uint32 SecondDWORD = GetValueFromCursor<uint32>(Cursor);
		uint32 ThirdDWORD = GetValueFromCursor<uint32>(Cursor);

		ASSERT(IsExtended);
		ASSERT((GetBitsFromWord<31, 31>(SecondDWORD) != 0));
		ASSERT((GetBitsFromWord<31, 31>(ThirdDWORD) == 0));

		auto SecondExtensionType = GetBitsFromWord<0, 5, OpcodeExtensionType>(SecondDWORD);
		auto ThirdExtensionType = GetBitsFromWord<0, 5, OpcodeExtensionType>(ThirdDWORD);

		ASSERT(SecondExtensionType == OpcodeExtensionType_ResourceDim);
		ASSERT(ThirdExtensionType == OpcodeExtensionType_ResourceReturnType);

		OpCode.SampleLOp.Dimension = GetBitsFromWord<6, 10, ResourceDimension>(SecondDWORD);
		OpCode.SampleLOp.ResourceStride = GetBitsFromWord<11, 15>(SecondDWORD);

		ResourceReturnType RetTypes[4] = {};
		for (int32 i = 0; i < 4; i++)
		{
			OpCode.SampleLOp.ReturnType[i] = GetBitsFromWord<ResourceReturnType>(ThirdDWORD, 6 + 4 * i, 9 + 4 * i);
		}

		//auto Decl1 = ParseIODeclFromCursor(Cursor);
		OpCode.SampleLOp.OutputReg = ParseIODeclFromCursor(Cursor);
		OpCode.SampleLOp.UVReg = ParseIODeclFromCursor(Cursor);
		OpCode.SampleLOp.TextureReg = ParseIODeclFromCursor(Cursor);
		OpCode.SampleLOp.SamplerReg = ParseIODeclFromCursor(Cursor);
		OpCode.SampleLOp.MipLevel = ParseIODeclFromCursor(Cursor);

		ASSERT(false);
	}
	else if (OpCode.Type == D3DOpcodeType_RET)
	{
		// Nothing
	}
	else
	{
		ASSERT(false);
	}

	ASSERT(*Cursor == OrigCursor + OpCodeLength * 4);

	return OpCode;
}

void ParseDXBCCode(byte* Code, int32 Length)
{
	ASSERT(Length >= sizeof(DXBCFileHeader));


	byte OurHash[16] = {};
	dxbcHash(Code + 20, Length - 20, OurHash);

	byte TheirHash[16] = {};
	memcpy(TheirHash, Code + 4, 16);

	ASSERT(memcmp(OurHash, TheirHash, 16) == 0);

	byte* Cursor = Code;

	DXBCFileHeader Header = GetValueFromCursor<DXBCFileHeader>(&Cursor);

	ASSERT(Header.MagicNumbers[0] == 'D');
	ASSERT(Header.MagicNumbers[1] == 'X');
	ASSERT(Header.MagicNumbers[2] == 'B');
	ASSERT(Header.MagicNumbers[3] == 'C');
	ASSERT(Header.FileSizeInBytes == Length);

	std::vector<uint32> ChunkOffsets;

	for (int32 i = 0; i < Header.ChunkCount; i++)
	{
		ChunkOffsets.push_back(GetValueFromCursor<uint32>(&Cursor));
	}

	for (uint32 ChunkOffset : ChunkOffsets)
	{
		byte* ChunkData = Code + ChunkOffset;
		byte* ChunkCursor = ChunkData;

		char ChunkHeaderType[5] = {};
		memcpy(ChunkHeaderType, ChunkCursor, 4);
		ChunkCursor += 4;
		ChunkHeaderType[4] = '\0';

		uint32 ChunkLength = GetValueFromCursor<uint32>(&ChunkCursor);

		byte* ChunkDataAfterHeader = ChunkCursor;

		LOG("Chunk of type '%s' with length %d", ChunkHeaderType, ChunkLength);

		if (memcmp(ChunkHeaderType, "RDEF", 4) == 0)
		{
			uint32 CBCount = GetValueFromCursor<uint32>(&ChunkCursor);
			uint32 CBByteOffset = GetValueFromCursor<uint32>(&ChunkCursor);

			uint32 ResourceCount = GetValueFromCursor<uint32>(&ChunkCursor);
			uint32 ResourceByteOffset = GetValueFromCursor<uint32>(&ChunkCursor);

			uint32 MinorVersion = GetValueFromCursor<byte>(&ChunkCursor);
			uint32 MajorVersion = GetValueFromCursor<byte>(&ChunkCursor);

			uint16 ProgramType = GetValueFromCursor<uint16>(&ChunkCursor);
			uint32 ProgramFlags = GetValueFromCursor<uint32>(&ChunkCursor);

			uint32 OffsetToCreatorString = GetValueFromCursor<uint32>(&ChunkCursor);

			byte* CreatorStringData = ChunkDataAfterHeader + OffsetToCreatorString;

			byte* ConstantBufferData = ChunkDataAfterHeader + CBByteOffset;
			byte* CBCursor = ConstantBufferData;
			for (int32 CBIdx = 0; CBIdx < CBCount; CBIdx++)
			{

				uint32 CBNameOffset = GetValueFromCursor<uint32>(&CBCursor);
				uint32 CBVarCount = GetValueFromCursor<uint32>(&CBCursor);
				uint32 CBVarDescOffset = GetValueFromCursor<uint32>(&CBCursor);
				uint32 CBSize = GetValueFromCursor<uint32>(&CBCursor);
				uint32 CBFlags = GetValueFromCursor<uint32>(&CBCursor);
				uint32 CBType = GetValueFromCursor<uint32>(&CBCursor);

				byte* CBNameValue = ChunkDataAfterHeader + CBNameOffset;

				byte* VarData = ChunkDataAfterHeader + CBVarDescOffset;
				byte* VarCursor = VarData;
				for (int32 VarIdx = 0; VarIdx < CBVarCount; VarIdx++)
				{
					uint32 VarNameOffset = GetValueFromCursor<uint32>(&VarCursor);
					uint32 OffsetInCBBuffer = GetValueFromCursor<uint32>(&VarCursor);
					uint32 VariableSize = GetValueFromCursor<uint32>(&VarCursor);
					uint32 VariableFlags = GetValueFromCursor<uint32>(&VarCursor);
					uint32 VarTypeOffset = GetValueFromCursor<uint32>(&VarCursor);
					uint32 VarDefaultValueOffset = GetValueFromCursor<uint32>(&VarCursor);

					uint32 Unknown[4] = {};
					for (int32 i = 0; i < 4; i++)
					{
						Unknown[i] = GetValueFromCursor<uint32>(&VarCursor);
					}

					byte* VarNameCursor = ChunkDataAfterHeader + VarNameOffset;

					byte* VarTypeCursor = ChunkDataAfterHeader + VarTypeOffset;
					uint16 VarClass = GetValueFromCursor<uint16>(&VarTypeCursor);
					uint16 VarType = GetValueFromCursor<uint16>(&VarTypeCursor);
					uint16 VarRowCount = GetValueFromCursor<uint16>(&VarTypeCursor);
					uint16 VarColCount = GetValueFromCursor<uint16>(&VarTypeCursor);
					uint16 VarArrayCount = GetValueFromCursor<uint16>(&VarTypeCursor);
					uint16 VarMemberCount = GetValueFromCursor<uint16>(&VarTypeCursor);
					uint16 VarMemberOffset = GetValueFromCursor<uint16>(&VarTypeCursor);
				}

			}


			byte* ResourceBindingData = ChunkDataAfterHeader + ResourceByteOffset;
			byte* RBCursor = ResourceBindingData;
			for (int32 ResourceIdx = 0; ResourceIdx < ResourceCount; ResourceIdx++)
			{
				uint32 ResourceNameOffset = GetValueFromCursor<uint32>(&RBCursor);

				// 0 = cbuffer
				// 2 = texture
				// 3 = sampler
				uint32 ResourceType = GetValueFromCursor<uint32>(&RBCursor);
				uint32 ResourceReturnType = GetValueFromCursor<uint32>(&RBCursor);
				uint32 ResourceViewDimension = GetValueFromCursor<uint32>(&RBCursor);
				uint32 ResourceNumSamples = GetValueFromCursor<uint32>(&RBCursor);
				uint32 ResourceBindPoint = GetValueFromCursor<uint32>(&RBCursor);
				uint32 ResourceBindCount = GetValueFromCursor<uint32>(&RBCursor);
				uint32 ResourceFlags = GetValueFromCursor<uint32>(&RBCursor);

				byte* ResourceNameCursor = ChunkDataAfterHeader + ResourceNameOffset;

				int xc = 0;
				xc++;
				(void)xc;
			}
		}
		// TODO: Same of OSGN, except for...output not input
		else if (memcmp(ChunkHeaderType, "ISGN", 4) == 0)
		{
			uint32 ElementCount = GetValueFromCursor<uint32>(&ChunkCursor);
			uint32 Unknown = GetValueFromCursor<uint32>(&ChunkCursor);

			for (int32 ElementIdx = 0; ElementIdx < ElementCount; ElementIdx++)
			{
				uint32 ElementNameOffset = GetValueFromCursor<uint32>(&ChunkCursor);
				uint32 ElementSemanticIndex = GetValueFromCursor<uint32>(&ChunkCursor);
				uint32 ElementValueType = GetValueFromCursor<uint32>(&ChunkCursor);
				uint32 ElementComponentType = GetValueFromCursor<uint32>(&ChunkCursor);
				uint32 ElementRegister = GetValueFromCursor<uint32>(&ChunkCursor);
				byte ElementMask = GetValueFromCursor<byte>(&ChunkCursor);
				byte ElementRWMask = GetValueFromCursor<byte>(&ChunkCursor);

				uint16 Unknown = GetValueFromCursor<uint16>(&ChunkCursor);

				byte* ElementName = ChunkDataAfterHeader + ElementNameOffset;

				int xc = 0;
				xc++;
				(void)xc;
			}
		}
		else if (memcmp(ChunkHeaderType, "SHEX", 4) == 0)
		{
			byte Version = GetValueFromCursor<byte>(&ChunkCursor);
			byte Padding = GetValueFromCursor<byte>(&ChunkCursor);
			uint16 ProgramType = GetValueFromCursor<uint16>(&ChunkCursor);
			uint32 NumberDWORDs = GetValueFromCursor<uint32>(&ChunkCursor);

			byte* OpcodesStart = ChunkCursor;

			while (ChunkCursor < ChunkDataAfterHeader + (NumberDWORDs * 4))
			{
				D3DOpcode OpCode = GetD3DOpcodeFromCursor(&ChunkCursor);
				(void)OpCode;
			}

			int xc = 0;
			xc++;
			(void)xc;
		}
	}
}


//
// --------------------------------------
//

// Planning for an alternative means of generating bytecode

// TODO: Rename bytecode to opcodes? Bytecode includes metadata and other chunks


enum struct BytecodeRegisterType
{
	Temp,
	Input,
	Output,
	Sampler,
	Texture,
	ConstantBuffer,
	Count
};

struct BytecodeRegisterRef
{
	BytecodeRegisterType RegType = BytecodeRegisterType::Temp;
	int32 RegIndex = 0;

	static BytecodeRegisterRef Temp(int32 RegIndex) {
		BytecodeRegisterRef ret;
		ret.RegType = BytecodeRegisterType::Temp;
		ret.RegIndex = RegIndex;
		return ret;
	}

	static BytecodeRegisterRef Input(int32 RegIndex) {
		BytecodeRegisterRef ret;
		ret.RegType = BytecodeRegisterType::Input;
		ret.RegIndex = RegIndex;
		return ret;
	}

	static BytecodeRegisterRef Output(int32 RegIndex) {
		BytecodeRegisterRef ret;
		ret.RegType = BytecodeRegisterType::Output;
		ret.RegIndex = RegIndex;
		return ret;
	}

	static BytecodeRegisterRef Sampler(int32 RegIndex) {
		BytecodeRegisterRef ret;
		ret.RegType = BytecodeRegisterType::Sampler;
		ret.RegIndex = RegIndex;
		return ret;
	}

	static BytecodeRegisterRef Texture(int32 RegIndex) {
		BytecodeRegisterRef ret;
		ret.RegType = BytecodeRegisterType::Texture;
		ret.RegIndex = RegIndex;
		return ret;
	}
};

struct BytecodeImmediateValue
{
	float Values[4] = {};

	BytecodeImmediateValue() { }
	BytecodeImmediateValue(float x, float y, float z, float w)
	{

	}
};

enum struct BytecodeOperandType
{
	Register,
	ImmediateInt1,
	ImmediateFloat1,
	ImmediateFloat4,
};

enum struct BytecodeOperandSwizzle {
	X,
	Y,
	Z,
	W
};

struct BytecodeOperandSwizzling {
	byte Swizzling[4] = {0x00, 0x01, 0x02, 0x03};

	BytecodeOperandSwizzling() {
		Swizzling[0] = 0;
		Swizzling[1] = 1;
		Swizzling[2] = 2;
		Swizzling[3] = 3;
	}

	BytecodeOperandSwizzling(BytecodeOperandSwizzle InX, BytecodeOperandSwizzle InY, BytecodeOperandSwizzle InZ, BytecodeOperandSwizzle InW)
	{
		Swizzling[0] = (byte)InX;
		Swizzling[1] = (byte)InY;
		Swizzling[2] = (byte)InZ;
		Swizzling[3] = (byte)InW;
	}
};

struct BytecodeOperand {

	BytecodeOperandType Type;

	byte Mask = 0x0F;
	BytecodeOperandSwizzling Swizzling;

	union
	{
		BytecodeRegisterRef Register;
		int32 Int1;
		float Float1;
		BytecodeImmediateValue Float4;
	};

	BytecodeOperand() { }

	static BytecodeOperand OpRegister(BytecodeRegisterRef Register, BytecodeOperandSwizzling Swizzle = BytecodeOperandSwizzling(), byte Mask = 0x0F) {
		BytecodeOperand Operand;
		Operand.Type = BytecodeOperandType::Register;
		Operand.Register = Register;
		Operand.Swizzling = Swizzle;
		Operand.Mask = Mask;
		return Operand;
	}

	static BytecodeOperand OpInt1(int32 Val) {
		BytecodeOperand Operand;
		Operand.Type = BytecodeOperandType::ImmediateInt1;
		Operand.Int1 = Val;
		return Operand;
	}

	//static BytecodeOperand OpFloat1(float Val) {
	//	BytecodeOperand Operand;
	//	Operand.Type = BytecodeOperandType::ImmediateFloat1;
	//	Operand.Float1 = Val;
	//	return Operand;
	//}

	static BytecodeOperand OpFloat4(BytecodeImmediateValue Val, BytecodeOperandSwizzling Swizzle = BytecodeOperandSwizzling(), byte Mask = 0x0F) {
		BytecodeOperand Operand;
		Operand.Type = BytecodeOperandType::ImmediateFloat4;
		Operand.Float4 = Val;
		Operand.Swizzling = Swizzle;
		Operand.Mask = Mask;
		return Operand;
	}
};

void ShaderDeclareGlobalFlags(D3DOpcodeState* Bytecode, bool isRefactoringAllowed)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10, D3DOpcodeType>(&OpcodeDWORD, D3DOpcodeType_DCL_GLOBAL_FLAGS);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 1); // Set length to 1 DWORD (including this one)
	SetBitsFromWord<11, 11>(&OpcodeDWORD, isRefactoringAllowed ? 1 : 0); // Set refactoring allowed

	Bytecode->Opcodes.push_back(OpcodeDWORD);
}

OperandSourceType OperandSourceTypeFromRegisterType(BytecodeRegisterType RegisterType)
{
	OperandSourceType SourceType = OperandSourceType_Count;
	if (RegisterType == BytecodeRegisterType::Temp)
	{
		SourceType = OperandSourceType_TempRegister;
	}
	else if (RegisterType == BytecodeRegisterType::Input)
	{
		SourceType = OperandSourceType_InputRegister;
	}
	else if (RegisterType == BytecodeRegisterType::Output)
	{
		SourceType = OperandSourceType_OutputRegister;
	}
	else if (RegisterType == BytecodeRegisterType::Texture)
	{
		SourceType = OperandSourceType_Resource;
	}
	else if (RegisterType == BytecodeRegisterType::Sampler)
	{
		SourceType = OperandSourceType_Sampler;
	}
	else if (RegisterType == BytecodeRegisterType::ConstantBuffer)
	{
		SourceType = OperandSourceType_ConstantBuffer;
	}
	else
	{
		ASSERT(false);
	}

	return SourceType;
}

void ShaderWriteOperand(D3DOpcodeState* Bytecode, const BytecodeOperand& Op)
{
	if (Op.Type == BytecodeOperandType::Register)
	{
		uint32 HeaderDWORD = 0;
		if (Op.Register.RegType == BytecodeRegisterType::Sampler)
		{
			SetBitsFromWord<0, 1>(&HeaderDWORD, OperandNumComponents_Zero);
		}
		else
		{
			SetBitsFromWord<0, 1>(&HeaderDWORD, OperandNumComponents_Four);
			//if (Op.Mask == 0x0F)
			//{
			//	SetBitsFromWord<2, 3>(&HeaderDWORD, Operand4CompSelection_Swizzle);
			//	for (int32 i = 0; i < 4; i++)
			//	{
			//		SetBitsFromWord(&HeaderDWORD, 4 + 2 * i, 5 + 2 * i, Op.Swizzling.Swizzling[i]);
			//	}
			//}
			//else
			{
				SetBitsFromWord<4, 7>(&HeaderDWORD, Op.Mask);
			}
		}

		SetBitsFromWord<12, 19>(&HeaderDWORD, OperandSourceTypeFromRegisterType(Op.Register.RegType));
		SetBitsFromWord<20, 21>(&HeaderDWORD, OperandSourceIndexDimension_1D);
		SetBitsFromWord<22, 24>(&HeaderDWORD, OperandSourceIndexRepr_Imm32);

		Bytecode->Opcodes.push_back(HeaderDWORD);
		Bytecode->Opcodes.push_back(Op.Register.RegIndex);
	}
	else
	{
		ASSERT(false);
	}
}

void ShaderDeclareInput(D3DOpcodeState* Bytecode, int32 RegisterIndex, byte InputMask = 0x0F)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_DCL_INPUT);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 3); // Set length to 3 DWORDs (including this one)
	Bytecode->Opcodes.push_back(OpcodeDWORD);
	
	auto Operand = BytecodeOperand::OpRegister(BytecodeRegisterRef::Input(RegisterIndex), BytecodeOperandSwizzling(), InputMask);
	ShaderWriteOperand(Bytecode, Operand);
}

void ShaderDeclareOutput(D3DOpcodeState* Bytecode, int32 RegisterIndex, byte OutputMask = 0x0F)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_DCL_OUTPUT);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 3); // Set length to 3 DWORDs (including this one)
	Bytecode->Opcodes.push_back(OpcodeDWORD);

	auto Operand = BytecodeOperand::OpRegister(BytecodeRegisterRef::Input(RegisterIndex), BytecodeOperandSwizzling(), OutputMask);
	ShaderWriteOperand(Bytecode, Operand);
}

void ShaderDeclareOutput_SIV(D3DOpcodeState* Bytecode, int32 RegisterIndex, OperandSemantic Semantic, byte OutputMask = 0x0F)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_DCL_OUTPUT_SIV);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 4); // Set length to 4 DWORDs (including this one)
	Bytecode->Opcodes.push_back(OpcodeDWORD);

	auto Operand = BytecodeOperand::OpRegister(BytecodeRegisterRef::Output(RegisterIndex), BytecodeOperandSwizzling(), OutputMask);
	ShaderWriteOperand(Bytecode, Operand);

	{
		uint32 SemanticDWORD = 0;
		SetBitsFromWord<0, 15>(&OpcodeDWORD, Semantic);
	}

	Bytecode->Opcodes.push_back(OpcodeDWORD);
}

void ShaderDeclareCBVImm(D3DOpcodeState* Bytecode, int32 RegisterIndex, int32 SizeInBytes)
{

}

void ShaderDeclareTextureResource(D3DOpcodeState* Bytecode, int32 RegisterIndex)
{

}

void ShaderDeclareSampler(D3DOpcodeState* Bytecode, int32 RegisterIndex /*TODO: Mode?*/)
{

}

void ShaderDoMov(D3DOpcodeState* Bytecode, BytecodeOperand Src, BytecodeOperand Dst)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_MOV);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 5); // Set length to 3 DWORDs (including this one)
	Bytecode->Opcodes.push_back(OpcodeDWORD);

	ASSERT(Dst.Type == BytecodeOperandType::Register);

	ShaderWriteOperand(Bytecode, Dst);
	ShaderWriteOperand(Bytecode, Src);
}

void ShaderAddReg(D3DOpcodeState* Bytecode, BytecodeOperand Src1, BytecodeOperand Src2, BytecodeRegisterRef Dst)
{

}

void ShaderFmad(D3DOpcodeState* Bytecode, BytecodeOperand AddSrc, BytecodeOperand MulSrc1, BytecodeOperand MulSrc2, BytecodeRegisterRef Dst)
{

}

void ShaderMul(D3DOpcodeState* Bytecode, BytecodeOperand Src1, BytecodeOperand Src2, BytecodeRegisterRef Dst)
{

}

void ShaderSampleTextureLevel0(D3DOpcodeState* Bytecode, BytecodeRegisterRef Tex, BytecodeRegisterRef Sampler, BytecodeOperand UVs, BytecodeRegisterRef Dst, float MipsLevel)
{
	
}

void ShaderReturn(D3DOpcodeState* Bytecode)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_RET);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 1);
	Bytecode->Opcodes.push_back(OpcodeDWORD);
}


// TODO: Also take shader description (resources, parameters, etc)
void GenerateBytecodeOpcodes(FuzzDXBCState* DXBCState, D3DOpcodeState* Bytecode)
{
	Bytecode->NextWrittenTempRegister = 0;
	Bytecode->TempRegisterClobberMask = 0;
	Bytecode->Opcodes.clear();

	// Blind reserve
	Bytecode->Opcodes.reserve(1024);


	// Generate declarations
	ShaderDeclareGlobalFlags(Bytecode, true);
	ShaderDeclareInput(Bytecode, 0);
	ShaderDeclareOutput_SIV(Bytecode, 0, OperandSemantic_Position);
	ShaderDoMov(Bytecode, BytecodeOperand::OpRegister(BytecodeRegisterRef::Input(0)), BytecodeOperand::OpRegister(BytecodeRegisterRef::Output(0)));
	ShaderReturn(Bytecode);

	//int32 NumOpcodes = 0;
	//for (int32 OpcodeIndex = 0; OpcodeIndex < NumOpcodes; OpcodeIndex++)
	//{
	//	// Generate an opcode
	//}

	
	//byte* Cursor = (byte*)Bytecode->Opcodes.data();
	//for (int32 i = 0; i < 5; i++)
	//{
	//	D3DOpcode OpCode = GetD3DOpcodeFromCursor(&Cursor);
	//
	//	ASSERT(false);
	//}

	//WriteDataToFile("../manual_bytecode/test_raw_01.bin", Bytecode->Opcodes.data(), Bytecode->Opcodes.size() * sizeof(uint32));
}

void RandomiseShaderBytecodeParams(FuzzDXBCState* DXBCState, D3DOpcodeState* VSOpcodes, D3DOpcodeState* PSOpcodes)
{
	VSOpcodes->InputSemantics.push_back(ShaderSemantic::POSITION);
	VSOpcodes->OutputSemantics.push_back(ShaderSemantic::SV_POSITION);

	PSOpcodes->InputSemantics.push_back(ShaderSemantic::SV_POSITION);
	PSOpcodes->OutputSemantics.push_back(ShaderSemantic::SV_TARGET);

	// TODO: Add random ones as well
}

void GenerateRDEFChunk(D3DOpcodeState* Opcodes, std::vector<unsigned char>* OutData)
{

}

void GenerateShaderBytecode(D3DOpcodeState* Opcodes, std::vector<unsigned char>* OutBytecode)
{
	DXBCFileHeader DXBCHeader = {};
	DXBCHeader.ChunkCount = 6; // ?
	DXBCHeader.FileSizeInBytes = 0;
	DXBCHeader.Unknown = 0;




	// Fix up the header now that we know the length/checksum, and 
}


void GenerateShaderDXBC(FuzzDXBCState* DXBCState)
{
	D3DOpcodeState VertShader;
	VertShader.ShaderType = D3DShaderType::Vertex;

	D3DOpcodeState PixelShader;
	PixelShader.ShaderType = D3DShaderType::Pixel;

	RandomiseShaderBytecodeParams(DXBCState, &VertShader, &PixelShader);

	GenerateBytecodeOpcodes(DXBCState, &VertShader);
	GenerateBytecodeOpcodes(DXBCState, &PixelShader);

	std::vector<unsigned char> VSBytecode;
	std::vector<unsigned char> PSBytecode;

	GenerateShaderBytecode(&VertShader, &VSBytecode);
	GenerateShaderBytecode(&PixelShader, &PSBytecode);

	HRESULT hr;
	hr = D3DCreateBlob(VSBytecode.size(), &DXBCState->VSBlob);
	ASSERT(SUCCEEDED(hr));
	memcpy(DXBCState->VSBlob->GetBufferPointer(), VSBytecode.data(), VSBytecode.size());

	hr = D3DCreateBlob(PSBytecode.size(), &DXBCState->PSBlob);
	ASSERT(SUCCEEDED(hr));
	memcpy(DXBCState->PSBlob->GetBufferPointer(), PSBytecode.data(), PSBytecode.size());
}


