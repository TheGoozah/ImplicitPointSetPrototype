#include "SharedData.hlsli"

void CreateBillboardVertex(inout TriangleStream<PointOutput> triStream, float4 position, float4 offset)
{
    PointOutput output = (PointOutput) 0;
    output.position = mul(WorldViewProjectionMatrix, position + mul(ViewInverseMatrix, offset));
    triStream.Append(output);
}

void CreateBillboardQuad(inout TriangleStream<PointOutput> triStream, float4 position)
{
    float delta = 0.1f;
    CreateBillboardVertex(triStream, position, float4(-delta, -delta, 0.f, 0.f));
    CreateBillboardVertex(triStream, position, float4(delta, -delta, 0.f, 0.f));
    CreateBillboardVertex(triStream, position, float4(-delta, delta, 0.f, 0.f));
    CreateBillboardVertex(triStream, position, float4(delta, delta, 0.f, 0.f));
    triStream.RestartStrip();
}

[maxvertexcount(8)]
void main(point PointGSInput p[1], inout TriangleStream<PointOutput> triStream)
{  
    //Create billboard for this sample
    if(p[0].seed != 0)
    {
        CreateBillboardQuad(triStream, p[0].position);
    }
}