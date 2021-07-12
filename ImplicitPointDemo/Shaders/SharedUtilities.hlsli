#ifndef __SHARED_UTITLITIES__
#define __SHARED_UTITLITIES__

//Get world position from depth buffer value
float4 DepthToWorldPosition(in float depth, in uint2 screenCoord, in uint2 screenDimensions, in float4x4 viewProjectionInverse)
{
    float2 normalizedScreenCoord = screenCoord / (float2) screenDimensions;
    float2 ndcXY = float2(normalizedScreenCoord.x, 1.f - normalizedScreenCoord.y) * 2.f - 1.f;
    float4 ndc = float4(ndcXY, depth, 1.f);
    float4 worldPos = mul(viewProjectionInverse, ndc);
    return worldPos / worldPos.w;
}

//Floating point value to (unsigned) fixed point, based on fractionalBitCount
inline uint ToFixedPoint(in float value, in uint fractionalBitCount)
{
    uint frac = 1 << fractionalBitCount;
    return uint(abs(value) * frac);
}

inline uint3 ToFixedPoint(in float3 value, in uint fractionalBitCount)
{
    uint frac = 1 << fractionalBitCount;
    return uint3(abs(value) * frac);
}

//Fixed point to floating point value, based on fractionalBitCount
inline float FromFixedPoint(in uint value, in uint fractionalBitCount)
{
    uint frac = 1 << fractionalBitCount;
    return float(value) / frac;
}

inline float3 FromFixedPoint(in uint3 value, in uint fractionalBitCount)
{
    uint frac = 1 << fractionalBitCount;
    return float3(value) / frac;
}
#endif