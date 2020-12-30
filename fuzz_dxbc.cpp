
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

#pragma pack(pop)

static_assert(sizeof(DXBCFileHeader) == 32, "Check packing on DXBCFileHeader");

template<typename T>
inline T GetValueFromCursor(byte** Cursor)
{
	T Val = *(T*)*Cursor;
	(*Cursor) += sizeof(T);
	return Val;
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

			int xc = 0;
			xc++;
			(void)xc;
		}
	}
}


