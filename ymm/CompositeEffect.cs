using System.Runtime.InteropServices;
using Vortice;
using Vortice.Direct2D1;
using YukkuriMovieMaker.Commons;
using YukkuriMovieMaker.Player.Video;

namespace SaAohueYmm;

internal sealed class CompositeEffect(IGraphicsDevicesAndContext devices) : D2D1CustomShaderEffectBase(Create<CompositeEffect.Impl>(devices))
{
    public float Amount { set => SetValue(0, value); }
    public float Contrast { set => SetValue(1, value); }
    public float OutputMode { set => SetValue(2, value); }
    public float Linear { set => SetValue(3, value); }
    public float Brightness { set => SetValue(4, value); }

    [CustomEffect(2)]
    private sealed class Impl : D2D1CustomShaderEffectImplBase<Impl>
    {
        private Constants _constants = new() { Amount = 1f, Contrast = 1f, Brightness = 1f };

        [CustomEffectProperty(PropertyType.Float, 0)] public float Amount { get => _constants.Amount; set { _constants.Amount = Math.Clamp(value, 0f, 4f); UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 1)] public float Contrast { get => _constants.Contrast; set { _constants.Contrast = Math.Clamp(value, .1f, 4f); UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 2)] public float OutputMode { get => _constants.OutputMode; set { _constants.OutputMode = Math.Clamp(MathF.Round(value), 0f, 2f); UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 3)] public float Linear { get => _constants.Linear; set { _constants.Linear = value >= .5f ? 1f : 0f; UpdateConstants(); } }
        [CustomEffectProperty(PropertyType.Float, 4)] public float Brightness { get => _constants.Brightness; set { _constants.Brightness = Math.Clamp(value, 0f, 2f); UpdateConstants(); } }

        public Impl() : base(ShaderResourceLoader.Get("Composite")) { }
        protected override void UpdateConstants() => drawInformation?.SetPixelShaderConstantBuffer(_constants);
        public override void MapInputRectsToOutputRect(RawRect[] inputRects, RawRect[] inputOpaqueSubRects, out RawRect outputRect, out RawRect outputOpaqueSubRect)
        {
            outputRect = inputRects[0];
            outputOpaqueSubRect = default;
        }
        public override void MapOutputRectToInputRects(RawRect outputRect, RawRect[] inputRects) { inputRects[0] = outputRect; inputRects[1] = outputRect; }

        [StructLayout(LayoutKind.Sequential)]
        private struct Constants
        {
            public float Amount, Contrast, OutputMode, Linear;
            public float Brightness, Padding0, Padding1, Padding2;
        }
    }
}
