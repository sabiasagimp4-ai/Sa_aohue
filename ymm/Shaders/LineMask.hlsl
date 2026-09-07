#define D2D_ENTRY main
#include <d2d1effecthelpers.hlsli>

float threshold;
float invertLines;
float isLinear;
float padding;

float3 unpremultiply(float4 c) { return c.a > 0 ? saturate(c.rgb / c.a) : 0; }
float3 decodeSrgb(float3 c) { return float3(c.r <= .04045 ? c.r / 12.92 : pow((c.r + .055) / 1.055, 2.4), c.g <= .04045 ? c.g / 12.92 : pow((c.g + .055) / 1.055, 2.4), c.b <= .04045 ? c.b / 12.92 : pow((c.b + .055) / 1.055, 2.4)); }
float luminance(float3 c) { float l = pow(max(dot(c, float3(.4122214708, .5363325363, .0514459929)), 0), 1.0 / 3.0); float m = pow(max(dot(c, float3(.2119034982, .6806995451, .1073969566)), 0), 1.0 / 3.0); float s = pow(max(dot(c, float3(.0883024619, .2817188376, .6299787005)), 0), 1.0 / 3.0); return saturate(.2104542553 * l + .793617785 * m - .0040720468 * s); }
float sampleL(float2 p) { float3 c = unpremultiply(D2DSampleInputAtPosition(0, p)); return luminance(isLinear > .5 ? c : decodeSrgb(c)); }
float disc(float2 p, float radius)
{
    float sum = 0, weight = 0;
    [unroll] for (int y = -2; y <= 2; ++y) [unroll] for (int x = -2; x <= 2; ++x)
    {
        float n2 = (x * x + y * y) / (radius * radius);
        if (n2 <= 1) { float a = saturate((1 - sqrt(n2)) * radius) * saturate(D2DSampleInputAtPosition(0, p + float2(x, y)).a); if (a > 0) { sum += sampleL(p + float2(x, y)) * a; weight += a; } }
    }
    return weight > 0 ? sum / weight : sampleL(p);
}
D2D_PS_ENTRY(main)
{
    float2 p = D2DGetScenePosition().xy;
    float4 source = D2DSampleInputAtPosition(0, p);
    if (source.a <= 0) return 0;
    float dog = disc(p, 1.0) - disc(p, 1.6);
    float t = saturate(threshold) * .03;
    float edgeMask = invertLines > .5 ? dog > t : dog < -t;
    return float4(edgeMask * source.a, edgeMask * source.a, edgeMask * source.a, source.a);
}
