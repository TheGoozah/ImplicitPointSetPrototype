#include "HashTable.hlsli"

RWStructuredBuffer<KeyData> AccumulationBuffer  : register(u1);
RWStructuredBuffer<uint2> LODBuffer             : register(u2);
cbuffer HashTableConstants                      : register(b0)
{
    uint WorldHashTableElementCount;
    uint AccumulationHashTableElementCount;
    uint WorldHashTableValueFractionalBits;
    uint WorldHashTableCountFractionalBits;
    uint AccumulationHashTableValueFractionalBits;
}

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    const uint2 screenDimensions = uint2(1920, 1080);
    const uint amountElements = screenDimensions.x * screenDimensions.y;
    
    const uint index = uint(DTid.x + (DTid.y * screenDimensions.x));
    if (index < amountElements)
    {
        AccumulationBuffer[index].key = 0;
        AccumulationBuffer[index].value = 0;
        AccumulationBuffer[index].count = 0;
        LODBuffer[index] = uint2(0, 0);
    }
}