#include "SharedData.hlsli"

SampleGenerationOutput main(SampleGenerationInput vsInput)
{
    SampleGenerationOutput vsOutput = (SampleGenerationOutput) 0;
	vsOutput.position = float4(vsInput.position, 1.f);
	return vsOutput;
}