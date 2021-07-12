#include "HashTable.hlsli"
#include "SharedUtilities.hlsli"

struct SampleData
{
    uint seed;     //4 bytes
    uint prevSeed; //4 bytes
    float3 sample; //12 bytes
};
StructuredBuffer<SampleData> PointSampleBuffer  : register(t0);
StructuredBuffer<KeyData>    AccumulationBuffer : register(t1);
RWStructuredBuffer<KeyData>  WorldHashTable     : register(u1);
cbuffer HashTableConstants                      : register(b0)
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
    
    //Store in World Hash Table
    const KeyData accumulatedData = HashTableLookupBySlotID(AccumulationBuffer, AccumulationHashTableElementCount, index);
    if (accumulatedData.key == 0) //No data accumulated, exit
        return;
    
    const uint se = accumulatedData.key; //Because no additional hashing in hashtable, the key is the seed value used
    const KeyData cachedData = HashTableLookup(WorldHashTable, WorldHashTableElementCount, se);
    
    //Perform constant rescale to prevent overflow
    const float constantOverflowMultiplier = 0.9f;
    const float scaledAccumulatedCount = accumulatedData.count * constantOverflowMultiplier;
    const float cachedCount = FromFixedPoint(cachedData.count, WorldHashTableCountFractionalBits);
    
    const float totalCount = scaledAccumulatedCount + cachedCount;
    const float cachedValueTerm = (cachedCount / totalCount) * FromFixedPoint(cachedData.value, WorldHashTableValueFractionalBits);
    const float accumulationValueTerm = (scaledAccumulatedCount / totalCount) * (FromFixedPoint(accumulatedData.value, AccumulationHashTableValueFractionalBits) / accumulatedData.count);
    
    const uint valueToStore = ToFixedPoint(cachedValueTerm + accumulationValueTerm, WorldHashTableValueFractionalBits);
    const uint countToStore = ToFixedPoint(totalCount, WorldHashTableCountFractionalBits);

    //Store in World Hash Table
    HashTableInsert(WorldHashTable, WorldHashTableElementCount, se, valueToStore, countToStore);
}