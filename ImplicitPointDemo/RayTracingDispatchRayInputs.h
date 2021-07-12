#pragma once
#include <d3d12.h>
#include <vector>
#include <atlbase.h>

#include "GpuBuffer.h"

struct RayTracingDispatchRayInputs
{
	RayTracingDispatchRayInputs() : m_HitGroupStride(0) {}
	RayTracingDispatchRayInputs(
		ID3D12StateObject* pPSO,
		void* pHitGroupShaderTable,
		UINT HitGroupStride,
		UINT HitGroupTableSize,
		LPCWSTR rayGenExportName,
		LPCWSTR missExportName) : m_pPSO(pPSO), m_HitGroupStride(HitGroupStride)
	{
		const UINT shaderTableSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		ID3D12StateObjectProperties* stateObjectProperties = nullptr;
		if (!FAILED(pPSO->QueryInterface(IID_PPV_ARGS(&stateObjectProperties))))
		{
			void* pRayGenShaderData = stateObjectProperties->GetShaderIdentifier(rayGenExportName);
			void* pMissShaderData = stateObjectProperties->GetShaderIdentifier(missExportName);

			m_HitGroupStride = HitGroupStride;

			// MiniEngine requires that all initial data be aligned to 16 bytes
			UINT alignment = 16;
			std::vector<BYTE> alignedShaderTableData(shaderTableSize + alignment - 1);
			BYTE* pAlignedShaderTableData = alignedShaderTableData.data() + ((UINT64)alignedShaderTableData.data() % alignment);
			memcpy(pAlignedShaderTableData, pRayGenShaderData, shaderTableSize);
			m_RayGenShaderTable.Create(L"Ray Gen Shader Table", 1, shaderTableSize, alignedShaderTableData.data());

			memcpy(pAlignedShaderTableData, pMissShaderData, shaderTableSize);
			m_MissShaderTable.Create(L"Miss Shader Table", 1, shaderTableSize, alignedShaderTableData.data());

			m_HitShaderTable.Create(L"Hit Shader Table", 1, HitGroupTableSize, pHitGroupShaderTable);
		}
		stateObjectProperties->Release();
	}

	~RayTracingDispatchRayInputs()
	{
		m_HitShaderTable.Destroy();
		m_MissShaderTable.Destroy();
		m_RayGenShaderTable.Destroy();
		m_pPSO.Release();
	}

	D3D12_DISPATCH_RAYS_DESC GetDispatchRayDesc(UINT DispatchWidth, UINT DispatchHeight)
	{
		D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = {};

		dispatchRaysDesc.RayGenerationShaderRecord.StartAddress = m_RayGenShaderTable.GetGpuVirtualAddress();
		dispatchRaysDesc.RayGenerationShaderRecord.SizeInBytes = m_RayGenShaderTable.GetBufferSize();
		dispatchRaysDesc.HitGroupTable.StartAddress = m_HitShaderTable.GetGpuVirtualAddress();
		dispatchRaysDesc.HitGroupTable.SizeInBytes = m_HitShaderTable.GetBufferSize();
		dispatchRaysDesc.HitGroupTable.StrideInBytes = m_HitGroupStride;
		dispatchRaysDesc.MissShaderTable.StartAddress = m_MissShaderTable.GetGpuVirtualAddress();
		dispatchRaysDesc.MissShaderTable.SizeInBytes = m_MissShaderTable.GetBufferSize();
		dispatchRaysDesc.MissShaderTable.StrideInBytes = dispatchRaysDesc.MissShaderTable.SizeInBytes; // Only one entry
		dispatchRaysDesc.Width = DispatchWidth;
		dispatchRaysDesc.Height = DispatchHeight;
		dispatchRaysDesc.Depth = 1;
		return dispatchRaysDesc;
	}

	UINT m_HitGroupStride;
	CComPtr<ID3D12StateObject> m_pPSO;
	ByteAddressBuffer   m_RayGenShaderTable;
	ByteAddressBuffer   m_MissShaderTable;
	ByteAddressBuffer   m_HitShaderTable;
};