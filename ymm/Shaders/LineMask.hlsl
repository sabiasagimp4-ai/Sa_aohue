#define D2D_ENTRY main
#include <d2d1effecthelpers.hlsli>

float threshold;
float invertLines;
float scale;
float positiveLobe;
float smallInverseSum;
float largeInverseSum;
float padding0;
float padding1;
float4 inputBounds;

D2D_PS_ENTRY(main)
{
    float2 p = D2DGetScenePosition().xy;
    float alpha = D2DSampleInputAtPosition(1, p).a;
    if (alpha <= 0) return 0;
    float largeScale = 1.6 * scale;
    int smallRadius = (int)ceil(3 * scale);
    int largeRadius = (int)ceil(3 * largeScale);
    float4 sum = 0;
    [loop] for (int y = -largeRadius; y <= largeRadius; ++y)
    {
        float2 samplePosition = p + float2(0, y);
        if (any(samplePosition < inputBounds.xy) || any(samplePosition >= inputBounds.zw)) continue;
        float4 uv = D2DGetInputCoordinate(0);
        float4 h = InputTexture0.SampleLevel(InputSampler0, uv.xy + uv.zw * float2(0, y), 0);
        float ws = abs(y) <= smallRadius ? exp(-.5 * y * y / (scale * scale)) * smallInverseSum : 0;
        float wb = exp(-.5 * y * y / (largeScale * largeScale)) * largeInverseSum;
        sum += h * float4(ws, wb, ws, wb);
    }
    float smallL = sum.b > 0 ? sum.r / sum.b : 0;
    float largeL = sum.a > 0 ? sum.g / sum.a : 0;
    float dog = (smallL - largeL) / max(positiveLobe, 1e-6);
    float response = invertLines > .5 ? dog : -dog;
    // Calibrated AFTER positive-lobe normalization (see README).
    float edgeMask = response > saturate(threshold) * .08 ? 1 : 0;
    return float4(edgeMask * alpha, edgeMask * alpha, edgeMask * alpha, alpha);
}
