// Scalar HLSL/C++ shared functions: the native tests execute production logic.
float ClampUnit(float value) { return value < 0 ? 0 : (value > 1 ? 1 : value); }
float UnitSmooth(float value) { float t = ClampUnit(value); return t * t * (3 - 2 * t); }
float LineWeight(float response, float cutoff, float influence, float stability)
{
    float mask = response > cutoff ? 1 : 0;
    if (stability > 0)
    {
        float width = .08f * stability;
        if (width < 1e-6f) width = 1e-6f;
        mask = UnitSmooth((response - cutoff) / width);
    }
    return mask * (1 - influence + influence * ClampUnit(response / .25f));
}
float SideWeight(float sourceL, float referenceL, float sideMode)
{
    if (sideMode < .5f) return 1;
    float brightWeight = UnitSmooth((sourceL - referenceL + .005f) / .01f);
    return sideMode < 1.5f ? brightWeight : 1 - brightWeight;
}
