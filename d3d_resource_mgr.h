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
	else
	{
		return false;
	}
}

struct ResourceLifecycleManager
{
	struct ResourceDesc
	{
		D3D12_RESOURCE_DESC ResDesc;
		bool IsUploadHeap = false;
		uint32 CreationNodeIndex = 0;
		uint32 NodeVisibilityMask = 0x01;

		bool operator==(const ResourceDesc& Other) const
		{
			return CompareD3D12ResourceDesc(Other.ResDesc, ResDesc)
				&& (Other.IsUploadHeap == IsUploadHeap)
				&& (Other.CreationNodeIndex == CreationNodeIndex)
				&& (Other.NodeVisibilityMask == NodeVisibilityMask);
		}
	};

	struct Resource
	{
		ID3D12Resource* Ptr = nullptr;
		ResourceDesc Desc;

		uint64 ResourceID = 0;

		uint64 FenceValueToWaitOn = 0;

		D3D12_RESOURCE_STATES CurrentState = D3D12_RESOURCE_STATE_COMMON;
		int32 CmdListUseCount = 0;
	};

	ID3D12Device* D3DDevice = nullptr;

	uint64 CurrentResourceID = 0;

	std::vector<Resource> LivingResources;
	std::vector<Resource> ResourcesPendingDelete;
	std::vector<Resource> ResourcesPendingCmdListFinish;

	uint64 AllocateResource(ResourceDesc Desc, ID3D12Resource** OutRes)
	{

		D3D12_HEAP_PROPERTIES Props = {};
		Props.Type = (Desc.IsUploadHeap ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
		const D3D12_RESOURCE_STATES InitialState = (Desc.IsUploadHeap ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST);
		HRESULT hr = D3DDevice->CreateCommittedResource(&Props, D3D12_HEAP_FLAG_NONE, &Desc.ResDesc, InitialState, nullptr, IID_PPV_ARGS(OutRes));
		ASSERT(SUCCEEDED(hr));

		Resource Res;
		Res.Ptr = *OutRes;
		Res.CmdListUseCount = 1;
		Res.CurrentState = InitialState;
		Res.ResourceID = CurrentResourceID;
		CurrentResourceID++;

		LivingResources.push_back(Res);
		return Res.ResourceID;
	}

	uint64 AcquireResource(ResourceDesc Desc, ID3D12Resource** OutRes)
	{
		for (auto& Resource : LivingResources)
		{
			if (Resource.Desc == Desc)
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

	void RequestResourceDestroyed(uint64 ResourceID)
	{
		for (int32 i = 0; i < LivingResources.size(); i++)
		{
			const auto& Resource = LivingResources[i];
			if (Resource.ResourceID == ResourceID)
			{
				ResourcesPendingDelete.push_back(Resource);
				LivingResources[i] = LivingResources.back();
				LivingResources.pop_back();
				return;
			}
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
				ResourcesPendingDelete[i] = ResourcesPendingDelete.back();
				ResourcesPendingDelete.pop_back();
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

				ResourcesPendingCmdListFinish[i] = ResourcesPendingCmdListFinish.back();
				ResourcesPendingCmdListFinish.pop_back();
				i--;
			}
		}
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

