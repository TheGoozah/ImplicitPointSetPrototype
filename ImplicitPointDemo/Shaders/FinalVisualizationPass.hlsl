#include "HashTable.hlsli"
#include "SharedUtilities.hlsli"
#include "HashFunctions.hlsli"
#include "SampleGenerationFunctions.hlsli"
#include "LODFunctions.hlsli"

struct SampleData
{
    uint seed;     //4 bytes
    uint prevSeed; //4 bytes
    float3 sample; //12 bytes
};
StructuredBuffer<SampleData>    PointSampleBuffer   : register(t0);
StructuredBuffer<KeyData>       WorldHashTable      : register(t1);
StructuredBuffer<uint2>         LODBuffer           : register(t2);
Texture2D<float>                DepthBuffer         : register(t3);
RWTexture2D<float4>             VisualizationBuffer : register(u1);
cbuffer HashTableConstants                          : register(b0)
{
    uint WorldHashTableElementCount;
    uint AccumulationHashTableElementCount;
    uint WorldHashTableValueFractionalBits;
    uint WorldHashTableCountFractionalBits;
    uint AccumulationHashTableValueFractionalBits;
}
cbuffer CameraData                                  : register(b1)
{
    float4x4 ViewProjectionInverseMatrix;
    float4x4 ViewInverseMatrix;
    float4   ViewVector;
};
cbuffer Modes                                       : register(b2)
{
    uint MaxLevels;
    uint LODMode;
    bool QuarterSPP;
}
cbuffer DemoConstants                               : register(b3)
{
    uint DiscreteCellSize;
    uint VoxelConnectivity;
}

//Visualize the pixel using only the value of the cached shading information, for that pixel, in the discrete structure (no filtering)
float SingleSample(in uint index1D, in uint2 screenDimensions)
{
    const uint amountElements = screenDimensions.x * screenDimensions.y;
    if (index1D >= amountElements)
        return 0.f;
    
    const SampleData pixelSampleData = PointSampleBuffer[index1D];
    if (pixelSampleData.seed == 0)
        return 0.f;
    
    const KeyData cachedData = HashTableLookup(WorldHashTable, WorldHashTableElementCount, pixelSampleData.seed);
    return FromFixedPoint(cachedData.value, WorldHashTableValueFractionalBits);
}

//Visualize the pixel by interpolating in world space using a modified Shepard interpolation. Single level as it doesn't guarantee data
//for previous levels.
float WorldSpaceShepardInterpolationSingleLevel(in uint discreteCellSize, in uint level, in float3 pixelWorldPosition, in uint maxLevels)
{
    //Shepard Interpolation Variables
    const float searchRadius = float(discreteCellSize) / float(1 << level);
    float valueSum = 0.f;
    float weightSum = 0.f;
    
    //Discretize position based on discrete cell size and current level
    const float deltaOnLevel = 1.f / (1 << level);
    const float cellSizeBasedOnLevel = discreteCellSize * deltaOnLevel;
    const float3 discretePosition = Discretize(pixelWorldPosition, cellSizeBasedOnLevel);
    
    //For that discrete position, interate over all the neighbouring voxels(based on the size of cell size)
    for (uint v = 0; v < (VoxelConnectivity + 1); ++v)
    {
        //Get the offset and calculate the neighbouring absolute position
        const int3 neighbourOffsets = NeighbourOffsets[v];
        const float3 neighbourPosition = discretePosition + (neighbourOffsets * cellSizeBasedOnLevel);
        
        //Get the top level discrete position of the neighbour position. This is due to the fact that we want to find the quadrant in a normalized
        //fashion and the fact that the neighbour position might be in the same or another voxel as the position!
        const float3 neighbourDiscreteTopPosition = Discretize(neighbourPosition, discreteCellSize);
        const float3 normalizedRelativePosition = abs((neighbourDiscreteTopPosition - neighbourPosition) / (int) discreteCellSize);
        const float deltaNextLevel = deltaOnLevel * 0.5f;
        const float3 centerNormalizedRelativePosition = normalizedRelativePosition + float3(deltaNextLevel, deltaNextLevel, deltaNextLevel);

        //Create samples in the current sub voxel & find closest!
        const float3 acp = neighbourDiscreteTopPosition + (centerNormalizedRelativePosition * discreteCellSize);
        const uint absoluteSeed = GetSeedWithSignedBits(acp, maxLevels);
        for (uint i = 0; i < 8; ++i)
        {
            SampleData currentSample;
            currentSample.seed = absoluteSeed + i;
            currentSample.sample = neighbourPosition + (GenerateSampleInVoxelQuadrant(absoluteSeed, level, i) * cellSizeBasedOnLevel);

            //Interpolation - Due to going over all points per subvoxel, duplicates are not possible          
            const KeyData cachedData = HashTableLookup(WorldHashTable, WorldHashTableElementCount, currentSample.seed);
            if (cachedData.key != 0)
            {
                const float sampleCount = FromFixedPoint(cachedData.count, WorldHashTableCountFractionalBits);
                const float dist = distance(pixelWorldPosition, currentSample.sample);
                const float weight = clamp(searchRadius - dist, 0.f, searchRadius) * sampleCount;
                
                valueSum += FromFixedPoint(cachedData.value, WorldHashTableValueFractionalBits) * weight;
                weightSum += weight;
            }
        }
    }
    
    if (weightSum > 0)
        valueSum /= weightSum;
    
    return valueSum;
}

//Visualize the pixel by interpolating in world space using a modified Shepard interpolation. Multiple levels as it acquires data from previous levels.
float WorldSpaceShepardInterpolationMultipleLevels(in uint discreteCellSize, in uint2 lhLOD, in float3 pixelWorldPosition, in uint maxLevels)
{
    //Shepard Interpolation Variables
    const uint voxelConnectivity = 27;
    const float searchRadius = float(discreteCellSize) / float(1 << lhLOD.y); //Most coarse level for interpolation
    float valueSum = 0.f;
    float weightSum = 0.f;
    
    for (uint l = lhLOD.y; l <= lhLOD.x; ++l)
    {
        //Discretize position based on discrete cell size and current level
        const float deltaOnLevel = 1.f / (1 << l);
        const float cellSizeBasedOnLevel = discreteCellSize * deltaOnLevel;
        const float3 discretePosition = Discretize(pixelWorldPosition, cellSizeBasedOnLevel);
    
        //For that discrete position, interate over all the neighbouring voxels(based on the size of cell size)
        for (uint v = 0; v < voxelConnectivity; ++v)
        {
            //Get the offset and calculate the neighbouring absolute position
            const int3 neighbourOffsets = NeighbourOffsets[v];
            const float3 neighbourPosition = discretePosition + (neighbourOffsets * cellSizeBasedOnLevel);
        
            //Get the top level discrete position of the neighbour position. This is due to the fact that we want to find the quadrant in a normalized
            //fashion and the fact that the neighbour position might be in the same or another voxel as the position!
            const float3 neighbourDiscreteTopPosition = Discretize(neighbourPosition, discreteCellSize);
            const float3 normalizedRelativePosition = abs((neighbourDiscreteTopPosition - neighbourPosition) / (int) discreteCellSize);
            const float deltaNextLevel = deltaOnLevel * 0.5f;
            const float3 centerNormalizedRelativePosition = normalizedRelativePosition + float3(deltaNextLevel, deltaNextLevel, deltaNextLevel);
        
            //Create samples in the current sub voxel & find closest!
            const float3 acp = neighbourDiscreteTopPosition + (centerNormalizedRelativePosition * discreteCellSize);
            const uint absoluteSeed = GetSeedWithSignedBits(acp, maxLevels);
            for (uint i = 0; i < 8; ++i)
            {
                SampleData currentSample;
                currentSample.seed = absoluteSeed + i;
                currentSample.sample = neighbourPosition + (GenerateSampleInVoxelQuadrant(absoluteSeed, l, i) * cellSizeBasedOnLevel);
            
                //Interpolation - Due to going over all points per subvoxel, duplicates are not possible          
                const KeyData cachedData = HashTableLookup(WorldHashTable, WorldHashTableElementCount, currentSample.seed);
                if (cachedData.key != 0)
                {
                    const float sampleCount = FromFixedPoint(cachedData.count, WorldHashTableCountFractionalBits);
                    const float dist = distance(pixelWorldPosition, currentSample.sample);
                    const float weight = clamp(searchRadius - dist, 0.f, searchRadius) * sampleCount;
                
                    valueSum += FromFixedPoint(cachedData.value, WorldHashTableValueFractionalBits) * weight;
                    weightSum += weight;
                }
            }
        }
    }
    
    if (weightSum > 0)
        valueSum /= weightSum;
    
    return valueSum;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    //Indexing
    const uint2 screenDimensions = uint2(1920, 1080);
    const uint index = uint(DTid.x + (DTid.y * screenDimensions.x));
    
    //Calculate world position of pixel
    float depth = DepthBuffer[DTid.xy];
    const int iDepth = asint(depth);
    if (iDepth == 0)
    {
        VisualizationBuffer[DTid.xy] = float4(0.f, 0.f, 0.f, 1.f);
        return;
    }
    float3 pixelWorldPosition = DepthToWorldPosition(depth, DTid.xy, screenDimensions, ViewProjectionInverseMatrix).xyz;
    
    //Get LODs - not constant to support DistanceLOD mode
    uint2 lods = LODBuffer[index];

    //Shepard interpolation - 3D Lookup & linear interpolation between LODs (if necessary)
    float aoValue = 0.f;
    if (LODMode == 0) //FixedLOD
    {
        aoValue = WorldSpaceShepardInterpolationSingleLevel(DiscreteCellSize, lods.x, pixelWorldPosition, MaxLevels);
    }
    else if (LODMode == 1) //DistanceLOD
    {
        float percentageLOD = 0.f;
        const float3 cameraPosition = ViewInverseMatrix._m03_m13_m23;
        SimpleDistanceLOD(MaxLevels, ViewVector.xyz, pixelWorldPosition, cameraPosition, lods.x, lods.y, percentageLOD);
        
        aoValue = WorldSpaceShepardInterpolationSingleLevel(DiscreteCellSize, lods.x, pixelWorldPosition, MaxLevels);
        float aoPrevLODValue = WorldSpaceShepardInterpolationSingleLevel(DiscreteCellSize, lods.y, pixelWorldPosition, MaxLevels);
        aoValue = (aoValue * (1 - percentageLOD)) + (aoPrevLODValue * percentageLOD);
    }
    else if (LODMode == 2) //MinMaxLOD
    {
        aoValue = WorldSpaceShepardInterpolationMultipleLevels(DiscreteCellSize, lods, pixelWorldPosition, MaxLevels);
    }

    //Write to the output buffer
    VisualizationBuffer[DTid.xy] = float4(aoValue, aoValue, aoValue, 1.f);
}