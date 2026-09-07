#define D2D_ENTRY main
#include <d2d1effecthelpers.hlsli>

float scale;
float smallInverseSum;
float largeInverseSum;
float padding;
float4 inputBounds;

float3 decodeSrgb(float3 c) { return float3(c.r <= .04045 ? c.r / 12.92 : pow((c.r + .055) / 1.055, 2.4), c.g <= .04045 ? c.g / 12.92 : pow((c.g + .055) / 1.055, 2.4), c.b <= .04045 ? c.b / 12.92 : pow((c.b + .055) / 1.055, 2.4)); }
// Preserve the existing detector's perceptual OKLab L domain.
float luminance(float3 c)
{
    float3 lms = float3(dot(c, float3(.4122214708, .5363325363, .0514459929)),
        dot(c, float3(.2119034982, .6806995451, .1073969566)),
        dot(c, float3(.0883024619, .2817188376, .6299787005)));
    return saturate(dot(pow(max(lms, 0), 1.0 / 3.0), float3(.2104542553, .793617785, -.0040720468)));
}
D2D_PS_ENTRY(main)
{
    float2 p = D2DGetScenePosition().xy;
    float largeScale = 1.6 * scale;
    int smallRadius = (int)ceil(3 * scale);
    int largeRadius = (int)ceil(3 * largeScale);
    float4 sum = 0;
    [loop] for (int x = -largeRadius; x <= largeRadius; ++x)
    {
        float2 samplePosition = p + float2(x, 0);
        if (any(samplePosition < inputBounds.xy) || any(samplePosition >= inputBounds.zw)) continue;
        // Explicit LOD avoids undefined derivatives in the bounds-dependent loop.
        float4 uv = D2DGetInputCoordinate(0);
        float4 c = InputTexture0.SampleLevel(InputSampler0, uv.xy + uv.zw * float2(x, 0), 0);
        float a = saturate(c.a);
        float l = c.a > 0 ? luminance(decodeSrgb(saturate(c.rgb / c.a))) : 0;
        float ws = abs(x) <= smallRadius ? exp(-.5 * x * x / (scale * scale)) * smallInverseSum : 0;
        float wb = exp(-.5 * x * x / (largeScale * largeScale)) * largeInverseSum;
        // Carry numerator AND denominator through both axes.
        sum += float4(l * a * ws, l * a * wb, a * ws, a * wb);
    }
    return sum;
}
