using SaAohueYmm;

foreach (float scale in new[] { .6f, 1f, 1.6f, 2.5f, 4f })
{
    var kernel = DogKernel.Create(scale);
    if (!(kernel.PositiveLobe > 0 && kernel.PositiveLobe < 1))
        throw new Exception("Invalid DoG normalization");
    int rs = (int)MathF.Ceiling(3f * scale);
    int rb = (int)MathF.Ceiling(3f * (1.6f * scale));
    double ss = 0, sb = 0;
    for (int x = -rb; x <= rb; x++)
    {
        if (Math.Abs(x) <= rs) ss += Math.Exp(-.5 * x * x / ((double)scale * scale));
        double big = 1.6f * scale;
        sb += Math.Exp(-.5 * x * x / (big * big));
    }
    if (Math.Abs(ss * kernel.SmallInverseSum - 1) > 1e-6 ||
        Math.Abs(sb * kernel.LargeInverseSum - 1) > 1e-6)
        throw new Exception("Invalid Gaussian sum");
    Console.WriteLine($"PASS: scale={scale}, positive lobe={kernel.PositiveLobe}");
}
if (DogKernel.Create(0).Scale != .6f || DogKernel.Create(100).Scale != 4f)
    throw new Exception("Scale clamp failed");
