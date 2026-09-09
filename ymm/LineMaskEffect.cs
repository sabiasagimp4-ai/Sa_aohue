using System.Runtime.InteropServices;
using Vortice;
using Vortice.Direct2D1;
using YukkuriMovieMaker.Commons;
using YukkuriMovieMaker.Player.Video;

namespace SaAohueYmm;

internal sealed class LineMaskEffect(IGraphicsDevicesAndContext devices) : D2D1CustomShaderEffectBase(Create<LineMaskEffect.Impl>(devices))
{
    public float Threshold { set => SetValue(0, value); }
    public float Invert { set => SetValue(1, value); }
    public float StrengthInfluence { set => SetValue(6, value); }
    public float Stability { set => SetValue(7, value); }
    public void SetKernel(DogKernel kernel)
    {
        SetValue(2, kernel.Scale);
        SetValue(3, kernel.PositiveLobe);
        SetValue(4, kernel.SmallInverseSum);
        SetValue(5, kernel.LargeInverseSum);
    }
    [CustomEffect(2)]
    private sealed class Impl : D2D1CustomShaderEffectImplBase<Impl>
    {
        private Constants _constants = new() { Scale = 1, PositiveLobe = 1 };
        [CustomEffectProperty(PropertyType.Float, 0)] public float Threshold { get => _constants.Threshold; set { _constants.Threshold = Math.Clamp(value, 0f, 1f); UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 1)] public float Invert { get => _constants.Invert; set { _constants.Invert = value >= .5f ? 1f : 0f; UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 2)] public float Scale { get => _constants.Scale; set { _constants.Scale = Math.Clamp(value, .6f, 4f); UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 3)] public float PositiveLobe { get => _constants.PositiveLobe; set { _constants.PositiveLobe = value; UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 4)] public float SmallInverseSum { get => _constants.SmallInverseSum; set { _constants.SmallInverseSum = value; UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 5)] public float LargeInverseSum { get => _constants.LargeInverseSum; set { _constants.LargeInverseSum = value; UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 6)] public float StrengthInfluence { get => _constants.StrengthInfluence; set { _constants.StrengthInfluence = Math.Clamp(value, 0f, 1f); UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 7)] public float Stability { get => _constants.Stability; set { _constants.Stability = Math.Clamp(value, 0f, 1f); UpdateConstants(); } }
        public Impl() : base(ShaderResourceLoader.Get("LineMask")) { }
        protected override void UpdateConstants()
        {
            drawInformation?.SetOutputBuffer(BufferPrecision.PerChannel32Float, ChannelDepth.Four);
            drawInformation?.SetPixelShaderConstantBuffer(_constants);
        }
        public override void MapInputRectsToOutputRect(RawRect[] inputRects, RawRect[] inputOpaqueSubRects, out RawRect outputRect, out RawRect outputOpaqueSubRect)
        {
            outputRect = inputRects[1];
            outputOpaqueSubRect = default;
            _constants.Left = outputRect.Left; _constants.Top = outputRect.Top;
            _constants.Right = outputRect.Right; _constants.Bottom = outputRect.Bottom;
            UpdateConstants();
        }
        public override void MapOutputRectToInputRects(RawRect outputRect, RawRect[] inputRects)
        {
            int halo = (int)MathF.Ceiling(3f * (1.6f * _constants.Scale));
            inputRects[0] = new(outputRect.Left, outputRect.Top - halo, outputRect.Right, outputRect.Bottom + halo);
            inputRects[1] = outputRect;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct Constants
        {
            public float Threshold, Invert, Scale, PositiveLobe;
            public float SmallInverseSum, LargeInverseSum, StrengthInfluence, Stability;
            public float Left, Top, Right, Bottom;
        }
    }
}
