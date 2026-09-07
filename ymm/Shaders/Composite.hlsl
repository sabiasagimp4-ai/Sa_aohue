#define D2D_ENTRY main
#include <d2d1effecthelpers.hlsli>

float amount;
float contrast;
float outputMode;
float isLinear;
float brightness;
float hueShift;
float vibrance;
float padding;

float3 unpremultiply(float4 c) { return c.a > 0 ? c.rgb / c.a : 0; }
float3 decodeSrgb(float3 c) { return float3(c.r <= .04045 ? c.r / 12.92 : pow((c.r + .055) / 1.055, 2.4), c.g <= .04045 ? c.g / 12.92 : pow((c.g + .055) / 1.055, 2.4), c.b <= .04045 ? c.b / 12.92 : pow((c.b + .055) / 1.055, 2.4)); }
float3 encodeSrgb(float3 c) { c = max(c, 0); return float3(c.r <= .0031308 ? c.r * 12.92 : 1.055 * pow(c.r, 1.0 / 2.4) - .055, c.g <= .0031308 ? c.g * 12.92 : 1.055 * pow(c.g, 1.0 / 2.4) - .055, c.b <= .0031308 ? c.b * 12.92 : 1.055 * pow(c.b, 1.0 / 2.4) - .055); }
float3 toLab(float3 c) { float l = pow(max(dot(c, float3(.4122214708, .5363325363, .0514459929)), 0), 1.0 / 3.0); float m = pow(max(dot(c, float3(.2119034982, .6806995451, .1073969566)), 0), 1.0 / 3.0); float s = pow(max(dot(c, float3(.0883024619, .2817188376, .6299787005)), 0), 1.0 / 3.0); return float3(.2104542553 * l + .793617785 * m - .0040720468 * s, 1.9779984951 * l - 2.428592205 * m + .4505937099 * s, .0259040371 * l + .7827717662 * m - .808675766 * s); }
float3 fromLab(float3 c) { float l = c.x + .3963377774 * c.y + .2158037573 * c.z; float m = c.x - .1055613458 * c.y - .0638541728 * c.z; float s = c.x - .0894841775 * c.y - 1.291485548 * c.z; l *= l * l; m *= m * m; s *= s * s; return float3(4.0767416621 * l - 3.3077115913 * m + .2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - .3413193965 * s, -.0041960863 * l - .7034186147 * m + 1.707614701 * s); }
bool inGamut(float3 c) { return all(c >= 0) && all(c <= 1); }
float3 adjustColor(float3 rgb, float mask)
{
    if (!inGamut(rgb) || mask <= 0) return rgb;
    float3 lab = toLab(isLinear > .5 ? rgb : decodeSrgb(max(rgb, 0)));
    lab.x = saturate(lab.x * lerp(1, brightness, mask));
    float angle = radians(hueShift * mask);
    float sine, cosine;
    sincos(angle, sine, cosine);
    float2 ab = float2(cosine * lab.y - sine * lab.z, sine * lab.y + cosine * lab.z);
    float vibranceWeight = lerp(1, 1 - saturate(length(ab) / .4), vibrance);
    float gain = max(0, 1 + amount * mask * (amount > 0 ? vibranceWeight : 1));
    float3 target = fromLab(float3(lab.x, ab * gain));
    float lo = gain, hi = gain;
    if (!inGamut(target))
    {
        lo = 0;
        [unroll] for (int i = 0; i < 12; ++i) { float g = (lo + hi) * .5; if (inGamut(fromLab(float3(lab.x, ab * g)))) lo = g; else hi = g; }
    }
    float3 result = fromLab(float3(lab.x, ab * lo));
    return isLinear > .5 ? result : encodeSrgb(result);
}
D2D_PS_ENTRY(main)
{
    float2 p = D2DGetScenePosition().xy;
    float4 source = D2DSampleInputAtPosition(0, p);
    if (outputMode < .5 && (source.a <= 0 || !inGamut(unpremultiply(source)) || (brightness == 1 && amount == 0 && hueShift == 0))) return source;
    float4 blurred = D2DSampleInputAtPosition(1, p);
    float mask = pow(saturate(blurred.a > 0 ? blurred.r / blurred.a : 0), 1.0 / max(.1, contrast));
    if (outputMode < .5 && mask <= 0) return source;
    if (outputMode > 1.5) { float v = mask; return float4(v * source.a, v * source.a, v * source.a, source.a); }
    if (outputMode > .5) { float v = 1 - mask; return float4(v * source.a, v * source.a, v * source.a, source.a); }
    float3 rgb = adjustColor(unpremultiply(source), mask);
    return float4(rgb * source.a, source.a);
}
