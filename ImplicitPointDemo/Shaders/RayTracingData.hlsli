//SRV
RaytracingAccelerationStructure SceneBVH      : register(t0);
Texture2D<float>  DepthBuffer                 : register(t1);
Texture2D<float4> NormalBuffer                : register(t2);

//UAV
RWTexture2D<float4> OutputBuffer              : register(u1); 

//CB
cbuffer RayTracingCB                          : register(b0)
{
    float4x4 ViewProjectionInverseMatrix;
    uint FrameCount;
    bool QuarterSPP;
}

struct RayPayload
{
    float rayHitT;
};