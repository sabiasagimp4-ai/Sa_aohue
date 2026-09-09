using System.Runtime.InteropServices;
using Vortice;
using Vortice.Direct2D1;
using YukkuriMovieMaker.Commons;
using YukkuriMovieMaker.Player.Video;

namespace SaAohueYmm;

internal sealed class PointCleanupEffect(IGraphicsDevicesAndContext devices)
    : D2D1CustomShaderEffectBase(Create<PointCleanupEffect.Impl>(devices))
{
    public float PointSize { set => SetValue(0, value); }

    [CustomEffect(1)]
    private sealed class Impl : D2D1CustomShaderEffectImplBase<Impl>
    {
        private Constants _constants;
        [CustomEffectProperty(PropertyType.Float, 0)]
        public float PointSize { get => _constants.PointSize; set { _constants.PointSize = Math.Clamp(MathF.Round(value), 0f, 3f); UpdateConstants(); } }
        public Impl() : base(ShaderResourceLoader.Get("PointCleanup")) { }
        protected override void UpdateConstants()
        {
            drawInformation?.SetOutputBuffer(BufferPrecision.PerChannel32Float, ChannelDepth.Four);
            drawInformation?.SetPixelShaderConstantBuffer(_constants);
        }
        public override void MapInputRectsToOutputRect(RawRect[] inputRects, RawRect[] inputOpaqueSubRects, out RawRect outputRect, out RawRect outputOpaqueSubRect)
        {
            outputRect = inputRects[0];
            outputOpaqueSubRect = default;
            _constants.Left = outputRect.Left; _constants.Top = outputRect.Top;
            _constants.Right = outputRect.Right; _constants.Bottom = outputRect.Bottom;
            UpdateConstants();
        }
        public override void MapOutputRectToInputRects(RawRect outputRect, RawRect[] inputRects)
        {
            int halo = _constants.PointSize > 0 ? 3 : 0;
            inputRects[0] = new(outputRect.Left - halo, outputRect.Top - halo, outputRect.Right + halo, outputRect.Bottom + halo);
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct Constants
        {
            public float PointSize, Padding0, Padding1, Padding2;
            public float Left, Top, Right, Bottom;
        }
    }
}
