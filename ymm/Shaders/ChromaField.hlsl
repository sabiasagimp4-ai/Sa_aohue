#define D2D_REQUIRES_SCENE_POSITION
#define D2D_ENTRY main
#include <d2d1effecthelpers.hlsli>

float3 decodeSrgb(float3 c)
{
    return float3(
        c.r <= .04045 ? c.r / 12.92 : pow((c.r + .055) / 1.055, 2.4),
        c.g <= .04045 ? c.g / 12.92 : pow((c.g + .055) / 1.055, 2.4),
        c.b <= .04045 ? c.b / 12.92 : pow((c.b + .055) / 1.055, 2.4));
}

float3 toLab(float3 c)
{
    float l = pow(max(dot(c, float3(.4122214708, .5363325363, .0514459929)), 0), 1.0 / 3.0);
    float m = pow(max(dot(c, float3(.2119034982, .6806995451, .1073969566)), 0), 1.0 / 3.0);
    float s = pow(max(dot(c, float3(.0883024619, .2817188376, .6299787005)), 0), 1.0 / 3.0);
    return float3(
        .2104542553 * l + .793617785 * m - .0040720468 * s,
        1.9779984951 * l - 2.428592205 * m + .4505937099 * s,
        .0259040371 * l + .7827717662 * m - .808675766 * s);
}

D2D_PS_ENTRY(main)
{
    float4 source = D2DSampleInputAtPosition(0, D2DGetScenePosition().xy);
    float alpha = saturate(source.a);
    if (alpha <= 0) return float4(0, 0, 0, 1);

    // YMM supplies premultiplied input.  The RGB-to-OKLab conversion is done
    // on straight sRGB, matching Composite.hlsl and the CPU reference.
    float3 rgb = saturate(source.rgb / alpha);
    float3 lab = toLab(decodeSrgb(rgb));
    return float4(lab.y * alpha, lab.z * alpha, alpha, 1);
}
