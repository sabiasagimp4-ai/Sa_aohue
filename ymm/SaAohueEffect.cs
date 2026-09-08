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

    [Display(Name = "彩度", Description = "線に沿う彩度変化量。-100%で無彩色、0%で変化なし、100%で2倍", Order = 9)]
    [AnimationSlider("F1", "%", -100, 400)]
    public Animation Amount { get; } = new(100, -100, 400);

    [Display(Name = "変化範囲", Description = "線から色を変化させる広がり。0でぼかしなし", Order = 7)]
    [AnimationSlider("F1", "px", 0, 512)]
    public Animation Radius { get; } = new(24, 0, 512);

    [Display(Name = "コントラスト", Description = "1未満で変化域を絞り、1より大きいと弱い変化を持ち上げます", Order = 8)]
    [AnimationSlider("F2", "", 0.1, 4)]
    public Animation Contrast { get; } = new(1, 0.1, 4);

    [Display(Name = "輝度", Description = "線に沿って調整する明るさ。100%で変化なし", Order = 10)]
    [AnimationSlider("F1", "%", 0, 200)]
    public Animation Brightness { get; } = new(100, 0, 200);

    [Display(Name = "色相", Description = "線に沿う色相の移動量", Order = 11)]
    [AnimationSlider("F1", "°", -180, 180)]
    public Animation HueShift { get; } = new(0, -180, 180);

    [Display(Name = "出力", Description = "線マスクは効果が強い部分を白で表示", Order = 12)]
    [EnumComboBox]
    public SaAohueOutputMode OutputMode { get => _outputMode; set => Set(ref _outputMode, value); }
    private SaAohueOutputMode _outputMode = SaAohueOutputMode.Composite;

    [Display(Name = "線を反転", Description = "オフで暗い線、オンで明るい線を検出", Order = 2)]
    [ToggleSlider]
    public bool InvertLines { get => _invertLines; set => Set(ref _invertLines, value); }
    private bool _invertLines;

    [Display(Name = "検出しきい値", Description = "大きいほど強い線だけを検出", Order = 1)]
    [AnimationSlider("F0", "", 0, 255)]
    public Animation LineThreshold { get; } = new(128, 0, 255);

    [Display(Name = "検出サイズ", Description = "検出する線の太さ。変化範囲とは独立しています", Order = 0)]
    [AnimationSlider("F1", "px", 0.6, 4)]
    public Animation DetectionScale { get; } = new(1, 0.6, 4);

    [Display(Name = "強弱の反映", Description = "0%で検出した線を一律に、100%で強い線ほど強く色を変えます", Order = 3)]
    [AnimationSlider("F1", "%", 0, 100)]
    public Animation StrengthInfluence { get; } = new(0, 0, 100);

    [Display(Name = "点ノイズ除去", Description = "縦横とも指定px以下の孤立した検出を除去。0で無効、斜めのつながりも保持", Order = 4)]
    [AnimationSlider("F0", "px", 0, 3)]
    public Animation PointNoiseSize { get; } = new(0, 0, 3);

    [Display(Name = "安定性", Description = "検出境界の変化を滑らかにして点滅を軽減。0%で従来どおり。過去フレームは混ぜません", Order = 5)]
    [AnimationSlider("F1", "%", 0, 100)]
    public Animation Stability { get; } = new(0, 0, 100);

    [Display(Name = "適用する側", Description = "輪郭付近の明暗を基準に色を変える側を選択。線を反転とは独立", Order = 6)]
    [EnumComboBox]
    public SaAohueSideMode SideMode { get => _sideMode; set => Set(ref _sideMode, value); }
    private SaAohueSideMode _sideMode = SaAohueSideMode.Both;

    public override IEnumerable<string> CreateExoVideoFilters(int keyFrameIndex, ExoOutputDescription exoOutputDescription) => [];

    public override IVideoEffectProcessor CreateVideoEffect(IGraphicsDevicesAndContext devices) => new SaAohueProcessor(devices, this);

    protected override IEnumerable<IAnimatable> GetAnimatables() => [DetectionScale, LineThreshold, StrengthInfluence, PointNoiseSize, Stability, Radius, Contrast, Amount, Brightness, HueShift];
}

// Keep serialized names and numeric values; v0.2 intentionally changes mask polarity.
public enum SaAohueOutputMode
{
    [Display(Name = "合成")] Composite = 0,
    [Display(Name = "線マスク")] Lines = 1,
    [Display(Name = "線マスク反転")] LinesInverted = 2,
}

public enum SaAohueSideMode
{
    [Display(Name = "両側")] Both = 0,
    [Display(Name = "明るい側")] Bright = 1,
    [Display(Name = "暗い側")] Dark = 2,
}
