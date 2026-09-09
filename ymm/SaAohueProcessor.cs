using Vortice.Direct2D1;
using Vortice.Direct2D1.Effects;
using YukkuriMovieMaker.Commons;
using YukkuriMovieMaker.Player.Video;

namespace SaAohueYmm;

internal sealed class SaAohueProcessor : IVideoEffectProcessor
{
    private readonly SaAohueEffect _item;
    private readonly GaussianHorizontalEffect? _horizontal;
    private float _lastScale = float.NaN;
    private bool _blurBypassed;
    private bool _cleanupBypassed;
    private readonly LineMaskEffect? _mask;
    private readonly PointCleanupEffect? _cleanup;
    private readonly GaussianBlur? _blur;
    private readonly ChromaFieldEffect? _chromaField;
    private readonly GaussianBlur? _chromaBlur;
    private readonly CompositeEffect? _composite;
    private readonly ID2D1Image? _output;
    private ID2D1Image? _input;
    private bool _chromaBlurBypassed;

    public SaAohueProcessor(IGraphicsDevicesAndContext devices, SaAohueEffect item)
    {
        _item = item;
        GaussianHorizontalEffect? horizontal = null;
        LineMaskEffect? mask = null;
        PointCleanupEffect? cleanup = null;
        GaussianBlur? blur = null;
        ChromaFieldEffect? chromaField = null;
        GaussianBlur? chromaBlur = null;
        CompositeEffect? composite = null;
        ID2D1Image? output = null;
        try
        {
            horizontal = new GaussianHorizontalEffect(devices);
            mask = new LineMaskEffect(devices);
            cleanup = new PointCleanupEffect(devices);
            blur = new GaussianBlur(devices.DeviceContext);
            blur.Optimization = GaussianBlurOptimization.Quality;
            blur.BorderMode = BorderMode.Soft;
            chromaField = new ChromaFieldEffect(devices);
            chromaBlur = new GaussianBlur(devices.DeviceContext);
            chromaBlur.Optimization = GaussianBlurOptimization.Quality;
            chromaBlur.BorderMode = BorderMode.Soft;
            composite = new CompositeEffect(devices);
            if (!horizontal.IsEnabled || !mask.IsEnabled || !cleanup.IsEnabled || !chromaField.IsEnabled || !composite.IsEnabled)
                return;
            using (var horizontalOutput = horizontal.Output)
                mask.SetInput(0, horizontalOutput, true);
            using (var maskOutput = mask.Output)
                cleanup.SetInput(0, maskOutput, true);
            using (var cleanOutput = cleanup.Output)
                blur.SetInput(0, cleanOutput, true);
            using (var blurredMask = blur.Output)
                composite.SetInput(1, blurredMask, true);
            using (var chromaFieldOutput = chromaField.Output)
                chromaBlur.SetInput(0, chromaFieldOutput, true);
            using (var blurredChroma = chromaBlur.Output)
                composite.SetInput(2, blurredChroma, true);
            output = composite.Output;
            using (var cleanOutput = cleanup.Output)
                composite.SetInput(3, cleanOutput, true);
            _mask = mask; _blur = blur; _chromaField = chromaField; _chromaBlur = chromaBlur;
            _composite = composite; _output = output;
            _horizontal = horizontal;
            _cleanup = cleanup;
            cleanup = null;
            horizontal = null;
            mask = null; blur = null; chromaField = null; chromaBlur = null; composite = null; output = null;
        }
        finally
        {
            output?.Dispose();
            composite?.Dispose();
            blur?.Dispose();
            chromaBlur?.Dispose();
            chromaField?.Dispose();
            cleanup?.Dispose();
            mask?.Dispose();
            horizontal?.Dispose();
        }
    }

    public ID2D1Image Output => _output ?? _input ?? throw new InvalidOperationException("入力が未設定です。");

    public void SetInput(ID2D1Image? input)
    {
        _input = input;
        _horizontal?.SetInput(0, input, true);
        _mask?.SetInput(1, input, true);
        _chromaField?.SetInput(0, input, true);
        _composite?.SetInput(0, input, true);
        if (_chromaBlurBypassed)
            _composite?.SetInput(2, input, true);
    }

    public void ClearInput()
    {
        _input = null;
        _horizontal?.SetInput(0, null, true);
        _mask?.SetInput(1, null, true);
        _chromaField?.SetInput(0, null, true);
        _composite?.SetInput(0, null, true);
        if (_chromaBlurBypassed)
            _composite?.SetInput(2, null, true);
    }

    public DrawDescription Update(EffectDescription effectDescription)
    {
        if (_horizontal is null || _mask is null || _cleanup is null || _blur is null || _chromaField is null || _chromaBlur is null || _composite is null)
            return effectDescription.DrawDescription;
        var frame = effectDescription.ItemPosition.Frame;
        var length = effectDescription.ItemDuration.Frame;
        var fps = effectDescription.FPS;
        float scale = Math.Clamp((float)_item.DetectionScale.GetValue(frame, length, fps), .6f, 4f);
        if (scale != _lastScale)
        {
            var kernel = DogKernel.Create(scale);
            _horizontal.SetKernel(kernel);
            _mask.SetKernel(kernel);
            _lastScale = scale;
        }
        _mask.Threshold = (float)(Math.Round(_item.LineThreshold.GetValue(frame, length, fps)) / 255.0);
        _mask.Invert = _item.InvertLines ? 1f : 0f;
        _mask.StrengthInfluence = (float)(_item.StrengthInfluence.GetValue(frame, length, fps) / 100.0);
        _mask.Stability = (float)(_item.Stability.GetValue(frame, length, fps) / 100.0);
        float pointSize = Math.Clamp((float)Math.Round(_item.PointNoiseSize.GetValue(frame, length, fps)), 0f, 3f);
        _cleanup.PointSize = pointSize;
        bool cleanupBypass = pointSize == 0;
        bool cleanupChanged = cleanupBypass != _cleanupBypassed;
        if (cleanupChanged)
        {
            using var cleanInput = cleanupBypass ? _mask.Output : _cleanup.Output;
            _blur.SetInput(0, cleanInput, true);
            _composite.SetInput(3, cleanInput, true);
            _cleanupBypassed = cleanupBypass;
        }
        _composite.SideMode = (float)_item.SideMode;
        float radius = Math.Clamp((float)_item.Radius.GetValue(frame, length, fps), 0f, 512f);
        bool bypass = radius == 0;
        if (bypass != _blurBypassed || (bypass && cleanupChanged))
        {
            using var maskInput = bypass ? (_cleanupBypassed ? _mask.Output : _cleanup.Output) : _blur.Output;
            _composite.SetInput(1, maskInput, true);
            _blurBypassed = bypass;
        }
        if (!bypass) _blur.StandardDeviation = radius;
        float colorBleed = Math.Clamp((float)(_item.ColorBleed.GetValue(frame, length, fps) / 100.0), -1f, 1f);
        _composite.ColorBleed = colorBleed;
        bool chromaBypass = radius == 0 || MathF.Abs(colorBleed) < 1e-5f;
        if (chromaBypass != _chromaBlurBypassed)
        {
            SetChromaInput(chromaBypass);
            _chromaBlurBypassed = chromaBypass;
        }
        if (!chromaBypass) _chromaBlur.StandardDeviation = radius;
        _composite.Amount = (float)(_item.Amount.GetValue(frame, length, fps) / 100.0);
        _composite.Contrast = (float)_item.Contrast.GetValue(frame, length, fps);
        _composite.Brightness = (float)(_item.Brightness.GetValue(frame, length, fps) / 100.0);
        _composite.HueShift = (float)_item.HueShift.GetValue(frame, length, fps);
        _composite.OutputMode = (float)_item.OutputMode;
        return effectDescription.DrawDescription;
    }

    private void SetChromaInput(bool bypass)
    {
        if (bypass && _input is not null)
        {
            // With no active colour exchange the source image is a valid dummy
            // input, and this disconnects the extra conversion/blur work from
            // the composite graph.  The shader still guards the input by the
            // ColorBleed value, so toggling is safe on an animated parameter.
            _composite?.SetInput(2, _input, true);
            return;
        }
        using var chromaInput = bypass ? _chromaField?.Output : _chromaBlur?.Output;
        _composite?.SetInput(2, chromaInput, true);
    }

    public void Dispose()
    {
        ClearInput();
        _blur?.SetInput(0, null, true);
        _chromaBlur?.SetInput(0, null, true);
        _composite?.SetInput(1, null, true);
        _composite?.SetInput(2, null, true);
        _composite?.SetInput(3, null, true);
        _chromaField?.SetInput(0, null, true);
        _cleanup?.SetInput(0, null, true);
        _mask?.SetInput(0, null, true);
        _output?.Dispose();
        _composite?.Dispose();
        _blur?.Dispose();
        _chromaBlur?.Dispose();
        _chromaField?.Dispose();
        _cleanup?.Dispose();
        _mask?.Dispose();
        _horizontal?.Dispose();
    }
}
