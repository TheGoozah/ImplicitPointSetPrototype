//Accumulate the AO results of this frame. This uses a temporary 2D hash table with the size of the screen dimensions, which gets cleared every frame.
//Using atomic operations, both results and count are accumulated. During the merge pass, the data get's averaged
//and added to the 3D hash table representation of the world. This extra step is necessary to be able to combine results
//that would write to the same 3D "location" in the 3D hash table.
#include "HashTable.hlsli"
#include "SharedUtilities.hlsli"

struct SampleData
{
    uint seed;     //4 bytes
    uint prevSeed; //4 bytes
    float3 sample; //12 bytes
};
Texture2D<float4>               AOBuffer            : register(t0);
StructuredBuffer<SampleData>    PointSampleBuffer   : register(t1);
RWStructuredBuffer<KeyData>     AccumulationBuffer  : register(u1);
cbuffer HashTableConstants                          : register(b0)
{
    uint WorldHashTableElementCount;
    uint AccumulationHashTableElementCount;
    uint WorldHashTableValueFractionalBits;
    uint WorldHashTableCountFractionalBits;
    uint AccumulationHashTableValueFractionalBits;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 screenDimensions = uint2(1920, 1080);
    
    //Input Data
    const uint index = uint(DTid.x + (DTid.y * screenDimensions.x));
    if (index >= AccumulationHashTableElementCount)
        return;
    
    const float aoValue = AOBuffer[DTid.xy].r;
    const uint aoFixedRepresentation = ToFixedPoint(aoValue, AccumulationHashTableValueFractionalBits);
    const SampleData data = PointSampleBuffer[index];
    if(data.seed == 0)
        return;
    
    HashTableIncrement(AccumulationBuffer, AccumulationHashTableElementCount, data.seed, aoFixedRepresentation);
    if (data.prevSeed != 0 && data.prevSeed != data.seed)
        HashTableIncrement(AccumulationBuffer, AccumulationHashTableElementCount, data.prevSeed, aoFixedRepresentation);
}