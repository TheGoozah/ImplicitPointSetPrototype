#include "RayTracingData.hlsli"
#include "RayTracingUtils.hlsli"
#include "SharedUtilities.hlsli"

float ShootRayInHemisphere(in float3 worldPosition, in float3 worldDirection)
{
    //Ray Structure
    RayDesc ray;
    ray.Origin = worldPosition;
    ray.Direction = worldDirection;
    ray.TMin = 0.01f;
    ray.TMax = 25.0f; //AO Radius
    
    //Initialize ray payload (a per-ray, user defined structure, passed along shaders)
    RayPayload payLoad = { ray.TMax };
    
    //Trace a ray - FLAGS: https://docs.microsoft.com/en-us/windows/win32/direct3d12/ray_flag
    TraceRay(SceneBVH,
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, //Ray Flags
        0xFF, //Instance inclusion mask - 0xFF = nothing discarded
        0, //Hit Group Index
        1, //Number Hit Groups
        0, //Miss Program Index
        ray, //Ray Desc
        payLoad); //RayData/PayLoad
    
    return saturate(payLoad.rayHitT / ray.TMax);
}

[shader("raygeneration")]
void RayGen()
{
    const uint2 screenDimensions = uint2(1920, 1080);
      
    //Test if necessary to cast (using depth buffer)
    const float depth = DepthBuffer[DispatchRaysIndex().xy];
    const int iDepth = asint(depth);
    if (iDepth == 0)
    {
        OutputBuffer[DispatchRaysIndex().xy] = float4(0.f, 0.f, 0.f, 1.f);
        return;
    }
    
    //Get world normal and world position
    const float3 worldNormal = NormalBuffer[DispatchRaysIndex().xy].xyz;
    const float3 worldPosition = DepthToWorldPosition(depth, DispatchRaysIndex().xy, screenDimensions, ViewProjectionInverseMatrix).xyz;
    uint randSeed = InitRand(DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x, FrameCount, 16);
    
    const uint amountRays = 1;
    float ambientOcclusion = 0.f;
    for (int i = 0; i < amountRays; ++i)
    {
        //Random sample direction
        float3 worldDirection = GetCosHemisphereSample(randSeed, worldNormal);
        ambientOcclusion += ShootRayInHemisphere(worldPosition + (worldDirection * 0.01f), worldDirection);
    }
    
    ambientOcclusion /= float(amountRays);
    OutputBuffer[DispatchRaysIndex().xy] = float4(ambientOcclusion, ambientOcclusion, ambientOcclusion, 1.f);
}