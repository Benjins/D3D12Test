
#include "fuzz_dxbc.h"

#include "shader_meta.h"

#include "dxbc_hash.h"

#include "string_stack_buffer.h"

#include <vector>

#pragma pack(push)
#pragma pack(1)

struct DXBCFileHeader
{
	char MagicNumbers[4] = {};
	byte CheckSum[16] = {};
	uint32 Unknown = 0x01;
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

inline OperandSemantic ShaderSemanticToOperandSemantic(ShaderSemantic Sementic)
{
	if (Sementic == ShaderSemantic::SV_POSITION)
	{
		return OperandSemantic_Position;
	}
	else
	{
		return OperandSemantic_Undefined;
	}
}

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

enum PSInputInterpolationMode
{
	PSInputInterpolationMode_Undefined,
	PSInputInterpolationMode_Constant,
	PSInputInterpolationMode_Linear,
	PSInputInterpolationMode_LinearCentroid,
	PSInputInterpolationMode_LinearNoPerspective,
	PSInputInterpolationMode_LinearNoPerspectiveCentroid,
	PSInputInterpolationMode_LinearSample,
	PSInputInterpolationMode_LinearNoPerspectiveSample,
	PSInputInterpolationMode_Count
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
			IODecl Decl;
			OperandSemantic Semantic;
			PSInputInterpolationMode InterpolationMode;
		} PSInputDeclarationSIV;

		struct {
			IODecl Decl;
			PSInputInterpolationMode InterpolationMode;
		} PSInputDeclaration;

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
	else if (OpCode.Type == D3DOpcodeType_DCL_INPUT_PS_SIV)
	{
		OpCode.PSInputDeclarationSIV.Decl = ParseIODeclFromCursor(Cursor);
		uint32 SemanticDWORD = GetValueFromCursor<uint32>(Cursor);
		OpCode.PSInputDeclarationSIV.Semantic = GetBitsFromWord<0, 15, OperandSemantic>(SemanticDWORD);
		OpCode.PSInputDeclarationSIV.InterpolationMode = GetBitsFromWord<11, 14, PSInputInterpolationMode>(OpcodeStartDWORD);
	}
	else if (OpCode.Type == D3DOpcodeType_DCL_INPUT_PS)
	{
		OpCode.PSInputDeclaration.Decl = ParseIODeclFromCursor(Cursor);
		OpCode.PSInputDeclaration.InterpolationMode = GetBitsFromWord<11, 14, PSInputInterpolationMode>(OpcodeStartDWORD);
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

		//ASSERT(false);
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

					uint16 UnknownStuff1 = GetValueFromCursor<uint16>(&VarTypeCursor);
					uint32 UnknownStuff2 = GetValueFromCursor<uint32>(&VarTypeCursor);
					uint32 UnknownStuff3 = GetValueFromCursor<uint32>(&VarTypeCursor);
					uint32 UnknownStuff4 = GetValueFromCursor<uint32>(&VarTypeCursor);
					uint32 UnknownStuff5 = GetValueFromCursor<uint32>(&VarTypeCursor);
					uint32 VarTypeNameOffset = GetValueFromCursor<uint32>(&VarTypeCursor);
					byte* VarTypeNameCursor = ChunkDataAfterHeader + VarTypeNameOffset;

					int xc = 0;
					xc++;
					(void)xc;
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

	static BytecodeRegisterRef ConstantBuffer(int32 RegIndex) {
		BytecodeRegisterRef ret;
		ret.RegType = BytecodeRegisterType::ConstantBuffer;
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
		Values[0] = x;
		Values[1] = y;
		Values[2] = z;
		Values[3] = w;
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

	bool IsDefault() const
	{
		for (int32 i = 0; i < 4; i++)
		{
			if (Swizzling[i] != i)
			{
				return false;
			}
		}

		return true;
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

	// Only for registers of type Constant Buffer
	// Something something type system
	int32 CBVAccessIndex = 0;

	BytecodeOperand() { }

	static BytecodeOperand OpRegister(BytecodeRegisterRef Register, BytecodeOperandSwizzling Swizzle = BytecodeOperandSwizzling(), byte Mask = 0x0F) {
		BytecodeOperand Operand;
		Operand.Type = BytecodeOperandType::Register;
		Operand.Register = Register;
		Operand.Swizzling = Swizzle;
		Operand.Mask = Mask;
		return Operand;
	}

	static BytecodeOperand OpCBV(BytecodeRegisterRef Register, int32 Index, BytecodeOperandSwizzling Swizzle = BytecodeOperandSwizzling(), byte Mask = 0x0F) {
		BytecodeOperand Operand;
		Operand.Type = BytecodeOperandType::Register;
		Operand.Register = Register;
		Operand.Swizzling = Swizzle;
		Operand.Mask = Mask;
		Operand.CBVAccessIndex = Index;
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

	static BytecodeOperand OpFloat1(float Val) {
		BytecodeOperand Operand;
		Operand.Type = BytecodeOperandType::ImmediateFloat1;
		Operand.Float1 = Val;
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

OperandNumComponents NumComponentsInOperand(const BytecodeOperand& Op, bool IsDeclaration)
{
	if (Op.Type == BytecodeOperandType::ImmediateFloat1)
	{
		return OperandNumComponents_One;
	}
	else if (Op.Type == BytecodeOperandType::ImmediateFloat4)
	{
		return OperandNumComponents_Four;
	}
	else if (Op.Type == BytecodeOperandType::Register)
	{
		if (Op.Register.RegType == BytecodeRegisterType::Sampler)
		{
			return OperandNumComponents_Zero;
		}
		else if (Op.Register.RegType == BytecodeRegisterType::Texture)
		{
			return IsDeclaration ? OperandNumComponents_Zero : OperandNumComponents_Four;
		}
		else
		{
			return OperandNumComponents_Four;
		}
	}
	else
	{
		ASSERT(false);
		return OperandNumComponents_Zero;
	}
}

void ShaderWriteOperand(D3DOpcodeState* Bytecode, const BytecodeOperand& Op, bool IsDeclaration)
{
	uint32 HeaderDWORD = 0;
	auto NumComponents = NumComponentsInOperand(Op, IsDeclaration);
	if (NumComponents == OperandNumComponents_Four)
	{
		// I'm not sure if it's a restriction with dxilconv, or with the graphics drivers, but if we're sampling from a texture, the operand needs
		// to explicitly state the swizzle, using a mask isn't sufficient
		bool ShouldForceSwizzle = (Op.Type == BytecodeOperandType::Register) && (Op.Register.RegType == BytecodeRegisterType::Texture);
		if (!Op.Swizzling.IsDefault() || ShouldForceSwizzle)
		{
			SetBitsFromWord<2, 3>(&HeaderDWORD, Operand4CompSelection_Swizzle);
			for (int32 i = 0; i < 4; i++)
			{
				SetBitsFromWord(&HeaderDWORD, 4 + 2 * i, 5 + 2 * i, Op.Swizzling.Swizzling[i]);
			}
		}
		else
		{
			SetBitsFromWord<4, 7>(&HeaderDWORD, Op.Mask);
		}
	}

	SetBitsFromWord<0, 1>(&HeaderDWORD, NumComponents);

	if (Op.Type == BytecodeOperandType::Register)
	{
		if (Op.Register.RegType == BytecodeRegisterType::ConstantBuffer)
		{
			SetBitsFromWord<12, 19>(&HeaderDWORD, OperandSourceTypeFromRegisterType(Op.Register.RegType));
			SetBitsFromWord<20, 21>(&HeaderDWORD, OperandSourceIndexDimension_2D);
			SetBitsFromWord<22, 24>(&HeaderDWORD, OperandSourceIndexRepr_Imm32);

			Bytecode->Opcodes.push_back(HeaderDWORD);
			Bytecode->Opcodes.push_back(Op.Register.RegIndex);
			Bytecode->Opcodes.push_back(Op.CBVAccessIndex);
		}
		else
		{
			SetBitsFromWord<12, 19>(&HeaderDWORD, OperandSourceTypeFromRegisterType(Op.Register.RegType));
			SetBitsFromWord<20, 21>(&HeaderDWORD, OperandSourceIndexDimension_1D);
			SetBitsFromWord<22, 24>(&HeaderDWORD, OperandSourceIndexRepr_Imm32);

			Bytecode->Opcodes.push_back(HeaderDWORD);
			Bytecode->Opcodes.push_back(Op.Register.RegIndex);
		}
	}
	else if (Op.Type == BytecodeOperandType::ImmediateFloat4)
	{
		SetBitsFromWord<12, 19>(&HeaderDWORD, OperandSourceType_Immediate32);
		Bytecode->Opcodes.push_back(HeaderDWORD);

		for (int32 i = 0; i < 4; i++)
		{
			Bytecode->Opcodes.push_back(*(uint32*)&Op.Float4.Values[i]);
		}
	}
	else if (Op.Type == BytecodeOperandType::ImmediateFloat1)
	{
		SetBitsFromWord<12, 19>(&HeaderDWORD, OperandSourceType_Immediate32);
		Bytecode->Opcodes.push_back(HeaderDWORD);

		Bytecode->Opcodes.push_back(*(uint32*)&Op.Float1);
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
	ShaderWriteOperand(Bytecode, Operand, true);
}

void ShaderDeclareOutput(D3DOpcodeState* Bytecode, int32 RegisterIndex, byte OutputMask = 0x0F)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_DCL_OUTPUT);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 3); // Set length to 3 DWORDs (including this one)
	Bytecode->Opcodes.push_back(OpcodeDWORD);

	auto Operand = BytecodeOperand::OpRegister(BytecodeRegisterRef::Output(RegisterIndex), BytecodeOperandSwizzling(), OutputMask);
	ShaderWriteOperand(Bytecode, Operand, true);
}

void ShaderDeclareOutput_SIV(D3DOpcodeState* Bytecode, int32 RegisterIndex, OperandSemantic Semantic, byte OutputMask = 0x0F)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_DCL_OUTPUT_SIV);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 4); // Set length to 4 DWORDs (including this one)
	Bytecode->Opcodes.push_back(OpcodeDWORD);

	auto Operand = BytecodeOperand::OpRegister(BytecodeRegisterRef::Output(RegisterIndex), BytecodeOperandSwizzling(), OutputMask);
	ShaderWriteOperand(Bytecode, Operand, true);

	{
		uint32 SemanticDWORD = 0;
		SetBitsFromWord<0, 15>(&OpcodeDWORD, Semantic);
	}

	Bytecode->Opcodes.push_back(OpcodeDWORD);
}

void ShaderDeclareInputPS(D3DOpcodeState* Bytecode, int32 RegisterIndex, PSInputInterpolationMode InterpolationMode, byte InputMask = 0x0F)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_DCL_INPUT_PS);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 3); // Set length to 3 DWORDs (including this one)
	SetBitsFromWord<11, 14>(&OpcodeDWORD, InterpolationMode);
	Bytecode->Opcodes.push_back(OpcodeDWORD);

	auto Operand = BytecodeOperand::OpRegister(BytecodeRegisterRef::Input(RegisterIndex), BytecodeOperandSwizzling(), InputMask);
	ShaderWriteOperand(Bytecode, Operand, true);
}

void ShaderDeclareInputPS_SIV(D3DOpcodeState* Bytecode, int32 RegisterIndex, OperandSemantic Semantic, PSInputInterpolationMode InterpolationMode, byte OutputMask = 0x0F)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_DCL_INPUT_PS_SIV);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 4); // Set length to 4 DWORDs (including this one)
	SetBitsFromWord<11, 14>(&OpcodeDWORD, InterpolationMode);
	Bytecode->Opcodes.push_back(OpcodeDWORD);

	auto Operand = BytecodeOperand::OpRegister(BytecodeRegisterRef::Input(RegisterIndex), BytecodeOperandSwizzling(), OutputMask);
	ShaderWriteOperand(Bytecode, Operand, true);

	{
		uint32 SemanticDWORD = 0;
		SetBitsFromWord<0, 15>(&OpcodeDWORD, Semantic);
	}

	Bytecode->Opcodes.push_back(OpcodeDWORD);
}

void ShaderDeclareCBVImm(D3DOpcodeState* Bytecode, int32 RegisterIndex, int32 SizeInBytes)
{

}

void ShaderDeclareTexture2DResource(D3DOpcodeState* Bytecode, int32 RegisterIndex)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_DCL_RESOURCE);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 4); // Set length to 3 DWORDs (including this one)

	SetBitsFromWord<11, 15>(&OpcodeDWORD, ResourceDimension_Texture2D);
	int32 SampleCount = 0;
	SetBitsFromWord<16, 22>(&OpcodeDWORD, SampleCount);

	Bytecode->Opcodes.push_back(OpcodeDWORD);

	auto Operand = BytecodeOperand::OpRegister(BytecodeRegisterRef::Texture(RegisterIndex));
	ShaderWriteOperand(Bytecode, Operand, true);

	uint32 ReturnTypeDWORD = 0;
	for (int32 i = 0; i < 4; i++)
	{
		SetBitsFromWord(&ReturnTypeDWORD, 4 * i, 4 * i + 3, ResourceReturnType_Float);
	}

	Bytecode->Opcodes.push_back(ReturnTypeDWORD);
}

void ShaderDeclareSampler(D3DOpcodeState* Bytecode, int32 RegisterIndex /*TODO: Mode?*/)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_DCL_SAMPLER);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 3); // Set length to 3 DWORDs (including this one)
	SetBitsFromWord<11, 14>(&OpcodeDWORD, SamplerMode_Default);

	Bytecode->Opcodes.push_back(OpcodeDWORD);

	auto Operand = BytecodeOperand::OpRegister(BytecodeRegisterRef::Sampler(RegisterIndex));
	ShaderWriteOperand(Bytecode, Operand, true);
}

void ShaderDeclareNumTempRegisters(D3DOpcodeState* Bytecode, int32 NumTempRegs)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_DCL_TEMPS);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 2); // Set length to 2 DWORDs (including this one)
	Bytecode->Opcodes.push_back(OpcodeDWORD);

	Bytecode->Opcodes.push_back(NumTempRegs);
}

void ShaderDeclareConstantBuffer(D3DOpcodeState* Bytecode, int32 CBVRegister, int32 CBVSize)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_DCL_CONSTANT_BUFFER);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 4); // Set length to 2 DWORDs (including this one)
	Bytecode->Opcodes.push_back(OpcodeDWORD);

	auto Operand = BytecodeOperand::OpCBV(BytecodeRegisterRef::ConstantBuffer(CBVRegister), CBVSize);
	ShaderWriteOperand(Bytecode, Operand, true);
}

void ShaderDoMov(D3DOpcodeState* Bytecode, BytecodeOperand Src, BytecodeOperand Dst)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_MOV);

	int32 OpcodeFirstWordIndex = Bytecode->Opcodes.size();
	Bytecode->Opcodes.push_back(OpcodeDWORD);

	ASSERT(Dst.Type == BytecodeOperandType::Register);

	ShaderWriteOperand(Bytecode, Dst, false);
	ShaderWriteOperand(Bytecode, Src, false);

	// Set op code length
	SetBitsFromWord<24, 30>(&Bytecode->Opcodes[OpcodeFirstWordIndex], Bytecode->Opcodes.size() - OpcodeFirstWordIndex);
}

void ShaderPerformBinaryOp(D3DOpcodeState* Bytecode, D3DOpcodeType OpcodeType, BytecodeOperand Src1, BytecodeOperand Src2, BytecodeOperand Dst)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, OpcodeType);
	
	int32 OpcodeFirstWordIndex = Bytecode->Opcodes.size();
	Bytecode->Opcodes.push_back(OpcodeDWORD);

	ASSERT(Dst.Type == BytecodeOperandType::Register);

	ShaderWriteOperand(Bytecode, Dst, false);
	ShaderWriteOperand(Bytecode, Src1, false);
	ShaderWriteOperand(Bytecode, Src2, false);

	// Set op code length
	SetBitsFromWord<24, 30>(&Bytecode->Opcodes[OpcodeFirstWordIndex], Bytecode->Opcodes.size() - OpcodeFirstWordIndex);
}

void ShaderAdd(D3DOpcodeState* Bytecode, BytecodeOperand Src1, BytecodeOperand Src2, BytecodeOperand Dst)
{
	ShaderPerformBinaryOp(Bytecode, D3DOpcodeType_ADD, Src1, Src2, Dst);
}

void ShaderFmad(D3DOpcodeState* Bytecode, BytecodeOperand AddSrc, BytecodeOperand MulSrc1, BytecodeOperand MulSrc2, BytecodeOperand Dst)
{

}

void ShaderMul(D3DOpcodeState* Bytecode, BytecodeOperand Src1, BytecodeOperand Src2, BytecodeOperand Dst)
{
	ShaderPerformBinaryOp(Bytecode, D3DOpcodeType_MUL, Src1, Src2, Dst);
}

void ShaderSampleTextureLevel(D3DOpcodeState* Bytecode, BytecodeOperand Tex, BytecodeOperand Sampler, BytecodeOperand UVs, BytecodeOperand Dst, BytecodeOperand MipsLevel)
{
	ASSERT(Dst.Type == BytecodeOperandType::Register);
	ASSERT(Tex.Type == BytecodeOperandType::Register);
	ASSERT(Tex.Register.RegType == BytecodeRegisterType::Texture);
	ASSERT(Sampler.Type == BytecodeOperandType::Register);
	ASSERT(Sampler.Register.RegType == BytecodeRegisterType::Sampler);
	ASSERT(MipsLevel.Type == BytecodeOperandType::ImmediateFloat1);

	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_SAMPLE_L);
	SetBitsFromWord<31, 31>(&OpcodeDWORD, 1);
	
	int32 OpcodeFirstWordIndex = Bytecode->Opcodes.size();
	
	//SetBitsFromWord<24, 30>(&OpcodeDWORD, 5);
	Bytecode->Opcodes.push_back(OpcodeDWORD);

	{
		uint32 OpcodeHeaderExtension = 0;
		SetBitsFromWord<0, 5>(&OpcodeHeaderExtension, OpcodeExtensionType_ResourceDim);
		SetBitsFromWord<6, 10>(&OpcodeHeaderExtension, ResourceDimension_Texture2D);
		uint32 ResourceStride = 0;
		SetBitsFromWord<11, 15>(&OpcodeHeaderExtension, ResourceStride);
		SetBitsFromWord<31, 31>(&OpcodeHeaderExtension, 1);

		Bytecode->Opcodes.push_back(OpcodeHeaderExtension);
	}

	{
		uint32 OpcodeHeaderExtension = 0;
		SetBitsFromWord<0, 5>(&OpcodeHeaderExtension, OpcodeExtensionType_ResourceReturnType);

		for (int32 i = 0; i < 4; i++)
		{
			SetBitsFromWord(&OpcodeHeaderExtension, 6 + 4 * i, 9 + 4 * i, ResourceReturnType_Float);
		}

		SetBitsFromWord<31, 31>(&OpcodeHeaderExtension, 0);

		Bytecode->Opcodes.push_back(OpcodeHeaderExtension);
	}

	ShaderWriteOperand(Bytecode, Dst, false);
	ShaderWriteOperand(Bytecode, UVs, false);
	ShaderWriteOperand(Bytecode, Tex, false);
	ShaderWriteOperand(Bytecode, Sampler, false);
	ShaderWriteOperand(Bytecode, MipsLevel, false);


	SetBitsFromWord<24, 30>(&Bytecode->Opcodes[OpcodeFirstWordIndex], Bytecode->Opcodes.size() - OpcodeFirstWordIndex);
}

void ShaderReturn(D3DOpcodeState* Bytecode)
{
	uint32 OpcodeDWORD = 0;
	SetBitsFromWord<0, 10>(&OpcodeDWORD, D3DOpcodeType_RET);
	SetBitsFromWord<24, 30>(&OpcodeDWORD, 1);
	Bytecode->Opcodes.push_back(OpcodeDWORD);
}


constexpr static uint32 Log2(uint32 Val) {
	uint32 Log = 0;
	while (Val > 1)
	{
		Val = Val >> 1;
		Log++;
	}
	return Log;
}

static_assert(Log2(1) == 0, "Log2 test");
static_assert(Log2(2) == 1, "Log2 test");
static_assert(Log2(4) == 2, "Log2 test");

void GenerateBytecodeOpcodes(FuzzDXBCState* DXBCState, D3DOpcodeState* Bytecode)
{
	Bytecode->Opcodes.clear();

	// Blind reserve
	Bytecode->Opcodes.reserve(1024);


	// Generate declarations
	ShaderDeclareGlobalFlags(Bytecode, true);

	for (int32 i = 0; i < Bytecode->CBVSizes.size(); i++)
	{
		ShaderDeclareConstantBuffer(Bytecode, i, Bytecode->CBVSizes[i]);
	}

	for (int32 i = 0; i < Bytecode->NumSamplers; i++)
	{
		ShaderDeclareSampler(Bytecode, i);
	}
	
	for (int32 i = 0; i < Bytecode->NumTextures; i++)
	{
		ShaderDeclareTexture2DResource(Bytecode, i);
	}

	if (Bytecode->ShaderType == D3DShaderType::Vertex)
	{
		for (int32 i = 0; i < Bytecode->InputSemantics.size(); i++)
		{
			ShaderDeclareInput(Bytecode, i);
		}

		for (int32 i = 0; i < Bytecode->OutputSemantics.size(); i++)
		{
			auto Semantic = Bytecode->OutputSemantics[i];
			if (Semantic == ShaderSemantic::SV_POSITION)
			{
				ShaderDeclareOutput_SIV(Bytecode, i, OperandSemantic_Position);
			}
			else
			{
				ShaderDeclareOutput(Bytecode, i);
			}
		}
	}
	else if (Bytecode->ShaderType == D3DShaderType::Pixel)
	{
		for (int32 i = 0; i < Bytecode->InputSemantics.size(); i++)
		{
			auto Semantic = Bytecode->InputSemantics[i];

			if (Semantic == ShaderSemantic::SV_POSITION)
			{
				ShaderDeclareInputPS_SIV(Bytecode, i, OperandSemantic_Position, PSInputInterpolationMode_LinearNoPerspective);
			}
			else
			{
				ShaderDeclareInputPS(Bytecode, i, PSInputInterpolationMode_Linear);
			}
		}

		for (int32 i = 0; i < Bytecode->OutputSemantics.size(); i++)
		{
			ShaderDeclareOutput(Bytecode, i);
		}
	}

	// I guess we could do something besides this, but like what
	ASSERT(Bytecode->NumTempRegisters > 0);

	if (Bytecode->NumTempRegisters > 0)
	{
		// Pixel shaders will use one extra register for storing the SV_POSITION input arg in  a normalized fashion
		if (Bytecode->ShaderType == D3DShaderType::Pixel)
		{
			ShaderDeclareNumTempRegisters(Bytecode, Bytecode->NumTempRegisters + 1);
		}
		else
		{
			ShaderDeclareNumTempRegisters(Bytecode, Bytecode->NumTempRegisters);
		}
	}

	// TODO: Randomise op codes based on possible inputs and outputs, basically cycle through temp registers writing to them until the end where we output it
	uint32 RegisterValidMask = 0;
	int32 NextTempRegisterToWrite = 0;

	auto GetRandomValue = [&]()
	{
		return DXBCState->GetFloat01();
	};

	auto GetRandomSwizzle = [&]()
	{
		auto Swizzle = BytecodeOperandSwizzling();
		if (DXBCState->GetFloat01() < 0.2f)
		{
			for (int32 i = 0; i < 4; i++)
			{
				Swizzle.Swizzling[i] = DXBCState->GetIntInRange(0, 3);
			}
		}
		return Swizzle;
	};

	// [0, Bytecode->NumTempRegisters) will be scratch registers for calculating stuff, 
	int32 TempRegisterToHoldNormalizedSVCoords = Bytecode->NumTempRegisters;

	auto PickDataOperand = [&](bool AllowLiterals) {

		auto PickInputRegister = [&]()
		{
			int32 InputIndex = DXBCState->GetIntInRange(0, Bytecode->InputSemantics.size() - 1);

			if (Bytecode->ShaderType == D3DShaderType::Pixel && InputIndex == 0)
			{
				return BytecodeOperand::OpRegister(BytecodeRegisterRef::Temp(TempRegisterToHoldNormalizedSVCoords));
			}
			else
			{
				return BytecodeOperand::OpRegister(BytecodeRegisterRef::Input(InputIndex));
			}
		};


		int32 Decider = DXBCState->GetIntInRange(0, 99);

		if (RegisterValidMask > 0 && Decider < 40)
		{
			int32 NumValidRegs = Log2(RegisterValidMask + 1);
			int32 TempRegIndex = DXBCState->GetIntInRange(0, NumValidRegs - 1);

			return BytecodeOperand::OpRegister(BytecodeRegisterRef::Temp(TempRegIndex), GetRandomSwizzle());
		}
		else if (Decider < 55)
		{
			return PickInputRegister();
		}
		else if (Bytecode->CBVSizes.size() > 0 && Decider < 65)
		{
			int32 CBVRegister = DXBCState->GetIntInRange(0, Bytecode->CBVSizes.size() - 1);
			int32 CBVOffset = DXBCState->GetIntInRange(0, Bytecode->CBVSizes[CBVRegister] - 1);

			return BytecodeOperand::OpCBV(BytecodeRegisterRef::ConstantBuffer(CBVRegister), CBVOffset, GetRandomSwizzle());
		}
		else if (AllowLiterals && Decider < 90)
		{
			return BytecodeOperand::OpFloat4(BytecodeImmediateValue(GetRandomValue() + 0.5f, GetRandomValue() + 0.5f, GetRandomValue() + 0.5f, GetRandomValue() + 0.5f));
		}
		else if (Decider < 100)
		{
			return PickInputRegister();
		}
		else
		{
			ASSERT(false);
		}

		return BytecodeOperand::OpRegister(BytecodeRegisterRef::Input(0));
	};

	if (Bytecode->ShaderType == D3DShaderType::Pixel)
	{
		ASSERT(Bytecode->InputSemantics[0] == ShaderSemantic::SV_POSITION);
		auto SV_INPUT = BytecodeOperand::OpRegister(BytecodeRegisterRef::Input(0));
		auto NormlizedRegister = BytecodeOperand::OpRegister(BytecodeRegisterRef::Temp(TempRegisterToHoldNormalizedSVCoords));

		// TODO: Assuming 512x512 render target size
		float XScale = 1.0f / DXBCState->GetFloatInRange(400, 700);
		float YScale = 1.0f / DXBCState->GetFloatInRange(400, 700);
		auto ScaleOp = BytecodeOperand::OpFloat4(BytecodeImmediateValue(XScale, YScale, 1.0f, 1.0f));

		ShaderMul(Bytecode, SV_INPUT, ScaleOp, NormlizedRegister);
	}

	for (int32 i = 0; i < Bytecode->DataOpcodesToEmit; i++)
	{
		// TODO: No no no
		int32 Decider = DXBCState->GetIntInRange(0, 99);
	
		auto Dst = BytecodeOperand::OpRegister(BytecodeRegisterRef::Temp(NextTempRegisterToWrite));
	
		if (Bytecode->NumTextures > 0 && Bytecode->NumSamplers > 0 && Decider < 30)
		{
			auto Tex = BytecodeOperand::OpRegister(BytecodeRegisterRef::Texture(DXBCState->GetIntInRange(0, Bytecode->NumTextures - 1)));
			auto Sampler = BytecodeOperand::OpRegister(BytecodeRegisterRef::Sampler(DXBCState->GetIntInRange(0, Bytecode->NumSamplers - 1)));
			auto UVs = PickDataOperand(false);
			// We always need this sort of swizzle for UVs to sampling...I guess
			UVs.Swizzling = BytecodeOperandSwizzling(BytecodeOperandSwizzle::X, BytecodeOperandSwizzle::Y, BytecodeOperandSwizzle::X, BytecodeOperandSwizzle::X);
			
			auto MipsLevel = BytecodeOperand::OpFloat1(GetRandomValue() * 4.0f);
			ShaderSampleTextureLevel(Bytecode, Tex, Sampler, UVs, Dst, MipsLevel);
		}
		else if (Decider < 60)
		{
			// Mul
			ShaderMul(Bytecode, PickDataOperand(false), PickDataOperand(true), Dst);
		}
		else if (Decider < 100)
		{
			// Add
			ShaderAdd(Bytecode, PickDataOperand(false), PickDataOperand(true), Dst);
		}
		else
		{
			ASSERT(false);
		}
	
		NextTempRegisterToWrite = (NextTempRegisterToWrite + 1) % Bytecode->NumTempRegisters;
		RegisterValidMask |= (1 << NextTempRegisterToWrite);
	}

	//if (Bytecode->ShaderType == D3DShaderType::Vertex)
	//{
	//	// TODO: Scan for it if order changes
	//	ASSERT(Bytecode->InputSemantics[0] == ShaderSemantic::POSITION);
	//	ASSERT(Bytecode->OutputSemantics[0] == ShaderSemantic::SV_POSITION);
	//	
	//	auto LastWrittenRegOp = BytecodeOperand::OpRegister(BytecodeRegisterRef::Temp(LastWrittenTempRegister));
	//	auto PosInputReg = BytecodeOperand::OpRegister(BytecodeRegisterRef::Input(0));
	//	//const float Scalar = 0.000001f;
	//	//auto Coefficient = BytecodeOperand::OpFloat4(BytecodeImmediateValue(Scalar, Scalar, Scalar, 0.0f));
	//	//ShaderMul(Bytecode, LastWrittenRegOp, Coefficient, LastWrittenRegOp);
	//	//ShaderAdd(Bytecode, LastWrittenRegOp, PosInputReg, LastWrittenRegOp);
	//
	//	auto Output = BytecodeOperand::OpRegister(BytecodeRegisterRef::Output(0));
	//
	//	ShaderDoMov(Bytecode, PosInputReg, Output);
	//}
	//else if (Bytecode->ShaderType == D3DShaderType::Pixel)
	//{
	//	// TODO: Scan for it if order changes
	//	ASSERT(Bytecode->InputSemantics[0] == ShaderSemantic::SV_POSITION);
	//	ASSERT(Bytecode->OutputSemantics[0] == ShaderSemantic::SV_TARGET);
	//
	//	auto LastWrittenRegOp = BytecodeOperand::OpRegister(BytecodeRegisterRef::Temp(LastWrittenTempRegister));
	//	// TODO: Mov with a mask?
	//	auto PosInputReg = BytecodeOperand::OpRegister(BytecodeRegisterRef::Input(0));
	//	auto Coefficient = BytecodeOperand::OpFloat4(BytecodeImmediateValue(1.0f, 1.0f, 1.0f, 0.0f));
	//	auto Addendum = BytecodeOperand::OpFloat4(BytecodeImmediateValue(0.0f, 0.0f, 0.0f, 1.0f));
	//	//ShaderMul(Bytecode, LastWrittenRegOp, Coefficient, LastWrittenRegOp);
	//	//ShaderAdd(Bytecode, LastWrittenRegOp, Addendum, LastWrittenRegOp);
	//
	//	//auto NewCoefficient = BytecodeOperand::OpFloat4(BytecodeImmediateValue(0.01f, 0.01f, 0.01f, 0.0f));
	//	//ShaderMul(Bytecode, PosInputReg, NewCoefficient, LastWrittenRegOp);
	//	//ShaderAdd(Bytecode, LastWrittenRegOp, Addendum, LastWrittenRegOp);
	//	
	//	auto ZeroCoeff = BytecodeOperand::OpFloat4(BytecodeImmediateValue(0.0f, 0.0f, 0.0f, 0.0f));
	//	auto SetColVal = BytecodeOperand::OpFloat4(BytecodeImmediateValue(0.6f, 0.9f, 0.8f, 1.0f));
	//
	//	{
	//		float Scalar = 1.0f / 512.0f;
	//		auto ScaleCoefficient = BytecodeOperand::OpFloat4(BytecodeImmediateValue(Scalar, Scalar, Scalar, 1.0f));
	//		auto Input = BytecodeOperand::OpRegister(BytecodeRegisterRef::Input(0));
	//		auto Result = BytecodeOperand::OpRegister(BytecodeRegisterRef::Temp(0));
	//
	//		ShaderMul(Bytecode, Input, ScaleCoefficient, Result);
	//	}
	//
	//	{
	//		auto Tex = BytecodeOperand::OpRegister(BytecodeRegisterRef::Texture(0));
	//		auto Sampler = BytecodeOperand::OpRegister(BytecodeRegisterRef::Sampler(0));
	//		auto UVs = BytecodeOperand::OpRegister(BytecodeRegisterRef::Temp(0)); //LastWrittenRegOp;
	//		// We always need this sort of swizzle for UVs to sampling...I guess
	//		UVs.Swizzling = BytecodeOperandSwizzling(BytecodeOperandSwizzle::X, BytecodeOperandSwizzle::Y, BytecodeOperandSwizzle::X, BytecodeOperandSwizzle::X);
	//
	//		auto Output = BytecodeOperand::OpRegister(BytecodeRegisterRef::Output(0));
	//
	//		auto MipsLevel = BytecodeOperand::OpFloat1(0.0f);
	//		ShaderSampleTextureLevel(Bytecode, Tex, Sampler, UVs, Output, MipsLevel);
	//	}
	//
	//	//ShaderDoMov(Bytecode, SetColVal, LastWrittenRegOp);
	//	//ShaderMul(Bytecode, LastWrittenRegOp, ZeroCoeff, LastWrittenRegOp);
	//	//ShaderAdd(Bytecode, LastWrittenRegOp, SetColVal, LastWrittenRegOp);
	//}

	int32 LastWrittenTempRegister = (NextTempRegisterToWrite + Bytecode->NumTempRegisters - 1) % Bytecode->NumTempRegisters;

	// Do a MOV from last written temp register to output, and from random temp registers for the others
	ShaderDoMov(Bytecode, BytecodeOperand::OpRegister(BytecodeRegisterRef::Temp(LastWrittenTempRegister)), BytecodeOperand::OpRegister(BytecodeRegisterRef::Output(0)));
	for (int32 i = 1; i < Bytecode->OutputSemantics.size(); i++)
	{
		ShaderDoMov(Bytecode, BytecodeOperand::OpRegister(BytecodeRegisterRef::Temp(DXBCState->GetIntInRange(0, Bytecode->NumTempRegisters - 1))), BytecodeOperand::OpRegister(BytecodeRegisterRef::Output(i)));
	}

	ShaderReturn(Bytecode);
}

void RandomiseShaderBytecodeParams(FuzzDXBCState* DXBCState, D3DOpcodeState* VSOpcodes, D3DOpcodeState* PSOpcodes)
{
	VSOpcodes->InputSemantics.push_back(ShaderSemantic::POSITION);
	VSOpcodes->OutputSemantics.push_back(ShaderSemantic::SV_POSITION);

	PSOpcodes->InputSemantics.push_back(ShaderSemantic::SV_POSITION);
	PSOpcodes->OutputSemantics.push_back(ShaderSemantic::SV_TARGET);

	if (DXBCState->GetFloat01() < 0.5f)
	{
		VSOpcodes->OutputSemantics.push_back(ShaderSemantic::TEXCOORD);
		PSOpcodes->InputSemantics.push_back(ShaderSemantic::TEXCOORD);
	}


	if (DXBCState->GetFloat01() < 0.3f)
	{
		VSOpcodes->OutputSemantics.push_back(ShaderSemantic::COLOR);
		PSOpcodes->InputSemantics.push_back(ShaderSemantic::COLOR);
	}

	if (DXBCState->GetFloat01() < 0.3f)
	{
		VSOpcodes->InputSemantics.push_back(ShaderSemantic::TEXCOORD);
	}

	if (DXBCState->GetFloat01() < 0.3f)
	{
		VSOpcodes->InputSemantics.push_back(ShaderSemantic::COLOR);
	}

	// TODO: Shuffle input and output semantics (as long as the VS out and PS in order match)

	VSOpcodes->NumTextures = DXBCState->GetIntInRange(0, 3);
	VSOpcodes->NumSamplers = DXBCState->GetIntInRange(0, 3);
	VSOpcodes->CBVSizes.push_back(1);
	VSOpcodes->CBVSizes.resize(DXBCState->GetIntInRange(0, 3));
	for (int32 i = 0; i < VSOpcodes->CBVSizes.size(); i++)
	{
		VSOpcodes->CBVSizes[i] = DXBCState->GetIntInRange(1, 5);
	}

	PSOpcodes->NumTextures = DXBCState->GetIntInRange(0, 3);
	PSOpcodes->NumSamplers = DXBCState->GetIntInRange(0, 3);

	PSOpcodes->CBVSizes.resize(DXBCState->GetIntInRange(0, 3));
	for (int32 i = 0; i < PSOpcodes->CBVSizes.size(); i++)
	{
		PSOpcodes->CBVSizes[i] = DXBCState->GetIntInRange(1, 5);
	}
}

// Little-endian, as god intended
void WriteU32ToUCharVectorLE(std::vector<byte>* OutData, uint32 Val, int32 Index = -1)
{
	if (Index == -1)
	{
		for (int32 i = 0; i < 4; i++)
		{
			OutData->push_back((byte)((Val >> (i * 8)) & 0xFF));
		}
	}
	else
	{
		ASSERT(Index >= 0 && Index <= OutData->size() - 4);
		for (int32 i = 0; i < 4; i++)
		{
			(*OutData)[Index + i] = ((byte)((Val >> (i * 8)) & 0xFF));
		}
	}
}

void WriteU16ToUCharVectorLE(std::vector<byte>* OutData, uint16 Val, int32 Index = -1)
{
	if (Index == -1)
	{
		for (int32 i = 0; i < 2; i++)
		{
			OutData->push_back((byte)((Val >> (i * 8)) & 0xFF));
		}
	}
	else
	{
		ASSERT(Index >= 0 && Index < OutData->size() - 2);
		for (int32 i = 0; i < 2; i++)
		{
			(*OutData)[Index + i] = ((byte)((Val >> (i * 8)) & 0xFF));
		}
	}
}

void WriteStringtoUCharVector(std::vector<byte>* OutData, const char* Str)
{
	// Include the null byte in this
	int32 Len = strlen(Str) + 1;
	int32 CurrentDataSize = OutData->size();
	OutData->resize(CurrentDataSize + Len);
	memcpy(OutData->data() + CurrentDataSize, Str, Len);
}

void GenerateRDEFChunk(D3DOpcodeState* Opcodes, std::vector<byte>* OutData)
{
	const uint32 ChunkMagic = 0x46454452;
	WriteU32ToUCharVectorLE(OutData, ChunkMagic);

	int32 ChunkSizeIndex = OutData->size();
	// Placeholder for size, we will fix up later
	WriteU32ToUCharVectorLE(OutData, 0);

	int32 ChunkDataStartIndex = OutData->size();

	WriteU32ToUCharVectorLE(OutData, Opcodes->CBVSizes.size());

	int32 CBVOffsetIndex = OutData->size();
	// Placeholder for CBV offset, we will fix up later
	WriteU32ToUCharVectorLE(OutData, 0);

	int32 NumResources = Opcodes->CBVSizes.size() + Opcodes->NumSamplers + Opcodes->NumTextures;
	//int32 NumResources = Opcodes->CBVSizes.size() + Opcodes->NumSamplers;
	//int32 NumResources = Opcodes->NumSamplers;
	//int32 NumResources = Opcodes->NumSamplers + Opcodes->NumTextures;
	//int32 NumResources = Opcodes->CBVSizes.size() + Opcodes->NumTextures;
	//int32 NumResources = Opcodes->CBVSizes.size();
	WriteU32ToUCharVectorLE(OutData, NumResources);

	int32 RDefOffsetIndex = OutData->size();
	// Placeholder for resource definitions offset, we will fix up later
	WriteU32ToUCharVectorLE(OutData, 0);

	// Minor version number, major version number
	OutData->push_back(0);
	OutData->push_back(5);

	// Shader type
	if (Opcodes->ShaderType == D3DShaderType::Vertex)
	{
		OutData->push_back(0xFE);
		OutData->push_back(0xFF);
	}
	else if (Opcodes->ShaderType == D3DShaderType::Pixel)
	{
		OutData->push_back(0xFF);
		OutData->push_back(0xFF);
	}
	else
	{
		ASSERT(false);
	}

	// NoPreShader...not sure if we should put other stuff in here
	uint32 MiscFlags = 0x100;
	WriteU32ToUCharVectorLE(OutData, MiscFlags);

	int32 CreatorNameOffsetIndex = OutData->size();
	// Placeholder for offset to creator string
	WriteU32ToUCharVectorLE(OutData, 0);

	const uint32 RD114CC = 0x31314452;
	WriteU32ToUCharVectorLE(OutData, RD114CC);

	// Unknown, going off of what's produced by D3DCompile
	WriteU32ToUCharVectorLE(OutData, 0x3C);
	WriteU32ToUCharVectorLE(OutData, 0x18);
	WriteU32ToUCharVectorLE(OutData, 0x20);
	WriteU32ToUCharVectorLE(OutData, 0x28);
	WriteU32ToUCharVectorLE(OutData, 0x24);
	WriteU32ToUCharVectorLE(OutData, 0x0C);

	uint32 InterfaceSlotCount = 0;
	WriteU32ToUCharVectorLE(OutData, InterfaceSlotCount);

	std::vector<int32> CBVNameOffsetIndices;
	CBVNameOffsetIndices.reserve(Opcodes->CBVSizes.size());

	std::vector<int32> CBVVariableDescOffsetIndices;
	CBVVariableDescOffsetIndices.reserve(Opcodes->CBVSizes.size());

	if (Opcodes->CBVSizes.size() > 0)
	{
		WriteU32ToUCharVectorLE(OutData, OutData->size() - ChunkDataStartIndex, CBVOffsetIndex);
	}
	else
	{
		WriteU32ToUCharVectorLE(OutData, 0, CBVOffsetIndex);
	}

	for (int32 CBVSize : Opcodes->CBVSizes)
	{
		CBVNameOffsetIndices.push_back(OutData->size());
		// Placeholder for CBV name offset, we will fix up later
		WriteU32ToUCharVectorLE(OutData, 0);

		// Number of variables
		WriteU32ToUCharVectorLE(OutData, CBVSize);

		CBVVariableDescOffsetIndices.push_back(OutData->size());
		// Placeholder for CBV variable descriptions offset, we will fix up later
		WriteU32ToUCharVectorLE(OutData, 0);

		// CBV size in bytes, not numbers of float4 registers
		WriteU32ToUCharVectorLE(OutData, CBVSize * 16);

		// Flags, which are none
		WriteU32ToUCharVectorLE(OutData, 0);

		// D3D11_CT_CBUFFER type
		WriteU32ToUCharVectorLE(OutData, 0);
	}

	std::vector<int32> ResourceBindingNameOffsetIndices;
	ResourceBindingNameOffsetIndices.reserve(NumResources);

	auto AddResource = [&](uint32 ResourceType, int32 BindIndex) {
		ResourceBindingNameOffsetIndices.push_back(OutData->size());
		// Placeholder for resource name offset, we will fix up later
		WriteU32ToUCharVectorLE(OutData, 0);

		// Resource type (0 = cbv, 2 = texture, 3 = sampler)
		WriteU32ToUCharVectorLE(OutData, ResourceType);

		// Resource return type
		WriteU32ToUCharVectorLE(OutData, (ResourceType == 2) ? 5 : 0);

		// Resource view dimension
		// TODO: Other texture dimensions
		// 0 - N/A
		// 1 - buff
		// 2 - 1d
		// 3 - 1darray
		// 4 - 2d
		// 5 - 2darray
		// 6 - 2d MS
		// 7 - 2darray MS
		// 8 - 3d
		WriteU32ToUCharVectorLE(OutData, (ResourceType == 2) ? 4 : 0);

		// Number of samples
		WriteU32ToUCharVectorLE(OutData, (ResourceType == 2) ? -1 : 0);

		// Bind point
		WriteU32ToUCharVectorLE(OutData, BindIndex);

		// Bind count
		WriteU32ToUCharVectorLE(OutData, 1);

		// Shader flags
		WriteU32ToUCharVectorLE(OutData, (ResourceType == 2) ? 0x0C : 0);
	};

	WriteU32ToUCharVectorLE(OutData, OutData->size() - ChunkDataStartIndex, RDefOffsetIndex);

	for (int32 CBVIndex = 0; CBVIndex < Opcodes->CBVSizes.size(); CBVIndex++)
	{
		AddResource(0, CBVIndex);
	}

	for (int32 SamplerIndex = 0; SamplerIndex < Opcodes->NumSamplers; SamplerIndex++)
	{
		AddResource(3, SamplerIndex);
	}

	for (int32 TextureIndex = 0; TextureIndex < Opcodes->NumTextures; TextureIndex++)
	{
		AddResource(2, TextureIndex);
	}

	for (int32 CBVIndex = 0; CBVIndex < CBVNameOffsetIndices.size(); CBVIndex++)
	{
		int32 CBVNameOffsetIndex = CBVNameOffsetIndices[CBVIndex];
		WriteU32ToUCharVectorLE(OutData, OutData->size() - ChunkDataStartIndex, CBVNameOffsetIndex);

		WriteStringtoUCharVector(OutData, StringStackBuffer<256>("res_%d", CBVIndex).buffer);
	}

	std::vector<int32> CBVVarNameOffsetIndices;
	std::vector<int32> CBVVarTypeOffsetIndices;
	for (int32 CBVIndex = 0; CBVIndex < CBVVariableDescOffsetIndices.size(); CBVIndex++)
	{
		int32 CBVVariableDescOffsetIndex = CBVVariableDescOffsetIndices[CBVIndex];
		WriteU32ToUCharVectorLE(OutData, OutData->size() - ChunkDataStartIndex, CBVVariableDescOffsetIndex);

		int32 VarCount = Opcodes->CBVSizes[CBVIndex];
		for (int32 VarIdx = 0; VarIdx < VarCount; VarIdx++)
		{
			CBVVarNameOffsetIndices.push_back(OutData->size());
			// Placeholder for CBV name offset, we will fix up later
			WriteU32ToUCharVectorLE(OutData, 0);

			// Offset of variable w/in CBV in bytes
			WriteU32ToUCharVectorLE(OutData, 16 * VarIdx);
			// Size of variable in bytes
			WriteU32ToUCharVectorLE(OutData, 16);
			
			// Flags (0x02 means it's used in the shader...which might not be true?)
			WriteU32ToUCharVectorLE(OutData, 0x02);

			CBVVarTypeOffsetIndices.push_back(OutData->size());
			// Placeholder for CBV type offset, we will fix up later
			WriteU32ToUCharVectorLE(OutData, 0);

			// Offset to default value, we can just put this as 0 and it won't try to read it
			WriteU32ToUCharVectorLE(OutData, 0);
			
			// Unknown, something to do with textures/samplers maybe
			WriteU32ToUCharVectorLE(OutData, -1);
			WriteU32ToUCharVectorLE(OutData, 0);
			WriteU32ToUCharVectorLE(OutData, -1);
			WriteU32ToUCharVectorLE(OutData, 0);
		}
	}

	ASSERT(CBVVarNameOffsetIndices.size() == CBVVarTypeOffsetIndices.size());

	for (int32 CBVVarIndex = 0; CBVVarIndex < CBVVarNameOffsetIndices.size(); CBVVarIndex++)
	{
		int32 CBVVarNameOffsetIndex = CBVVarNameOffsetIndices[CBVVarIndex];
		WriteU32ToUCharVectorLE(OutData, OutData->size() - ChunkDataStartIndex, CBVVarNameOffsetIndex);

		WriteStringtoUCharVector(OutData, StringStackBuffer<256>("cb_var_%d", CBVVarIndex).buffer);
		// Uhhh...padding?
		while (OutData->size() % 4 != 0)
		{
			OutData->push_back(0xAB);
		}
	}

	std::vector<int32> CBVVarTypeNameOffsetIndices;
	for (int32 CBVVarIndex = 0; CBVVarIndex < CBVVarTypeOffsetIndices.size(); CBVVarIndex++)
	{
		int32 CBVVarTypeOffsetIndex = CBVVarTypeOffsetIndices[CBVVarIndex];
		WriteU32ToUCharVectorLE(OutData, OutData->size() - ChunkDataStartIndex, CBVVarTypeOffsetIndex);

		// Variable class (D3D_SVC_VECTOR)
		WriteU16ToUCharVectorLE(OutData, 0x01);

		// Variable type (D3D_SVT_FLOAT)
		WriteU16ToUCharVectorLE(OutData, 0x03);

		// Number of rows
		WriteU16ToUCharVectorLE(OutData, 1);
		// Number of columns
		WriteU16ToUCharVectorLE(OutData, 4);

		// Array size (irrelevant for vectors)
		WriteU16ToUCharVectorLE(OutData, 0);

		// Number of members ina  struct (irrelevant for vectors)
		WriteU16ToUCharVectorLE(OutData, 0);

		// Offset from chunk data start to first member (irrelevant for vectors)
		WriteU16ToUCharVectorLE(OutData, 0);
		
		// Unkown, maybe some flags
		WriteU16ToUCharVectorLE(OutData, 0);
		
		// Maybe some texture/sampler data? Idk, seems to only be in SM5.0 or higher
		WriteU32ToUCharVectorLE(OutData, 0);
		WriteU32ToUCharVectorLE(OutData, 0);
		WriteU32ToUCharVectorLE(OutData, 0);
		WriteU32ToUCharVectorLE(OutData, 0);

		CBVVarTypeNameOffsetIndices.push_back(OutData->size());
		// Placeholder for CBV type name offset, we will fix up later
		WriteU32ToUCharVectorLE(OutData, 0);
	}

	if (CBVVarTypeNameOffsetIndices.size() > 0)
	{
		for (int32 CBVVarIndex = 0; CBVVarIndex < CBVVarTypeNameOffsetIndices.size(); CBVVarIndex++)
		{
			int32 CBVVarTypeNameOffsetIndex = CBVVarTypeNameOffsetIndices[CBVVarIndex];
			WriteU32ToUCharVectorLE(OutData, OutData->size() - ChunkDataStartIndex, CBVVarTypeNameOffsetIndex);
		}

		WriteStringtoUCharVector(OutData, "float4");
		// Uhhh...padding?
		while (OutData->size() % 4 != 0)
		{
			OutData->push_back(0xAB);
		}
	}


	for (int32 ResIndex = 0; ResIndex < ResourceBindingNameOffsetIndices.size(); ResIndex++)
	{
		int32 ResourceBindingNameOffsetIndex = ResourceBindingNameOffsetIndices[ResIndex];
		WriteU32ToUCharVectorLE(OutData, OutData->size() - ChunkDataStartIndex, ResourceBindingNameOffsetIndex);

		WriteStringtoUCharVector(OutData, StringStackBuffer<256>("res_%d", ResIndex).buffer);
		// Uhhh...padding?
		while (OutData->size() % 4 != 0)
		{
			OutData->push_back(0xAB);
		}
	}

	{
		WriteU32ToUCharVectorLE(OutData, OutData->size() - ChunkDataStartIndex, CreatorNameOffsetIndex);

		// I'm just gonna guess that maybe some driver detects non-MS compilers and does some different path,
		// so for now let's just play it safe and pretend we're coming out of the official compiler
		WriteStringtoUCharVector(OutData, "Microsoft (R) HLSL Shader Compiler 10.1");
	}

	{
		while (OutData->size() % 4 != 0)
		{
			OutData->push_back(0xAB);
		}
	}

	// Now that we know the chunk size, go back and fix it up
	WriteU32ToUCharVectorLE(OutData, OutData->size() - ChunkDataStartIndex, ChunkSizeIndex);
}

void GenerateIOSignatureChunk(D3DOpcodeState* Opcodes, std::vector<byte>* OutData, bool IsInput)
{
	const uint32 ChunkMagic = IsInput ? 0x4E475349 : 0x4E47534F;
	WriteU32ToUCharVectorLE(OutData, ChunkMagic);

	int32 ChunkSizeIndex = OutData->size();
	// Placeholder for size, we will fix up later
	WriteU32ToUCharVectorLE(OutData, 0);

	int32 ChunkDataStartIndex = OutData->size();

	const auto& Semantics = IsInput ? Opcodes->InputSemantics : Opcodes->OutputSemantics;

	WriteU32ToUCharVectorLE(OutData, Semantics.size());

	// ???
	uint32 MaybeFlags = 0x08;
	WriteU32ToUCharVectorLE(OutData, MaybeFlags);

	std::vector<int32> ElementNameOffsetIndices;
	ElementNameOffsetIndices.reserve(Semantics.size());

	for (int32 SignatureIndex = 0; SignatureIndex < Semantics.size(); SignatureIndex++)
	{
		ElementNameOffsetIndices.push_back(OutData->size());
		// Placeholder for element name offset, we will fix up later
		WriteU32ToUCharVectorLE(OutData, 0);

		// Semantic index (always 0, we never re-use semantics)
		WriteU32ToUCharVectorLE(OutData, 0);

		// System semantic
		WriteU32ToUCharVectorLE(OutData, ShaderSemanticToOperandSemantic(Semantics[SignatureIndex]));

		// Type (3 = floating point)
		WriteU32ToUCharVectorLE(OutData, 0x03);

		// Register
		WriteU32ToUCharVectorLE(OutData, SignatureIndex);

		// Masks (first is declaration, second is read/write)
		OutData->push_back(0x0F);
		// TODO: ????????
		if (IsInput)
		{
			OutData->push_back(0x0F);
		}
		else
		{
			OutData->push_back(0x00);
		}

		// ??? Is this padding, or some new stuff in SM 5.0 ?
		WriteU16ToUCharVectorLE(OutData, 0);
	}

	for (int32 SignatureIndex = 0; SignatureIndex < Semantics.size(); SignatureIndex++)
	{
		int32 ElementNameOffsetIndex = ElementNameOffsetIndices[SignatureIndex];
		WriteU32ToUCharVectorLE(OutData, OutData->size() - ChunkDataStartIndex, ElementNameOffsetIndex);

		WriteStringtoUCharVector(OutData, GetSemanticNameFromSemantic(Semantics[SignatureIndex]));

		// Uhhh...padding?
		while (OutData->size() % 4 != 0)
		{
			OutData->push_back(0xAB);
		}
	}

	// Now that we know the chunk size, go back and fix it up
	WriteU32ToUCharVectorLE(OutData, OutData->size() - ChunkDataStartIndex, ChunkSizeIndex);
}

void GenerateSHEXChunk(D3DOpcodeState* Opcodes, std::vector<byte>* OutData)
{
	int32 NumberOfBytes = Opcodes->Opcodes.size() * 4;

	const uint32 ChunkMagic = 0x58454853;
	WriteU32ToUCharVectorLE(OutData, ChunkMagic);

	// 8 bytes of the header
	int32 ChunkSize = NumberOfBytes + 8;
	WriteU32ToUCharVectorLE(OutData, ChunkSize);

	// Version: major version is high nibble (5), minor version is low nibble (0)
	OutData->push_back(0x50);
	// Uhhh....idk
	OutData->push_back(0x00);

	if (Opcodes->ShaderType == D3DShaderType::Vertex)
	{
		WriteU16ToUCharVectorLE(OutData, 0x01);
	}
	else if (Opcodes->ShaderType == D3DShaderType::Pixel)
	{
		WriteU16ToUCharVectorLE(OutData, 0x00);
	}
	else
	{
		ASSERT(false);
	}

	WriteU32ToUCharVectorLE(OutData, Opcodes->Opcodes.size() + 2);

	int32 OpcodeStartIndex = OutData->size();
	OutData->resize(OutData->size() + NumberOfBytes);
	memcpy(OutData->data() + OpcodeStartIndex, Opcodes->Opcodes.data(), NumberOfBytes);
}

void GenerateSTATChunk(D3DOpcodeState* Opcodes, std::vector<byte>* OutData)
{
	// TODO: Does this really matter?

	// 53 54 41 54
	const uint32 ChunkMagic = 0x54415453;
	WriteU32ToUCharVectorLE(OutData, ChunkMagic);

	// 8 bytes of the header
	int32 ChunkSize = 148;
	WriteU32ToUCharVectorLE(OutData, ChunkSize);

	int32 StartOfChunkIndex = OutData->size();

	OutData->resize(OutData->size() + ChunkSize);

	// TODO: I...don't think this is used anywhere really? I'm still suspicious it'll be used
	//WriteU32ToUCharVectorLE(OutData, 2, StartOfChunkIndex + 0x00);
	//WriteU32ToUCharVectorLE(OutData, 2, StartOfChunkIndex + 0x0C);
	//WriteU32ToUCharVectorLE(OutData, 1, StartOfChunkIndex + 0x1C);
	//WriteU32ToUCharVectorLE(OutData, 1, StartOfChunkIndex + 0x3C);
}

void GenerateShaderBytecode(D3DOpcodeState* Opcodes, std::vector<byte>* OutBytecode)
{
	DXBCFileHeader DXBCHeader = {};
	DXBCHeader.MagicNumbers[0] = 'D';
	DXBCHeader.MagicNumbers[1] = 'X';
	DXBCHeader.MagicNumbers[2] = 'B';
	DXBCHeader.MagicNumbers[3] = 'C';
	DXBCHeader.ChunkCount = 5;

	// Write the header, and write each chunk
	OutBytecode->resize(sizeof(DXBCFileHeader));
	memcpy(OutBytecode->data(), &DXBCHeader, sizeof(DXBCFileHeader));

	int32 ChunkOffsetsStartIndex = OutBytecode->size();

	for (int32 i = 0; i < DXBCHeader.ChunkCount; i++)
	{
		// Write in a placeholder 0, we will come back later
		WriteU32ToUCharVectorLE(OutBytecode, 0);
	}

	// Write back the first chunk offset, since we're about to start the first chunk
	WriteU32ToUCharVectorLE(OutBytecode, OutBytecode->size(), ChunkOffsetsStartIndex);

	GenerateRDEFChunk(Opcodes, OutBytecode);

	WriteU32ToUCharVectorLE(OutBytecode, OutBytecode->size(), ChunkOffsetsStartIndex + 4);
	GenerateIOSignatureChunk(Opcodes, OutBytecode, true);

	WriteU32ToUCharVectorLE(OutBytecode, OutBytecode->size(), ChunkOffsetsStartIndex + 8);
	GenerateIOSignatureChunk(Opcodes, OutBytecode, false);

	WriteU32ToUCharVectorLE(OutBytecode, OutBytecode->size(), ChunkOffsetsStartIndex + 12);
	GenerateSHEXChunk(Opcodes, OutBytecode);

	WriteU32ToUCharVectorLE(OutBytecode, OutBytecode->size(), ChunkOffsetsStartIndex + 16);
	GenerateSTATChunk(Opcodes, OutBytecode);

	// Fix up the header now that we know the length/checksum, and also fix up offsets to each chunk
	DXBCHeader.FileSizeInBytes = OutBytecode->size();
	// NOTE: There's probably a better way of doing this, but the first memcpy replaces the length field,
	// which is needed since the length is part of the hash, honestly I'd rather not debug weird offset thingys anymore
	memcpy(OutBytecode->data(), &DXBCHeader, sizeof(DXBCFileHeader));

	dxbcHash(&(*OutBytecode)[20], OutBytecode->size() - 20, DXBCHeader.CheckSum);
	memcpy(OutBytecode->data(), &DXBCHeader, sizeof(DXBCFileHeader));
}


void GenerateShaderDXBC(FuzzDXBCState* DXBCState)
{
	D3DOpcodeState VertShader;
	VertShader.ShaderType = D3DShaderType::Vertex;

	D3DOpcodeState PixelShader;
	PixelShader.ShaderType = D3DShaderType::Pixel;

	RandomiseShaderBytecodeParams(DXBCState, &VertShader, &PixelShader);

	VertShader.NumTempRegisters = DXBCState->GetIntInRange(2,4);
	VertShader.DataOpcodesToEmit = DXBCState->GetIntInRange(5, 9);

	PixelShader.NumTempRegisters = DXBCState->GetIntInRange(4, 9);
	PixelShader.DataOpcodesToEmit = DXBCState->GetIntInRange(10, 25);

	GenerateBytecodeOpcodes(DXBCState, &VertShader);
	GenerateBytecodeOpcodes(DXBCState, &PixelShader);

	std::vector<byte> VSBytecode;
	std::vector<byte> PSBytecode;

	GenerateShaderBytecode(&VertShader, &VSBytecode);
	GenerateShaderBytecode(&PixelShader, &PSBytecode);

	HRESULT hr;

	//WriteDataToFile(StringStackBuffer<256>("manual_bytecode/gen_vs_seed_%llu.bin", DXBCState->InitialFuzzSeed).buffer, VSBytecode.data(), VSBytecode.size());
	//WriteDataToFile(StringStackBuffer<256>("manual_bytecode/gen_ps_seed_%llu.bin", DXBCState->InitialFuzzSeed).buffer, PSBytecode.data(), PSBytecode.size());
	
	//ParseDXBCCode(VSBytecode.data(), VSBytecode.size());
	//ParseDXBCCode(PSBytecode.data(), PSBytecode.size());
	
	//{
	//	ID3DBlob* VSDisasmBlob = nullptr;
	//	hr = D3DDisassemble(VSBytecode.data(), VSBytecode.size(), 0, nullptr, &VSDisasmBlob);
	//	ASSERT(SUCCEEDED(hr));
	//
	//	WriteDataToFile(StringStackBuffer<256>("manual_bytecode/gen_vs_seed_%llu_disasm.txt", DXBCState->InitialFuzzSeed).buffer, VSDisasmBlob->GetBufferPointer(), VSDisasmBlob->GetBufferSize());
	//
	//	ID3DBlob* PSDisasmBlob = nullptr;
	//	hr = D3DDisassemble(PSBytecode.data(), PSBytecode.size(), 0, nullptr, &PSDisasmBlob);
	//	ASSERT(SUCCEEDED(hr));
	//
	//	WriteDataToFile(StringStackBuffer<256>("manual_bytecode/gen_ps_seed_%llu_disasm.txt", DXBCState->InitialFuzzSeed).buffer, PSDisasmBlob->GetBufferPointer(), PSDisasmBlob->GetBufferSize());
	//}

	hr = D3DCreateBlob(VSBytecode.size(), &DXBCState->VSBlob);
	ASSERT(SUCCEEDED(hr));
	memcpy(DXBCState->VSBlob->GetBufferPointer(), VSBytecode.data(), VSBytecode.size());

	hr = D3DCreateBlob(PSBytecode.size(), &DXBCState->PSBlob);
	ASSERT(SUCCEEDED(hr));
	memcpy(DXBCState->PSBlob->GetBufferPointer(), PSBytecode.data(), PSBytecode.size());
}


