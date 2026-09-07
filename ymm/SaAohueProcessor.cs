using Vortice.Direct2D1;
using Vortice.Direct2D1.Effects;
using YukkuriMovieMaker.Commons;
using YukkuriMovieMaker.Player.Video;

namespace SaAohueYmm;

internal sealed class SaAohueProcessor : IVideoEffectProcessor
{
    private readonly SaAohueEffect _item;
    private readonly LineMaskEffect? _mask;
    private readonly GaussianBlur? _blur;
    private readonly CompositeEffect? _composite;
    private readonly ID2D1Image? _output;
    private ID2D1Image? _input;

    public SaAohueProcessor(IGraphicsDevicesAndContext devices, SaAohueEffect item)
    {
        _item = item;
        LineMaskEffect? mask = null;
        GaussianBlur? blur = null;
        CompositeEffect? composite = null;
        ID2D1Image? output = null;
        try
        {
            mask = new LineMaskEffect(devices);
            blur = new GaussianBlur(devices.DeviceContext);
            composite = new CompositeEffect(devices);
            if (!mask.IsEnabled || !composite.IsEnabled)
                return;
            using (var maskOutput = mask.Output)
                blur.SetInput(0, maskOutput, true);
            using (var blurredMask = blur.Output)
                composite.SetInput(1, blurredMask, true);
            output = composite.Output;
            _mask = mask; _blur = blur; _composite = composite; _output = output;
            mask = null; blur = null; composite = null; output = null;
        }
        finally
        {
            output?.Dispose();
            composite?.Dispose();
            blur?.Dispose();
            mask?.Dispose();
        }
    }

    public ID2D1Image Output => _output ?? _input ?? throw new InvalidOperationException("入力が未設定です。");

    public void SetInput(ID2D1Image? input)
    {
        _input = input;
        _mask?.SetInput(0, input, true);
        _composite?.SetInput(0, input, true);
    }

    public void ClearInput()
    {
        _input = null;
        _mask?.SetInput(0, null, true);
        _composite?.SetInput(0, null, true);
    }

    public DrawDescription Update(EffectDescription effectDescription)
    {
        if (_mask is null || _blur is null || _composite is null)
            return effectDescription.DrawDescription;
        var frame = effectDescription.ItemPosition.Frame;
        var length = effectDescription.ItemDuration.Frame;
        var fps = effectDescription.FPS;
        _mask.Threshold = (float)(_item.LineThreshold.GetValue(frame, length, fps) / 255.0);
        _mask.Invert = _item.InvertLines ? 1f : 0f;
        var isLinear = _item.RgbEncoding == SaAohueRgbEncoding.LinearSrgb ? 1f : 0f;
        _mask.Linear = isLinear;
        _blur.StandardDeviation = Math.Max(0.1f, (float)(_item.Radius.GetValue(frame, length, fps) / Math.Sqrt(3)));
        _composite.Amount = (float)(_item.Amount.GetValue(frame, length, fps) / 100.0);
        _composite.Contrast = (float)_item.Contrast.GetValue(frame, length, fps);
        _composite.Brightness = (float)(_item.Brightness.GetValue(frame, length, fps) / 100.0);
        _composite.HueShift = (float)_item.HueShift.GetValue(frame, length, fps);
        _composite.Vibrance = (float)(_item.Vibrance.GetValue(frame, length, fps) / 100.0);
        _composite.OutputMode = (float)_item.OutputMode;
        _composite.Linear = isLinear;
        return effectDescription.DrawDescription;
    }

    public void Dispose()
    {
        ClearInput();
        _blur?.SetInput(0, null, true);
        _composite?.SetInput(1, null, true);
        _output?.Dispose();
        _composite?.Dispose();
        _blur?.Dispose();
        _mask?.Dispose();
    }
}
