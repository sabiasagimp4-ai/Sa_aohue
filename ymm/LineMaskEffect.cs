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
    public float Linear { set => SetValue(2, value); }

    [CustomEffect(1)]
    private sealed class Impl : D2D1CustomShaderEffectImplBase<Impl>
    {
        private Constants _constants;

        [CustomEffectProperty(PropertyType.Float, 0)] public float Threshold { get => _constants.Threshold; set { _constants.Threshold = Math.Clamp(value, 0f, 1f); UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 1)] public float Invert { get => _constants.Invert; set { _constants.Invert = value >= .5f ? 1f : 0f; UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 2)] public float Linear { get => _constants.Linear; set { _constants.Linear = value >= .5f ? 1f : 0f; UpdateConstants(); } }

        public Impl() : base(ShaderResourceLoader.Get("LineMask")) { }
        protected override void UpdateConstants() => drawInformation?.SetPixelShaderConstantBuffer(_constants);
        public override void MapOutputRectToInputRects(RawRect outputRect, RawRect[] inputRects) => inputRects[0] = new RawRect(outputRect.Left - 3, outputRect.Top - 3, outputRect.Right + 3, outputRect.Bottom + 3);

        [StructLayout(LayoutKind.Sequential)]
        private struct Constants { public float Threshold, Invert, Linear, Padding; }
    }
}
