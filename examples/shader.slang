struct PushConstants
{
    uint width;
    uint height;
    uint input_image_index;
    uint sampler_index;
    uint output_image_index;
};

[[vk::push_constant]]
PushConstants pc;

[[vk::binding(0, 0)]]
Texture2D<float4> input_images[];

[[vk::binding(1, 0)]]
SamplerState samplers[];

[[vk::binding(2, 0)]]
RWTexture2D<float4> output_images[];

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_thread_id.xy;
    if (pixel.x >= pc.width || pixel.y >= pc.height)
        return;

    float2 uv = (float2(pixel) + 0.5) / float2(pc.width, pc.height);

    uint input_index = NonUniformResourceIndex(pc.input_image_index);
    uint sampler_index = NonUniformResourceIndex(pc.sampler_index);
    uint output_index = NonUniformResourceIndex(pc.output_image_index);

    float4 src = input_images[input_index].SampleLevel(samplers[sampler_index], uv, 0.0);
    output_images[output_index][pixel] = src;
}