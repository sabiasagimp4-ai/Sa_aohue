#define D2D_ENTRY main
#include <d2d1effecthelpers.hlsli>
#include "PointComponents.hlsli"

float pointSize;
float3 padding;
float4 inputBounds;

D2D_PS_ENTRY(main)
{
    float4 center = D2DGetInput(0);
    if (pointSize < .5 || center.b <= 0 || center.a <= 0) return center;
    float2 p = D2DGetScenePosition().xy;
    float4 uv = D2DGetInputCoordinate(0);
    uint occupied[7];
    [unroll] for (int y = 0; y < 7; ++y)
    {
        occupied[y] = 0u;
        [unroll] for (int x = 0; x < 7; ++x)
        {
            float2 offset = float2(x - 3, y - 3);
            float2 q = p + offset;
            if (any(q < inputBounds.xy) || any(q >= inputBounds.zw)) continue;
            float4 sampleValue = InputTexture0.SampleLevel(InputSampler0, uv.xy + uv.zw * offset, 0);
            if (sampleValue.b > 0 && sampleValue.a > 0) occupied[y] |= 1u << x;
        }
    }
    if (IsSmallComponent(occupied, (int)pointSize)) center.rgb = 0;
    return center;
}
