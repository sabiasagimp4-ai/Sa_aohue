namespace SaAohueYmm;

// Discrete Gaussian normalization shared by both shader passes.
internal readonly record struct DogKernel(float Scale, float SmallInverseSum, float LargeInverseSum, float PositiveLobe)
{
    public static DogKernel Create(float scale)
    {
        scale = Math.Clamp(scale, .6f, 4f);
        int smallRadius = (int)MathF.Ceiling(3f * scale);
        int largeRadius = (int)MathF.Ceiling(3f * (1.6f * scale));
        double[] small = new double[2 * largeRadius + 1];
        double[] large = new double[small.Length];
        double smallSum = 0, largeSum = 0;
        double largeScale = 1.6f * scale;
        for (int x = -largeRadius; x <= largeRadius; x++)
        {
            int i = x + largeRadius;
            small[i] = Math.Abs(x) <= smallRadius ? Math.Exp(-.5 * x * x / ((double)scale * scale)) : 0;
            large[i] = Math.Exp(-.5 * x * x / (largeScale * largeScale));
            smallSum += small[i];
            largeSum += large[i];
        }
        double positive = 0;
        for (int y = 0; y < small.Length; y++)
            for (int x = 0; x < small.Length; x++)
                positive += Math.Max(0, small[x] * small[y] / (smallSum * smallSum)
                    - large[x] * large[y] / (largeSum * largeSum));
        return new(scale, (float)(1 / smallSum), (float)(1 / largeSum), (float)positive);
    }
}
