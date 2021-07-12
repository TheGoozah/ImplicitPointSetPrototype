#include "SharedData.hlsli"

struct GBuffer
{
    float4 worldNormal      : SV_TARGET0;
    float4 worldTangent     : SV_TARGET1;
    float4 worldBitangent   : SV_TARGET2;
};

GBuffer main(MeshVertexOutput input)
{
    GBuffer output = (GBuffer) 0;
    output.worldNormal = float4(input.worldNormal, 0.f);
    output.worldTangent = float4(input.worldTangent, 0.f);
    output.worldBitangent = float4(input.worldBitangent, 0.f);
    return output;
}