# Caching Shading Information in World Space using Implicit Progressive Low Discrepancy Point Sets
This is the supplementary material for the publication "Caching Shading Information in World Space using Implicit Progressive Low Discrepancy Point Sets". The publication can be acquired via the following link: http://matthieudelaere.com/project_caching.html

The prototype, which was built using Microsoft's MiniEngine framework (https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/MiniEngine), **requires** a GPU which **supports hardware accelerated raytracing** as it relies on **DXR**. 

## Interesting files
- [Implicit Point Generation](ImplicitPointDemo/Shaders/SampleGenerationFunctions.hlsli): the essence of the proposed method. Using 3D noise functions, implicit points are generated and a closest point, for every visibile world position, get randomly selected using barycentric coordinate-based probability. This point finally gets hashed and its key is used as the entry within a hashtable, which is used to cache the shading information.
- [Shading Information Accumulation](ImplicitPointDemo/Shaders/AccumulationPass.hlsl): a simple accumulation where diferent world space positions atomically add their results to a temporary 2D hashtable. This structure is then used in the next step (see next bullet point).
- [Shading Information Merge](ImplicitPointDemo/Shaders/WorldHashTable.hlsl): after accumulating shading information from several world space positions, a final merge with the persistent data structure must be performed. To avoid data overflow, a constant rescaling is performed.
- [Image Reconstruction in World Space](ImplicitPointDemo/Shaders/FinalVisualizationPass.hlsl): after storing the shading information in the persistent data structure, the final image gets reconstructed, in world space, using a modified Shepard interpolation. Multiple filtering modes are supported based on the implemented LOD techniques.
