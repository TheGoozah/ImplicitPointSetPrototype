#ifndef __LOD_FUNCTIONS__
#define __LOD_FUNCTIONS__

//Fixed distance based LOD
void SimpleDistanceLOD(in uint maxAmountLevels, in float3 view, in float3 worldPos, in float3 cameraPos, 
    out uint lod, out uint prevLod, out float percentageBetweenLOD)
{
    const uint numLOD = maxAmountLevels + 1;
    const float lodTransitionInterval = 300.f;
    const float z = max(dot(view, worldPos - cameraPos), 0.f);
    const float invLod = float(max(numLOD - (z / lodTransitionInterval), 0));
    const float currentLOD = clamp(invLod, 0, numLOD);
    
    const uint l = ceil(currentLOD);
    if (l == numLOD) //Over maxAmountLevels
    {
        lod = maxAmountLevels;
        prevLod = maxAmountLevels;
        percentageBetweenLOD = 1.f;
    }
    else //Between 0 and maxAmountLevels + 1
    {
        lod = l;
        prevLod = l - 1;
        percentageBetweenLOD = (uint(currentLOD) + 1) - currentLOD; //Frac difference, linear norm "dist" to next LOD
    }
}

//Screen MinMax Based LOD
void ScreenMinMaxLOD(in uint2 index2D, in uint2 screenDimensions, in uint maxAmountLevels, 
    in Texture2D<float> depthBuffer, in Texture2D<float4> aoBuffer, out uint lod, out uint prevLod)
{
    //Start LOD and parameters
    uint l = maxAmountLevels;
    int kernelSize = 3;
    const uint amountElements = screenDimensions.x * screenDimensions.y;
    
    //Start with base kernel
    float2 lhLOD = float2(1, 0); //current minmax
    for (int yy = -kernelSize; yy <= kernelSize; ++yy)
    {
        for (int xx = -kernelSize; xx <= kernelSize; ++xx)
        {
                //Get wanted index based on kernel
            int2 sampleDTid = index2D + int2(xx, yy);
            int sampleIndex = int(sampleDTid.x + (sampleDTid.y * screenDimensions.x));
                
                //Range check
            if (sampleIndex >= amountElements || sampleIndex < 0)
                continue;
            
                //See if valid pixel
            const float depth = depthBuffer[sampleDTid];
            const int iDepth = asint(depth);
            if (iDepth == 0)
                continue;
            
            //Read AO, and determine min-max
            float ao = aoBuffer[sampleDTid].x;
            lhLOD.x = min(lhLOD.x, ao);
            lhLOD.y = max(lhLOD.y, ao);
        }
    }
    ++kernelSize;
    
    //Iterate until a wanted LOD is found - only checking new pixels at edges
    while (l > 0)
    {
        //Top & Bottom row
        for (int xx = -kernelSize; xx <= kernelSize; ++xx)
        {
            //----- TOP -----
            int2 sampleDTid = index2D + int2(xx, -kernelSize);
            int sampleIndex = int(sampleDTid.x + (sampleDTid.y * screenDimensions.x));
            
            //Range check
            if (sampleIndex >= amountElements || sampleIndex < 0)
                continue;
            
            //See if valid pixel
            float depth = depthBuffer[sampleDTid];
            int iDepth = asint(depth);
            if (iDepth == 0)
                continue;
            
            //Read AO, and determine min-max
            float ao = aoBuffer[sampleDTid].x;
            lhLOD.x = min(lhLOD.x, ao);
            lhLOD.y = max(lhLOD.y, ao);
            
            //----- BOTTOM -----
            sampleDTid = index2D + int2(xx, +kernelSize);
            sampleIndex = int(sampleDTid.x + (sampleDTid.y * screenDimensions.x));
            
            //Range check
            if (sampleIndex >= amountElements || sampleIndex < 0)
                continue;
            
            //See if valid pixel
            depth = depthBuffer[sampleDTid];
            iDepth = asint(depth);
            if (iDepth == 0)
                continue;
            
            //Read AO, and determine min-max
            ao = aoBuffer[sampleDTid].x;
            lhLOD.x = min(lhLOD.x, ao);
            lhLOD.y = max(lhLOD.y, ao);
        }
        
        //Sides
        for (int yy = -(kernelSize - 1); yy <= (kernelSize - 1); ++yy)
        {
            //----- TOP -----
            int2 sampleDTid = index2D + int2(-kernelSize, yy);
            int sampleIndex = int(sampleDTid.x + (sampleDTid.y * screenDimensions.x));
            
            //Range check
            if (sampleIndex >= amountElements || sampleIndex < 0)
                continue;
            
            //See if valid pixel
            float depth = depthBuffer[sampleDTid];
            int iDepth = asint(depth);
            if (iDepth == 0)
                continue;
            
            //Read AO, and determine min-max
            float ao = aoBuffer[sampleDTid].x;
            lhLOD.x = min(lhLOD.x, ao);
            lhLOD.y = max(lhLOD.y, ao);
            
            //----- BOTTOM -----
            sampleDTid = index2D + int2(+kernelSize, yy);
            sampleIndex = int(sampleDTid.x + (sampleDTid.y * screenDimensions.x));
            
            //Range check
            if (sampleIndex >= amountElements || sampleIndex < 0)
                continue;
            
            //See if valid pixel
            depth = depthBuffer[sampleDTid];
            iDepth = asint(depth);
            if (iDepth == 0)
                continue;
            
            //Read AO, and determine min-max
            ao = aoBuffer[sampleDTid].x;
            lhLOD.x = min(lhLOD.x, ao);
            lhLOD.y = max(lhLOD.y, ao);
        }
        
        //Check difference between minmax, if > threshold, lod found
        float diff = abs(lhLOD.y - lhLOD.x);
        float threshold = 0.00005f;
        if (diff > threshold)
        {
            lod = l;
            prevLod = uint(clamp(int(l) - 1, 0, maxAmountLevels));
            return;
        }
        
        //Go to next LOD and increase kernal size
        --l;
        ++kernelSize;
    }
    
    lod = 0;
    prevLod = 0;
}
#endif