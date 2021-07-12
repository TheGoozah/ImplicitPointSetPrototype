#ifndef __HASH_FUNCTION__
#define __HASH_FUNCTION__

//--------- SEED HASH FUNCTIONS ---------
//Source: https://gist.github.com/fboldog/a76648e1580b88ca9957f60b20edf49c
uint wang_hash(uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

//Based on http://jcgt.org/published/0009/03/02/
//Source: https://www.shadertoy.com/view/XlGcRh
//Same sources used, unless stated otherwise + nested & linear combinations based on paper/source
uint linearCombine(uint3 p)
{
    return 19 * p.x + 47 * p.y + 101 * p.z + 131;
}

uint xxhash32(uint3 p)
{
    const uint PRIME32_2 = 2246822519, PRIME32_3 = 3266489917;
    const uint PRIME32_4 = 668265263, PRIME32_5 = 374761393;
    uint h32 = p.z + PRIME32_5 + p.x * PRIME32_3;
    h32 = PRIME32_4 * ((h32 << 17) | (h32 >> (32 - 17)));
    h32 += p.y * PRIME32_3;
    h32 = PRIME32_4 * ((h32 << 17) | (h32 >> (32 - 17)));
    h32 = PRIME32_2 * (h32 ^ (h32 >> 15));
    h32 = PRIME32_3 * (h32 ^ (h32 >> 13));
    return h32 ^ (h32 >> 16);
}

uint wang_hash_linear(uint3 p)
{
    return wang_hash(linearCombine(p));
}

uint wang_hash_nested(uint3 p)
{
    return wang_hash(p.x + wang_hash(p.y + wang_hash(p.z)));
}

uint pcg(uint v)
{
    uint state = v * 747796405 + 2891336453;
    uint word = ((state >> ((state >> 28) + 4)) ^ state) * 277803737;
    return (word >> 22) ^ word;
}

uint pcg_nested(uint3 p)
{
    return pcg(p.x + pcg(p.y + pcg(p.z)));
}

uint xorshift32(uint v)
{
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    return v;
}

uint xorshift32_linear(uint3 p)
{
    return xorshift32(linearCombine(p));
}

//--------- RANDOM SAMPLE FUNCTIONS ---------
//https://www.shadertoy.com/view/llGSzw
float3 hash3(uint n)
{
    // integer hash copied from Hugo Elias
    n = (n << 13) ^ n;
    n = n * (n * n * 15731U + 789221) + 1376312589;
    uint3 k = n * uint3(n, n * 16807, n * 48271);
    uint3 v = k & uint(0x7fffffff);
    return float3(v) / float(0x7fffffff);
}

//Return random float [0,1] range
inline float randomNormalizedFloat(uint seed)
{
    return xorshift32(seed) * 2.3283064365387e-10f;
}

//https://www.ronja-tutorials.com/post/024-white-noise/
float rand3dTo1d(float3 value, float3 dotDir = float3(12.9898, 78.233, 37.719))
{
    //make value smaller to avoid artefacts
    float3 smallValue = sin(value);
    //get scalar value from 3d vector
    float random = dot(smallValue, dotDir);
    //make value more random by making it bigger and then taking teh factional part
    random = frac(sin(random) * 143758.5453);
    return random;
}

float3 rand3dTo3d(float3 value)
{
    return float3(
        rand3dTo1d(value, float3(12.989, 78.233, 37.719)),
        rand3dTo1d(value, float3(39.346, 11.135, 83.155)),
        rand3dTo1d(value, float3(73.156, 52.235, 09.151))
    );
}
#endif