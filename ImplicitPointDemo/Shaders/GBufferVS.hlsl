#include "SharedData.hlsli"

MeshVertexOutput main(MeshVertexInput input)
{
    MeshVertexOutput output = (MeshVertexOutput) 0;
    output.position = mul(WorldViewProjectionMatrix, float4(input.position, 1.f));
    output.worldNormal = mul((float3x3) WorldMatrix, input.normal);
    output.worldTangent = mul((float3x3) WorldMatrix, input.tangent);
    output.worldBitangent = mul((float3x3) WorldMatrix, input.bitangent);
    return output;
}