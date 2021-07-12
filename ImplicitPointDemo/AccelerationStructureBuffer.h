#pragma once
#include "GpuBuffer.h"
#include "GraphicsCore.h"

class AccelerationStructureBuffer final : public GpuBuffer
{
public:
	virtual void Destroy(void) override
	{
		GpuBuffer::Destroy();
	}

	virtual void CreateDerivedViews(void) override
	{
		//Create Views
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = m_GpuVirtualAddress;
	};
};