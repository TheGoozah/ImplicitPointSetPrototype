#include "HashFunctions.hlsli"
#include "SharedUtilities.hlsli"

//Voxel Connectivity Data
static const int3 NeighbourOffsets[27] =
{
    int3(0, 0, 0), //No Connectivity runs until here, which is 1 element
   
    int3(-1, 0, 0),
    int3(1, 0, 0),
    int3(0, 1, 0),
    int3(0, -1, 0),
    int3(0, 0, -1),
    int3(0, 0, 1), //6 Connectivity runs until here, which is 7 elements
    
    int3(-1, 1, 0),
    int3(-1, -1, 0),
    int3(1, -1, 0),
    int3(1, 1, 0),
    
    int3(-1, 0, 1),
    int3(1, 0, 1),
    int3(0, 1, 1),
    int3(0, -1, 1),
    
    int3(-1, 0, -1),
    int3(1, 0, -1),
    int3(0, 1, -1),
    int3(0, -1, -1), //18 Connectivity runs unitl here, which is 19 elements
    
    int3(-1, 1, 1),
    int3(-1, -1, 1),
    int3(1, -1, 1),
    int3(1, 1, 1),
    int3(-1, 1, -1),
    int3(-1, -1, -1),
    int3(1, -1, -1),
    int3(1, 1, -1) //26 Connectivity runs unitl here, which is 27 elements
};

//Struct used to cache closest points - not necessarly what you would store in the final buffers
struct ClosestPointSample
{
    uint seed;
    float3 sample;
};

//Returns the delta value for the level in normalized range
inline float GetCellDelta(in uint level)
{
    return 1.f / (1 << (level + 1));
}

//Voxel Quadrants are the 8 cells of a single subdivided cube. The pattern is a morton-like pattern.
//Front Layer: FTopLeft(0), FBottomLeft(1), FTopRight(2), FBottomRight(3)
//Back Layer: BTopLeft(4), BBottomLeft(5), BTopRight(6), BBottomRight(7)
//The point or sample must be normalized [0,1] relative within the implicit voxel.
uint GetQuadrant(in float3 normalizedPosition, in uint level)
{
    //Remap normalized position to level
    const float cellDelta = GetCellDelta(level);
    float3 np = normalizedPosition / (cellDelta * 2.f);
    np = np - (uint3)np;
    
    //See which components have values < or >= 0.5
    const uint3 npf = uint3(np * 2); //Make "fixed" -> (<0.5 == 0, >= 0.5 == 1)
    const uint lr = npf.x << 1; //Left vs Right: xx0x
    const uint tb = npf.y; //Top vs Bottom: xxx0
    const uint fb = npf.z << 2; //Front vs Back: x0xx
    
    return (lr | tb | fb);
}

//Returns an int3 where each component represents the offset direction
//E.g. (1,1,0) == RightBottomFront Voxel Offsets - TopLeft corners are the origin
int3 OffsetVectorFromQuadrant(in uint quadrant)
{
    return int3(
        (quadrant & 2) >> 1,    //Left vs Right: xx0x
        (quadrant & 1),         //Top vs Bottom: xxx0
        (quadrant & 4) >> 2);   //Front vs Back: x0xx
}

//Returns the discrete, yet normalized value within the implicit voxel, from the passed discrete, normalized, point.
//E.g. 2D ==> (0.625, 0.125) is a discrete normalized position in level 2 (delta is 0.125).
//In level 1 it resides in the TopLeft corner of that levels discrete position. That position (which gets returned) is (0.75, 0.25).
//In level 0 it resides in the TopRight corner of that levels discrete position. That position is (0.5, 0.5)
float3 GetDiscretePositionInLevel(in float3 currentDiscretePosition, in uint levelToMap, in uint quadrantInLevelToMap)
{
    const float wantedLevelCellDelta = GetCellDelta(levelToMap);
    const float3 offsets = (float3) OffsetVectorFromQuadrant(quadrantInLevelToMap);
   
    const float3 op = abs(currentDiscretePosition - wantedLevelCellDelta) % wantedLevelCellDelta; //Map + overflow safety
    const float3 d = op * ((offsets * (-2)) + 1); //Signed offset based on factor
    return (currentDiscretePosition + d);
}

//Generate a sample in the voxel within a certain quadrant.
//The sample is a normalized value within the voxel itself.
float3 GenerateSampleInVoxelQuadrant(in uint seed, in uint level, in uint quadrant)
{
    //Generate normalized sample, based on seed and offset based on quadrant (progressive required)
    const float3 sample = hash3(seed + quadrant);
    const float3 offsets = (float3)OffsetVectorFromQuadrant(quadrant);
    const float cellDelta = 0.5f;
    //Return remapped to level
    return (offsets * cellDelta) + (sample / (1.f / cellDelta));
}

//Get seed value based on discretized position
uint GetSeed(in float3 discretePos, in uint fixedPointFractionalBits)
{
    //Convert float value to fixed point representation for hash functions
    const uint3 fp = ToFixedPoint(discretePos, fixedPointFractionalBits);
    return pcg_nested(fp); //wang_hash_nested(fp);
}

//Get seed value based on discretized position - storing the signs from every axis in the most significant bits
uint GetSeedWithSignedBits(in float3 discretePos, in uint fixedPointFractionalBits)
{
    //Convert float value to fixed point representation for hash functions
    //Putting the signs of all 3 axis in the 3 most significant bits
    const uint fracScale = 1 << fixedPointFractionalBits;
    
    const float3 rawFractionalPart = frac(discretePos);
    const float3 rawIntegerPart = trunc(discretePos);
    
    const uint3 fp = uint3(rawFractionalPart * fracScale); //fractional part
    const uint3 ip = (uint3(abs(rawIntegerPart)) << fixedPointFractionalBits) & 0x1FFFFFFF; //shifted integer part
    const uint xp = clamp(sign(rawIntegerPart.x) * -1, 0, 1) << 31;
    const uint yp = clamp(sign(rawIntegerPart.y) * -1, 0, 1) << 30;
    const uint zp = clamp(sign(rawIntegerPart.z) * -1, 0, 1) << 29;
    
    const uint3 fic = fp | ip;
    const uint3 inp = uint3(xp | fic.x, yp | fic.y, zp | fic.z);
    return pcg_nested(inp);
}

//Map to top-left-back vertex, taking into account negative space: 
//+x = right, +y = up, +z = out of screen
float3 Discretize(in float3 position, in float cellSize)
{
    float3 discretePosition = int3(position / cellSize) * cellSize;
    discretePosition -= (asuint(position) >> 31) * cellSize; //If negative value, map to topleft in negative space!
    return discretePosition;
}

void GetClosestSample(in float3 pos, in float3 absSample, in uint seed, inout float closestSqrDistance, inout ClosestPointSample closestSample)
{
    const float3 diff = pos - absSample;
    const float sqrDistance = dot(diff, diff);
    if (sqrDistance < closestSqrDistance)
    {
        closestSqrDistance = sqrDistance;
        closestSample.sample = absSample;
        closestSample.seed = seed;
    }
}

void GetClosestSampleTriplet(in float3 pos, in float3 absSample, in uint seed, inout ClosestPointSample closestSamples[3])
{
    //Calculate the square distance
    const float3 diff = pos - absSample;
    const float currentSqrDistance = dot(diff, diff);
    
    //Find if closer than one of the three cached distances, and if so, which one
    int foundIndex = -1;
    float largestFoundDistance = 0.f;
    [unroll]
    for (uint i = 0; i < 3; ++i)
    {
        const float3 cachedDiff = pos - closestSamples[i].sample;
        const float cachedSqrDistance = dot(cachedDiff, cachedDiff);
        if (currentSqrDistance < cachedSqrDistance && largestFoundDistance < cachedSqrDistance)
        {
            foundIndex = i;
            largestFoundDistance = cachedSqrDistance;
        }
    }
    
    //Replace the one with the largest distance with this new closest sample
    if(foundIndex != -1)
    {
        closestSamples[foundIndex].sample = absSample;
        closestSamples[foundIndex].seed = seed;
    }
}

uint GetIndexByProbability(in float3 position, in ClosestPointSample closestSamples[3])
{  
    //Create probability values based on "area" (actually just distances)
    const float3 distances = float3(
        dot(position - closestSamples[0].sample, position - closestSamples[0].sample),
        dot(position - closestSamples[1].sample, position - closestSamples[1].sample),
        dot(position - closestSamples[2].sample, position - closestSamples[2].sample));
    float3 areas = float3(
        distances[1] * distances[2],
        distances[0] * distances[2],
        distances[0] * distances[1]);
    const float totalArea = areas.x + areas.y + areas.z;
    areas /= totalArea; //normalize

    //Get random normalized value, seed based on position
    const float randomValue = randomNormalizedFloat(GetSeed(position, 15));
    
    //Get index based on probability
    return saturate(uint(randomValue / areas.x)) + saturate(uint(randomValue / (areas.x + areas.y)));
}

ClosestPointSample GetGeneratedSample(in float3 position, in uint discreteCellSize, in uint level, in uint voxelConnectivity, in uint maxLevels)
{
    //Testing different technique
    const bool useRandomPoints = true;
    const bool useProbability = true;
    
    const float FLT_MAX = 3.402823466e+38f;  
    //For single sample use - remove if not used (keep for now for debugging)
    float closestSampleSqrtDistance = FLT_MAX;
    ClosestPointSample closestSample;
    closestSample.sample = float3(FLT_MAX, FLT_MAX, FLT_MAX);
    closestSample.seed = 0;
    //Triplet use
    ClosestPointSample closestSamples[3];
    [unroll]
    for (int i = 0; i < 3; ++i)
    {
        closestSamples[i].sample = float3(FLT_MAX, FLT_MAX, FLT_MAX);
        closestSamples[i].seed = 0;
    }
    
    //Discretize position based on discrete cell size and current level
    const float deltaOnLevel = 1.f / (1 << level);
    const float cellSizeBasedOnLevel = discreteCellSize * deltaOnLevel;
    const float3 discretePosition = Discretize(position, cellSizeBasedOnLevel);
    
    //For that discrete position, interate over all the neighbouring voxels(based on the size of cell size)
    for (uint v = 0; v < (voxelConnectivity + 1); ++v)
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
        
        //Non-random technique = fixed quantization
        if (!useRandomPoints)
        {
            if(!useProbability)
            {
                ClosestPointSample fixedSample = (ClosestPointSample) 0;
                fixedSample.seed = absoluteSeed;
                fixedSample.sample = acp;
                return fixedSample;
            }
            else
                GetClosestSampleTriplet(position, acp, absoluteSeed, closestSamples);
        }
        //Random point technique, using random implicit points
        else
        {
            [unroll]
            for (uint i = 0; i < 8; ++i)
            {
                uint currentSeed = absoluteSeed + i;
                uint currentLevel = level;
            
                const float3 normalizedSample = GenerateSampleInVoxelQuadrant(absoluteSeed, level, i);
                float3 absoluteSample = neighbourPosition + (normalizedSample * cellSizeBasedOnLevel);
        
                //See if closest
                if(!useProbability)
                    GetClosestSample(position, absoluteSample, currentSeed, closestSampleSqrtDistance, closestSample);
                else           
                    GetClosestSampleTriplet(position, absoluteSample, currentSeed, closestSamples);
            }
        }
    }
    
    //Triplet code
    if(useProbability)
    {
        const uint index = GetIndexByProbability(position, closestSamples);
        return closestSamples[index];
    }
    else
        return closestSample;
}
