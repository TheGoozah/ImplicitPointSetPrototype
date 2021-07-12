#include "SharedData.hlsli"

float4 main(MeshVertexOutput input) : SV_TARGET
{
    const float3 lightDirection = float3(-0.577f, 0.577f, 0.577f);
    const float3 ambientColor = float3(0.025f, 0.025f, 0.025f);
    
    const float diffStrength = max(dot(input.worldNormal, lightDirection), 0.f);
    const float3 diffuseColor = float3(diffStrength, diffStrength, diffStrength);
    return float4(diffuseColor + ambientColor, 1.f);
}