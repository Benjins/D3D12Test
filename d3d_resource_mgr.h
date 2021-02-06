#pragma once

#include "basics.h"

#include <d3d12.h>

#include <vector>

inline bool CompareD3D12ResourceDesc(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b)
{
	if (a.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && a.Dimension == b.Dimension)
	{
		return (a.Alignment == b.Alignment) && (a.Flags == b.Flags) && (a.Width == b.Width) && (a.Format == b.Format);
	}
	else if (a.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && a.Dimension == b.Dimension)
	{
		return (a.Alignment == b.Alignment) && (a.Flags == b.Flags) && (a.Width == b.Width) && (a.Height == b.Height) && (a.Layout == b.Layout) && (a.Format == b.Format);
	}
	else
	{
		return false;
	}
}


template<typename T>
inline void VectorSwapErase(std::vector<T>* Vec, int32 Index)
{
	ASSERT(Index >= 0 && Index < Vec->size());
	(*Vec)[Index] = (*Vec).back();
	(*Vec).pop_back();
}

template<typename T>
struct D3DRefWrapper
{
	T* ptr = nullptr;

	D3DRefWrapper() { }

	~D3DRefWrapper()
	{
		if (ptr)
		{
			ptr->Release();
		}
	}

	D3DRefWrapper(T* _ptr)
	{
		ptr = _ptr;
		ptr->AddRef();
	}

	D3DRefWrapper(const D3DRefWrapper& orig)
	{
		ptr = orig.ptr;
		if (ptr)
		{
			ptr->AddRef();
		}
	}

	void operator=(T* origRaw)
	{
		if (ptr != origRaw)
		{
			if (ptr)
			{
				ptr->Release();
			}
			ptr = origRaw;
			if (ptr)
			{
				ptr->AddRef();
			}
		}
	}

	void operator=(const D3DRefWrapper& orig)
	{
		if (ptr != orig.ptr)
		{
			if (ptr)
			{
				ptr->Release();
			}
			ptr = orig.ptr;
			if (ptr)
			{
				ptr->AddRef();
			}
		}
	}

	D3DRefWrapper(D3DRefWrapper&& orig) = delete;
	D3DRefWrapper& operator=(D3DRefWrapper&& orig) = delete;
};

struct ResourceLifecycleManager
{
	enum struct ResourceType
	{
		Committed,
		Placed,
		Reserved
	};

	struct ResourceDesc
	{
		D3D12_RESOURCE_DESC ResDesc;
		bool IsUploadHeap = false;
		ResourceType Type = ResourceType::Committed;
		uint32 CreationNodeIndex = 0;
		uint32 NodeVisibilityMask = 0x01;

		uint64 ReferencedHeapID = 0;

		bool operator==(const ResourceDesc& Other) const
		{
			return CompareD3D12ResourceDesc(Other.ResDesc, ResDesc)
				&& (Other.IsUploadHeap == IsUploadHeap)
				&& (Other.Type == Type)
				&& (Other.CreationNodeIndex == CreationNodeIndex)
				&& (Other.NodeVisibilityMask == NodeVisibilityMask);
		}
	};

	uint64 GetSizeNeededForRes(const ResourceDesc& ResDesc)
	{
		D3D12_RESOURCE_ALLOCATION_INFO AllocInfo = D3DDevice->GetResourceAllocationInfo(ResDesc.NodeVisibilityMask, 1, &ResDesc.ResDesc);
		return AllocInfo.SizeInBytes;
	}

	struct HeapDesc
	{
		uint64 Size = 0;
		D3D12_HEAP_FLAGS Flags = D3D12_HEAP_FLAG_NONE;
		uint32 CreationNodeIndex = 0;
		uint32 NodeVisibilityMask = 0x01;

		bool CanUseFor(const HeapDesc& OtherDesc, uint64 CurrOffset)
		{
			return (CreationNodeIndex == OtherDesc.CreationNodeIndex)
				&& (Flags == OtherDesc.Flags)
				&& (OtherDesc.NodeVisibilityMask & ~NodeVisibilityMask) == 0
				&& (Size >= (CurrOffset + OtherDesc.Size));
		}
	};

	struct StandaloneHeap
	{
		ID3D12Heap* Ptr = nullptr;
		uint64 HeapID = 0;

		uint64 CurrentOffset = 0;

		HeapDesc Desc;

		uint64 FenceValueToWaitOn = 0;

		int32 CmdListUseCount = 0;
	};

	struct Resource
	{
		ID3D12Resource* Ptr = nullptr;
		ResourceDesc Desc;

		D3DRefWrapper<ID3D12Heap> ReferencedHeap;

		uint64 ResourceID = 0;

		uint64 FenceValueToWaitOn = 0;

		D3D12_RESOURCE_STATES CurrentState = D3D12_RESOURCE_STATE_COMMON;
		int32 CmdListUseCount = 0;
	};

	struct ResourceToTransition
	{
		uint64 ResourceID = 0;
		D3D12_RESOURCE_STATES NextState;
	};

	ID3D12Device* D3DDevice = nullptr;

	uint64 CurrentResourceID = 0;
	std::vector<Resource> LivingResources;
	std::vector<Resource> ResourcesPendingDelete;
	std::vector<Resource> ResourcesPendingCmdListFinish;

	uint64 CurrentHeapID = 0;
	std::vector<StandaloneHeap> LivingHeaps;
	std::vector<StandaloneHeap> HeapsPendingDelete;
	std::vector<StandaloneHeap> HeapsPendingCmdListFinish;

	uint64 AllocateHeap(HeapDesc Desc, ID3D12Heap** OutHeap)
	{
		D3D12_HEAP_DESC HeapDesc = {};
		HeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		HeapDesc.Properties.CreationNodeMask = (1 << Desc.CreationNodeIndex);
		HeapDesc.Properties.VisibleNodeMask = Desc.NodeVisibilityMask;
		HeapDesc.SizeInBytes = Desc.Size;
		HeapDesc.Flags = Desc.Flags;

		HRESULT hr = D3DDevice->CreateHeap(&HeapDesc, IID_PPV_ARGS(OutHeap));
		ASSERT(SUCCEEDED(hr));

		StandaloneHeap StandaloneHeap;
		StandaloneHeap.Desc = Desc;
		StandaloneHeap.Ptr = *OutHeap;
		StandaloneHeap.HeapID = CurrentHeapID;
		StandaloneHeap.CmdListUseCount = 1;
		CurrentHeapID++;

		LivingHeaps.push_back(StandaloneHeap);
		return StandaloneHeap.HeapID;
	}

	uint64 AllocateResource(ResourceDesc Desc, ID3D12Resource** OutRes)
	{
		Resource Res;

		const D3D12_RESOURCE_STATES InitialState = (Desc.IsUploadHeap ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST);

		if (Desc.Type == ResourceType::Committed)
		{
			D3D12_HEAP_PROPERTIES Props = {};
			Props.Type = (Desc.IsUploadHeap ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
			HRESULT hr = D3DDevice->CreateCommittedResource(&Props, D3D12_HEAP_FLAG_NONE, &Desc.ResDesc, InitialState, nullptr, IID_PPV_ARGS(OutRes));
			ASSERT(SUCCEEDED(hr));
		}
		else if (Desc.Type == ResourceType::Placed)
		{
			ASSERT(!Desc.IsUploadHeap);
			auto* ReferencedHeap = InternalGetHeapByID(Desc.ReferencedHeapID);

			HRESULT hr = D3DDevice->CreatePlacedResource(ReferencedHeap->Ptr, ReferencedHeap->CurrentOffset, &Desc.ResDesc, InitialState, nullptr, IID_PPV_ARGS(OutRes));
			ASSERT(SUCCEEDED(hr));

			uint64 SizeNeeded = GetSizeNeededForRes(Desc);
			// Round up to align with D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
			SizeNeeded = ((SizeNeeded + D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT - 1) / D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

			ReferencedHeap->CurrentOffset += SizeNeeded;

			Res.ReferencedHeap = ReferencedHeap->Ptr;
		}
		else
		{
			ASSERT(false);
		}

		Res.Ptr = *OutRes;
		Res.Desc = Desc;
		Res.CmdListUseCount = 1;
		Res.CurrentState = InitialState;
		Res.ResourceID = CurrentResourceID;
		CurrentResourceID++;

		LivingResources.push_back(Res);
		return Res.ResourceID;
	}

	Resource* InternalGetResourceByID(uint64 ResID)
	{
		for (auto& Resource : LivingResources)
		{
			if (Resource.ResourceID == ResID)
			{
				return &Resource;
			}
		}

		return nullptr;
	}

	StandaloneHeap* InternalGetHeapByID(uint64 HeapID)
	{
		for (auto& Heap : LivingHeaps)
		{
			if (Heap.HeapID == HeapID)
			{
				return &Heap;
			}
		}

		return nullptr;
	}

	uint64 AcquireResource(ResourceDesc Desc, ID3D12Resource** OutRes)
	{
		ASSERT(Desc.Type == ResourceType::Committed);
		for (auto& Resource : LivingResources)
		{
			if (Resource.Desc == Desc && Resource.CmdListUseCount == 0)
			{
				// AddRef I guess?
				*OutRes = Resource.Ptr;
				Resource.CmdListUseCount++;
				return Resource.ResourceID;
			}
		}

		uint64 ResID = AllocateResource(Desc, OutRes);
		return ResID;
	}

	uint64 AcquirePlacedResource(ResourceDesc Desc, ID3D12Resource** OutRes, uint64* OutHeapID)
	{
		ASSERT(Desc.Type == ResourceType::Placed);
		for (auto& Resource : LivingResources)
		{
			if (Resource.Desc == Desc && Resource.CmdListUseCount == 0)
			{
				// AddRef I guess?
				*OutRes = Resource.Ptr;
				Resource.CmdListUseCount++;
				return Resource.ResourceID;
			}
		}

		ResourceLifecycleManager::HeapDesc HeapDesc;
		uint64 Size = GetSizeNeededForRes(Desc);
		// Make our heaps in 16 MB chunks
		// TODO: Tunable/config
		const uint64 AlignmentThreshold = 16 * 1024 * 1024;
		Size = ((Size + AlignmentThreshold - 1) / AlignmentThreshold) * AlignmentThreshold;
		HeapDesc.Size = Size;

		HeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;

		ID3D12Heap* UnusedHeapPtr = nullptr;
		uint64 HeapID = AcquireHeap(HeapDesc, &UnusedHeapPtr);

		Desc.ReferencedHeapID = HeapID;
		*OutHeapID = HeapID;

		uint64 ResID = AllocateResource(Desc, OutRes);
		return ResID;
	}

	uint64 AcquireHeap(HeapDesc Desc, ID3D12Heap** OutRes)
	{
		for (auto& Heap : LivingHeaps)
		{
			if (Heap.Desc.CanUseFor(Desc, Heap.CurrentOffset))
			{
				*OutRes = Heap.Ptr;
				Heap.CmdListUseCount++;
				return Heap.HeapID;
			}
		}

		uint64 HeapID = AllocateHeap(Desc, OutRes);
		return HeapID;
	}

	void RelinquishHeap(uint64 HeapID)
	{
		for (auto& Heap : LivingHeaps)
		{
			if (Heap.HeapID == HeapID)
			{
				Heap.CmdListUseCount--;
				return;
			}
		}

		for (auto& Heap : HeapsPendingDelete)
		{
			if (Heap.HeapID == HeapID)
			{
				Heap.CmdListUseCount--;
				return;
			}
		}
	}

	void RelinquishResource(uint64 ResourceID)
	{
		for (auto& Resource : LivingResources)
		{
			if (Resource.ResourceID == ResourceID)
			{
				Resource.CmdListUseCount--;
				return;
			}
		}

		for (auto& Resource : ResourcesPendingDelete)
		{
			if (Resource.ResourceID == ResourceID)
			{
				Resource.CmdListUseCount--;
				return;
			}
		}

		ASSERT(false && "adkfubasfd");
	}

	void RequestHeapDestroyed(uint64 HeapID)
	{
		for (int32 i = 0; i < LivingHeaps.size(); i++)
		{
			const auto& Heap = LivingHeaps[i];
			if (Heap.HeapID == HeapID)
			{
				HeapsPendingDelete.push_back(Heap);
				VectorSwapErase(&LivingHeaps, i);
				return;
			}
		}
	}

	void RequestResourceDestroyed(uint64 ResourceID)
	{
		for (int32 i = 0; i < LivingResources.size(); i++)
		{
			const auto& Resource = LivingResources[i];
			if (Resource.ResourceID == ResourceID)
			{
				ResourcesPendingDelete.push_back(Resource);
				VectorSwapErase(&LivingResources, i);
				return;
			}
		}
	}

	void ResetAllHeapOffsets()
	{
		for (auto& Heap : LivingHeaps)
		{
			Heap.CurrentOffset = 0;
		}
	}

	void OnFrameFenceSignaled(uint64 SignaledValue)
	{
		for (int32 i = 0; i < ResourcesPendingDelete.size(); i++)
		{
			auto& Resource = ResourcesPendingDelete[i];
			if (Resource.CmdListUseCount == 0)
			{
				Resource.FenceValueToWaitOn = SignaledValue;
				ResourcesPendingCmdListFinish.push_back(Resource);
				VectorSwapErase(&ResourcesPendingDelete, i);
				i--;
			}
		}

		for (int32 i = 0; i < HeapsPendingDelete.size(); i++)
		{
			auto& Heap = HeapsPendingDelete[i];
			if (Heap.CmdListUseCount == 0)
			{
				Heap.FenceValueToWaitOn = SignaledValue;
				HeapsPendingCmdListFinish.push_back(Heap);
				VectorSwapErase(&HeapsPendingDelete, i);
				i--;
			}
		}
	}

	void CheckIfFenceFinished(uint64 FrameFenceValue)
	{
		for (int32 i = 0; i < ResourcesPendingCmdListFinish.size(); i++)
		{
			auto& Resource = ResourcesPendingCmdListFinish[i];

			if (Resource.FenceValueToWaitOn <= FrameFenceValue)
			{
				Resource.Ptr->Release();

				VectorSwapErase(&ResourcesPendingCmdListFinish, i);
				i--;
			}
		}

		for (int32 i = 0; i < HeapsPendingCmdListFinish.size(); i++)
		{
			auto& Heap = HeapsPendingCmdListFinish[i];

			if (FrameFenceValue >= Heap.FenceValueToWaitOn)
			{
				Heap.Ptr->Release();

				VectorSwapErase(&HeapsPendingCmdListFinish, i);
				i--;
			}
		}

		for (int32 i = 0; i < OtherObjectsToDestroy.size(); i++)
		{
			auto& GPUObj = OtherObjectsToDestroy[i];

			if (GPUObj.FenceValue <= FrameFenceValue)
			{
				if (GPUObj.ObjType == OtherGPUObject::Type::RootSignature)
				{
					((ID3D12RootSignature*)GPUObj.Obj)->Release();
				}
				else if (GPUObj.ObjType == OtherGPUObject::Type::PipelineStateObject)
				{
					((ID3D12PipelineState*)GPUObj.Obj)->Release();
				}
				else if (GPUObj.ObjType == OtherGPUObject::Type::DescriptorHeap)
				{
					((ID3D12DescriptorHeap*)GPUObj.Obj)->Release();
				}
				else
				{
					ASSERT(false && "sdfkbskfhj");
				}

				OtherObjectsToDestroy[i] = OtherObjectsToDestroy.back();
				OtherObjectsToDestroy.pop_back();
				i--;
			}
		}
	}

	void PerformResourceTransitions(const std::vector<ResourceToTransition>& ResourceTransitions, ID3D12GraphicsCommandList* CommandList)
	{
		std::vector<D3D12_RESOURCE_BARRIER> Barriers;
		
		for (const auto& ResourceTransition : ResourceTransitions)
		{
			if (auto* Resource = InternalGetResourceByID(ResourceTransition.ResourceID))
			{
				if (Resource->CurrentState != ResourceTransition.NextState)
				{
					D3D12_RESOURCE_BARRIER Barrier = {};
					Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					Barrier.Transition.pResource = Resource->Ptr;
					Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					Barrier.Transition.StateBefore = Resource->CurrentState;
					Barrier.Transition.StateAfter = ResourceTransition.NextState;

					Barriers.push_back(Barrier);

					Resource->CurrentState = ResourceTransition.NextState;
				}
			}
		}
		
		if (!Barriers.empty())
		{
			CommandList->ResourceBarrier(Barriers.size(), Barriers.data());
		}
	}

	struct OtherGPUObject
	{
		enum struct Type
		{
			RootSignature,
			PipelineStateObject,
			DescriptorHeap
		};

		Type ObjType = Type::RootSignature;
		void* Obj = nullptr;
		uint64 FenceValue = 0;
	};

	std::vector<OtherGPUObject> OtherObjectsToDestroy;

	void DeferredDelete(ID3D12RootSignature* RootSig, uint64 FenceValue)
	{
		OtherGPUObject GPUObject;
		GPUObject.ObjType = OtherGPUObject::Type::RootSignature;
		GPUObject.Obj = RootSig;
		GPUObject.FenceValue = FenceValue;
		OtherObjectsToDestroy.push_back(GPUObject);
	}

	void DeferredDelete(ID3D12PipelineState* PSO, uint64 FenceValue)
	{
		OtherGPUObject GPUObject;
		GPUObject.ObjType = OtherGPUObject::Type::PipelineStateObject;
		GPUObject.Obj = PSO;
		GPUObject.FenceValue = FenceValue;
		OtherObjectsToDestroy.push_back(GPUObject);
	}

	void DeferredDelete(ID3D12DescriptorHeap* DescriptorHeap, uint64 FenceValue)
	{
		OtherGPUObject GPUObject;
		GPUObject.ObjType = OtherGPUObject::Type::DescriptorHeap;
		GPUObject.Obj = DescriptorHeap;
		GPUObject.FenceValue = FenceValue;
		OtherObjectsToDestroy.push_back(GPUObject);
	}


	// Request resource state change...might need to be atomic w.r.t. the command list submit
	// Ugh....but that would kill perf

	// Alternative: each thread has its own resource pool?
	// And if we wanted to share resources b/w them (like for MGPU) we could...do something...

	//void OnCommandListSubmitted() { }
	//void Tick() { }
};


struct CommandListReclaimer
{
	struct CommandListToReclaim
	{
		ID3D12GraphicsCommandList* CmdList = nullptr;
		uint64 FenceValueToWaitOn = 0;
	};

	struct CommandAllocatorToReclaim
	{
		ID3D12CommandAllocator* CmdAllocator = nullptr;
		uint64 FenceValueToWaitOn = 0;
	};

	std::vector<CommandListToReclaim> CmdListPendingNextFence;
	std::vector<CommandListToReclaim> CmdListWaitingForFence;
	std::vector<CommandListToReclaim> CmdListNowAvailable;

	std::vector<CommandAllocatorToReclaim> CmdAllocPendingNextFence;
	std::vector<CommandAllocatorToReclaim> CmdAllocWaitingForFence;
	std::vector<CommandAllocatorToReclaim> CmdAllocNowAvailable;

	void NowDoneWithCommandList(ID3D12GraphicsCommandList* CmdList)
	{
		// TODO: Oh no
		//CmdList->AddRef();
		CmdListPendingNextFence.push_back({ CmdList, 0 });
	}

	void NowDoneWithCommandAllocator(ID3D12CommandAllocator* CmdList)
	{
		// TODO: Oh no
		//CmdList->AddRef();
		CmdAllocPendingNextFence.push_back({ CmdList, 0 });
	}

	void OnFrameFenceSignaled(uint64 SignaledValue)
	{
		for (auto Pending : CmdListPendingNextFence)
		{
			CmdListWaitingForFence.push_back({ Pending.CmdList, SignaledValue });
		}

		CmdListPendingNextFence.clear();

		for (auto Pending : CmdAllocPendingNextFence)
		{
			CmdAllocWaitingForFence.push_back({ Pending.CmdAllocator, SignaledValue });
		}

		CmdAllocPendingNextFence.clear();
	}

	void CheckIfFenceFinished(uint64 FrameFenceValue)
	{
		for (int32 i = 0; i < CmdListWaitingForFence.size(); i++)
		{
			if (CmdListWaitingForFence[i].FenceValueToWaitOn <= FrameFenceValue)
			{
				CmdListNowAvailable.push_back(CmdListWaitingForFence[i]);
				CmdListWaitingForFence[i] = CmdListWaitingForFence.back();
				CmdListWaitingForFence.pop_back();
				i--;
			}
		}

		for (int32 i = 0; i < CmdAllocWaitingForFence.size(); i++)
		{
			if (CmdAllocWaitingForFence[i].FenceValueToWaitOn <= FrameFenceValue)
			{
				CmdAllocNowAvailable.push_back(CmdAllocWaitingForFence[i]);
				CmdAllocWaitingForFence[i] = CmdAllocWaitingForFence.back();
				CmdAllocWaitingForFence.pop_back();
				i--;
			}
		}
	}

	ID3D12CommandAllocator* GetOpenCommandAllocator()
	{
		if (CmdAllocNowAvailable.size() > 0)
		{
			auto* CmdAlloc = CmdAllocNowAvailable.back().CmdAllocator;
			CmdAllocNowAvailable.pop_back();

			CmdAlloc->Reset();
			return CmdAlloc;
		}
		else
		{
			return nullptr;
		}
	}

	ID3D12GraphicsCommandList* GetOpenCommandList(ID3D12CommandAllocator* CommandAllocator)
	{
		if (CmdListNowAvailable.size() > 0)
		{
			auto* CmdList = CmdListNowAvailable.back().CmdList;
			CmdListNowAvailable.pop_back();

			CmdList->Reset(CommandAllocator, nullptr);
			return CmdList;
		}
		else
		{
			return nullptr;
		}
	}
};

