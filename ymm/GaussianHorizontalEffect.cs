using System.Runtime.InteropServices;
using Vortice;
using Vortice.Direct2D1;
using YukkuriMovieMaker.Commons;
using YukkuriMovieMaker.Player.Video;

namespace SaAohueYmm;

internal sealed class GaussianHorizontalEffect(IGraphicsDevicesAndContext devices)
    : D2D1CustomShaderEffectBase(Create<GaussianHorizontalEffect.Impl>(devices))
{
    public void SetKernel(DogKernel kernel)
    {
        SetValue(0, kernel.Scale);
        SetValue(1, kernel.SmallInverseSum);
        SetValue(2, kernel.LargeInverseSum);
    }
    [CustomEffect(1)]
    private sealed class Impl : D2D1CustomShaderEffectImplBase<Impl>
    {
        private Constants _constants = new() { Scale = 1 };
        [CustomEffectProperty(PropertyType.Float, 0)] public float Scale { get => _constants.Scale; set { _constants.Scale = Math.Clamp(value, .6f, 4f); UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 1)] public float SmallInverseSum { get => _constants.SmallInverseSum; set { _constants.SmallInverseSum = value; UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 2)] public float LargeInverseSum { get => _constants.LargeInverseSum; set { _constants.LargeInverseSum = value; UpdateConstants(); } }
        public Impl() : base(ShaderResourceLoader.Get("GaussianHorizontal")) { }
        protected override void UpdateConstants()
        {
            // RGBA is packed data, not premultiplied color. Avoid 8-bit quantization.
            drawInformation?.SetOutputBuffer(BufferPrecision.PerChannel32Float, ChannelDepth.Four);
            drawInformation?.SetPixelShaderConstantBuffer(_constants);
        }
        public override void MapOutputRectToInputRects(RawRect outputRect, RawRect[] inputRects)
        {
            int halo = (int)MathF.Ceiling(3f * (1.6f * _constants.Scale));
            inputRects[0] = new(outputRect.Left - halo, outputRect.Top, outputRect.Right + halo, outputRect.Bottom);
        }
        public override void MapInputRectsToOutputRect(RawRect[] inputRects, RawRect[] inputOpaqueSubRects, out RawRect outputRect, out RawRect outputOpaqueSubRect)
        {
            outputRect = inputRects[0];
            outputOpaqueSubRect = default;
            _constants.Left = outputRect.Left; _constants.Top = outputRect.Top;
            _constants.Right = outputRect.Right; _constants.Bottom = outputRect.Bottom;
            UpdateConstants();
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct Constants
        {
            public float Scale, SmallInverseSum, LargeInverseSum, Padding;
            public float Left, Top, Right, Bottom;
        }
    }
}
