using System.ComponentModel.DataAnnotations;
using YukkuriMovieMaker.Commons;
using YukkuriMovieMaker.Controls;
using YukkuriMovieMaker.Exo;
using YukkuriMovieMaker.Player.Video;
using YukkuriMovieMaker.Plugin.Effects;

namespace SaAohueYmm;

[VideoEffect("Sa_aohue", ["フィルタ"], ["線画", "彩度", "OKLab"], IsAviUtlSupported = false)]
public sealed class SaAohueEffect : VideoEffectBase
{
    public override string Label => "Sa_aohue";

    [Display(Name = "量", Description = "線に沿って増やす彩度", Order = 0)]
    [AnimationSlider("F1", "%", 0, 200)]
    public Animation Amount { get; } = new(100, 0, 400);

    [Display(Name = "半径", Description = "線を広げる半径", Order = 1)]
    [AnimationSlider("F1", "px", 1, 128)]
    public Animation Radius { get; } = new(24, 1, 512);

    [Display(Name = "コントラスト", Description = "線マスクのガンマ", Order = 2)]
    [AnimationSlider("F2", "", 0.1, 4)]
    public Animation Contrast { get; } = new(1, 0.1, 4);

    [Display(Name = "出力", Description = "合成または線マスクを出力", Order = 3)]
    [EnumComboBox]
    public SaAohueOutputMode OutputMode { get => _outputMode; set => Set(ref _outputMode, value); }
    private SaAohueOutputMode _outputMode = SaAohueOutputMode.Composite;

    [Display(Name = "線を反転", Description = "検出する線の明暗側を反転", Order = 4)]
    [ToggleSlider]
    public bool InvertLines { get => _invertLines; set => Set(ref _invertLines, value); }
    private bool _invertLines;

    [Display(Name = "線しきい値", Description = "大きいほど強い線だけを検出", Order = 5)]
    [AnimationSlider("F1", "%", 0, 100)]
    public Animation LineThreshold { get; } = new(50, 0, 100);

    [Display(Name = "RGB エンコード", Description = "入力が線形 sRGB の場合に切り替え", Order = 6)]
    [EnumComboBox]
    public SaAohueRgbEncoding RgbEncoding { get => _rgbEncoding; set => Set(ref _rgbEncoding, value); }
    private SaAohueRgbEncoding _rgbEncoding = SaAohueRgbEncoding.Srgb;

    public override IEnumerable<string> CreateExoVideoFilters(int keyFrameIndex, ExoOutputDescription exoOutputDescription) => [];

    public override IVideoEffectProcessor CreateVideoEffect(IGraphicsDevicesAndContext devices) => new SaAohueProcessor(devices, this);

    protected override IEnumerable<IAnimatable> GetAnimatables() => [Amount, Radius, Contrast, LineThreshold];
}

public enum SaAohueOutputMode { Composite, Lines, LinesInverted }
public enum SaAohueRgbEncoding { Srgb, LinearSrgb }
