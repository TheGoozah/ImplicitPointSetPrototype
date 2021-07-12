#include "SharedData.hlsli"

PointGSInput main(PointInput vsInput, uint vertexID : SV_VertexID)
{
    //Sample from generated point sample buffer, and return its absolute sample position for the GS
    PointGSInput output;
    output.position = float4(PointSampleBuffer[vertexID].sample, 1.f);
    output.seed = PointSampleBuffer[vertexID].seed;
    return output;
}