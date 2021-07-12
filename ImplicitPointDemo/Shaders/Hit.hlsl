#include "RayTracingData.hlsli"

[shader("closesthit")]
void Hit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.rayHitT = RayTCurrent();
}