#include "SharedData.hlsli"
#include "SampleGenerationFunctions.hlsli"
#include "LODFunctions.hlsli"

void main(SampleGenerationOutput vsOutput)
{
    //For testing different techniques
    const bool useJittering = false;
    
    //Indexing
    const uint2 screenDimensions = uint2(1920, 1080);
    const uint index = uint(vsOutput.position.x) + (uint(vsOutput.position.y) * screenDimensions.x);
    const uint2 index2D = uint2(index % screenDimensions.x, index / screenDimensions.x);
    
    //Sample from depth and get world position
    const float depth = DepthBuffer[index2D]; //instead of load, this gives a read-only variable!
    //Test discard
    const int iDepth = asint(depth);
    if (iDepth == 0)
    {
        PointSampleBuffer[index].seed = 0;
        PointSampleBuffer[index].prevSeed = 0;
        PointSampleBuffer[index].sample = float3(0.f, 0.f, 0.f);
        return;
    }
  
    //Get world position
    float3 worldPosition = DepthToWorldPosition(depth, index2D, screenDimensions, ViewProjectionInverseMatrix).xyz;
    
    //Jittered sampling (if enabled)
    if (useJittering)
    {
        const float radius = 3.0f;
        const float3 rand = rand3dTo3d(worldPosition);
        float3 ds = float3(rand.xy, 0) * radius;
        const float3x3 localAxis = float3x3(TangentBuffer[index2D].xyz, BitangentBuffer[index2D].xyz, NormalBuffer[index2D].xyz);
        ds = mul(ds, localAxis); //Move to correct tangent space
        worldPosition += ds;
    }
    
    //Determine LOD based on mode (0 = Fixed, 1 = Distance, 2 = MinMax)
    uint lodLevel = 0;
    uint prevLodLevel = 0;
    //--- FIXED LOD ---
    if (LODMode == 0)
    {
        lodLevel = clamp(Level, 0, MaxLevels);
        prevLodLevel = lodLevel;
    }
    //--- DISTANCE LOD ---
    else if(LODMode == 1)
    {
        float3 cameraPosition = ViewInverseMatrix._m03_m13_m23;
        float percentageLOD = 0.f; //Not used here!
        SimpleDistanceLOD(MaxLevels, ViewVector.xyz, worldPosition, cameraPosition, lodLevel, prevLodLevel, percentageLOD);
    }
    //--- MINMAX LOD ---
    else if(LODMode == 2)
    {
        ScreenMinMaxLOD(index2D, screenDimensions, MaxLevels, DepthBuffer, BlurredAOBuffer, lodLevel, prevLodLevel);
    }
    //Store LODs (determined based on different method)
    LODBuffer[index] = uint2(lodLevel, prevLodLevel);
    
    //Generate closest sample and seed based on LODs
    const ClosestPointSample closestSample = GetGeneratedSample(worldPosition.xyz, CellSize, lodLevel, VoxelConnectivity, MaxLevels);
    ClosestPointSample closestSamplePrevLOD = (ClosestPointSample) 0;
    if(prevLodLevel != lodLevel) //Need to prevent double addition
        closestSamplePrevLOD = GetGeneratedSample(worldPosition.xyz, CellSize, prevLodLevel, VoxelConnectivity, MaxLevels);
    
    //Store in buffer
    PointSampleBuffer[index].seed = closestSample.seed;
    PointSampleBuffer[index].prevSeed = closestSamplePrevLOD.seed;
    PointSampleBuffer[index].sample = closestSample.sample;
}