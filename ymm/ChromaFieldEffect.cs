using Vortice;
using Vortice.Direct2D1;
using YukkuriMovieMaker.Commons;
using YukkuriMovieMaker.Player.Video;

namespace SaAohueYmm;

// Converts the source to an alpha-weighted OKLab chroma field.  The field is
// kept in a float render target so a later Gaussian pass can average a/b and
// the coverage weight without 8-bit quantisation.
internal sealed class ChromaFieldEffect(IGraphicsDevicesAndContext devices)
    : D2D1CustomShaderEffectBase(Create<ChromaFieldEffect.Impl>(devices))
{
    [CustomEffect(1)]
    private sealed class Impl : D2D1CustomShaderEffectImplBase<Impl>
    {
        public Impl() : base(ShaderResourceLoader.Get("ChromaField")) { }

        protected override void UpdateConstants()
        {
            drawInformation?.SetOutputBuffer(BufferPrecision.PerChannel32Float, ChannelDepth.Four);
        }

        public override void MapInputRectsToOutputRect(
            RawRect[] inputRects,
            RawRect[] inputOpaqueSubRects,
            out RawRect outputRect,
            out RawRect outputOpaqueSubRect)
        {
            outputRect = inputRects[0];
            outputOpaqueSubRect = default;
        }

        public override void MapOutputRectToInputRects(RawRect outputRect, RawRect[] inputRects)
        {
            inputRects[0] = outputRect;
        }
    }
}
