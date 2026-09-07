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
    private readonly LineMaskEffect? _mask;
    private readonly GaussianBlur? _blur;
    private readonly CompositeEffect? _composite;
    private readonly ID2D1Image? _output;
    private ID2D1Image? _input;

    public SaAohueProcessor(IGraphicsDevicesAndContext devices, SaAohueEffect item)
    {
        _item = item;
        GaussianHorizontalEffect? horizontal = null;
        LineMaskEffect? mask = null;
        GaussianBlur? blur = null;
        CompositeEffect? composite = null;
        ID2D1Image? output = null;
        try
        {
            horizontal = new GaussianHorizontalEffect(devices);
            mask = new LineMaskEffect(devices);
            blur = new GaussianBlur(devices.DeviceContext);
            blur.Optimization = GaussianBlurOptimization.Quality;
            blur.BorderMode = BorderMode.Soft;
            composite = new CompositeEffect(devices);
            if (!horizontal.IsEnabled || !mask.IsEnabled || !composite.IsEnabled)
                return;
            using (var horizontalOutput = horizontal.Output)
                mask.SetInput(0, horizontalOutput, true);
            using (var maskOutput = mask.Output)
                blur.SetInput(0, maskOutput, true);
            using (var blurredMask = blur.Output)
                composite.SetInput(1, blurredMask, true);
            output = composite.Output;
            _mask = mask; _blur = blur; _composite = composite; _output = output;
            _horizontal = horizontal;
            horizontal = null;
            mask = null; blur = null; composite = null; output = null;
        }
        finally
        {
            output?.Dispose();
            composite?.Dispose();
            blur?.Dispose();
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
        _composite?.SetInput(0, input, true);
    }

    public void ClearInput()
    {
        _input = null;
        _horizontal?.SetInput(0, null, true);
        _mask?.SetInput(1, null, true);
        _composite?.SetInput(0, null, true);
    }

    public DrawDescription Update(EffectDescription effectDescription)
    {
        if (_horizontal is null || _mask is null || _blur is null || _composite is null)
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
        float radius = Math.Clamp((float)_item.Radius.GetValue(frame, length, fps), 0f, 512f);
        bool bypass = radius == 0;
        if (bypass != _blurBypassed)
        {
            using var maskInput = bypass ? _mask.Output : _blur.Output;
            _composite.SetInput(1, maskInput, true);
            _blurBypassed = bypass;
        }
        if (!bypass) _blur.StandardDeviation = radius;
        _composite.Amount = (float)(_item.Amount.GetValue(frame, length, fps) / 100.0);
        _composite.Contrast = (float)_item.Contrast.GetValue(frame, length, fps);
        _composite.Brightness = (float)(_item.Brightness.GetValue(frame, length, fps) / 100.0);
        _composite.HueShift = (float)_item.HueShift.GetValue(frame, length, fps);
        _composite.OutputMode = (float)_item.OutputMode;
        return effectDescription.DrawDescription;
    }

    public void Dispose()
    {
        ClearInput();
        _blur?.SetInput(0, null, true);
        _composite?.SetInput(1, null, true);
        _mask?.SetInput(0, null, true);
        _output?.Dispose();
        _composite?.Dispose();
        _blur?.Dispose();
        _mask?.Dispose();
        _horizontal?.Dispose();
    }
}
