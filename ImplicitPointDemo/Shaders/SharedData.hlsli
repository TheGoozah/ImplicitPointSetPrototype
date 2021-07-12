#ifndef __SHARED_DATA__
#define __SHARED_DATA__

//---- DATA ----
cbuffer SharedConstants : register(b0)
{
    float4x4 WorldViewProjectionMatrix;
    float4x4 ViewProjectionInverseMatrix;
    float4x4 ViewInverseMatrix;
    float4x4 WorldMatrix;
    float4 ViewVector;
    uint VoxelConnectivity;
    uint Level;
    uint MaxLevels;
    uint CellSize;
    uint LODMode;
}

Texture2D<float>  DepthBuffer       : register(t0);
Texture2D<float4> NormalBuffer      : register(t1);
Texture2D<float4> TangentBuffer     : register(t2);
Texture2D<float4> BitangentBuffer   : register(t3);
Texture2D<float4> BlurredAOBuffer   : register(t4);

struct SampleData
{
    uint seed;      //4 bytes
    uint prevSeed;  //4 bytes
    float3 sample;  //12 bytes
};
RWStructuredBuffer<SampleData>  PointSampleBuffer   : register(u3);
RWStructuredBuffer<uint2>       LODBuffer           : register(u4);

//---- STRUCTS ----
struct MeshVertexInput
{
    float3 position     : POSITION;
    float2 texcoord0    : TEXCOORD;
    float3 normal       : NORMAL;
    float3 tangent      : TANGENT;
    float3 bitangent    : BITANGENT;
};

struct MeshVertexOutput
{
    float4 position         : SV_POSITION;
    float3 worldNormal      : NORMAL;
    float3 worldTangent     : TANGENT;
    float3 worldBitangent   : BITANGENT;
};

struct SampleGenerationInput
{
    float3 position     : POSITION;
};

struct SampleGenerationOutput
{
    float4 position     : SV_POSITION;
};

//BELOW DEBUG VISUALIZATION ONLY
struct PointInput
{
    float4 position     : POSITION;
};

struct PointGSInput
{
    uint seed           : SEED;
    float4 position     : POSITION;
};

struct PointOutput
{
    float4 position     : SV_POSITION;
};
#endif