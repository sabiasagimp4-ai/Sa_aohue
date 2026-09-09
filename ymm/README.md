# Sa_aohue for YMM4 — 0.2.0

線に沿って彩度・輝度・色相を調整するYMM4用エフェクト。今回の更新はYMM4のみで、AE側は変更していません。

## UI

| 順序 | 名前 | 範囲 | 初期値 |
|---:|---|---|---|
| 1 | 検出サイズ | 0.6–4.0 px | 1.0 |
| 2 | 検出しきい値 | 0–255（整数） | 128 |
| 3 | 線を反転 | オフ: 暗い線 / オン: 明るい線 | オフ |
| 4 | 変化範囲 | 0–512 px | 24 |
| 5 | コントラスト | 0.1–4.0 | 1 |
| 6 | 彩度 | -100–400% | 100 |
| 7 | 輝度 | 0–200% | 100 |
| 8 | 色相 | -180–180° | 0 |
| 9 | 出力 | 合成 / 線マスク / 線マスク反転 | 合成 |

彩度0%は無変化、100%はマスク最大部でクロマ2倍、-100%は無彩色化です。
線マスクは効果が強い部分が白、線マスク反転はその逆です。
RGB Encodingと自然な鮮やかさは削除しました。入力はsRGB前提です。HDR画素の色調整、非sRGB/線形ワークスペースは保証対象外です。

## 検出・ぼかし

- sRGBをデコードし、従来同様OKLab Lを線検出に使用します。
- 横・縦の分離Gaussian（σと1.6σ、半径ceil(3σ)）の差分を正側L1ノルムで正規化します。
- 横パスでは小/大Gaussianの輝度×アルファとアルファ重みを4チャンネルに保持し、縦パス後に割ります。中間は32bit floatです。
- 画像外を明示的に除外し、透明・半透明部分を重み付き正規化します。
- 内部しきい値は (表示値 / 255) × 0.08。先行仕様書の0.04は正規化後のノイズ基準を満たさなかったため修正しました。
- 変化範囲はDirect2D GaussianBlurの標準偏差へ直接渡します（Quality / Soft）。0では接続を切り替え、ぼかしを完全に省略します。
- ぼかしマスクもRGB/alphaで正規化します。コントラスト1ではpowを省略します。
- 色域内の目標色には二分探索を行いません。色域外のみ12回探索します。

## 互換性

既存のAmount/Radius/Contrast/Brightness/HueShift/LineThreshold/OutputMode/InvertLinesという保存名と出力enum値は維持しました。
削除したVibrance/RgbEncodingは実装から除去しています。旧プロジェクトの未知プロパティ取り扱いはYMM4実機で要確認です。

**v0.1と見た目は一致しません。** 検出器、変化範囲の尺度、マスク出力の極性が変わります。検出サイズは新規初期値1.0です。更新前にプロジェクトと旧DLLをバックアップしてください。

## ビルド・インストール

GitHub Actionsの「YMM4 build」は公式YMM4 LiteのDLLを参照し、Windows SDKのfxcで3つのシェーダーをコンパイルします。
成功した実行のArtifactsからSaAohueYmmをダウンロードできます。

.NET 10 SDK、YMM4のDLLフォルダ、Windows SDKのfxc.exeとd2d1effecthelpers.hlsliを指定します。

```powershell
dotnet build ymm/SaAohueYmm.csproj -c Release "-p:YMM4DirPath=C:\\path\\to\\YMM4\\" "-p:FxcPath=C:\\path\\to\\fxc.exe" "-p:D2DIncludePath=C:\\path\\to\\sdk\\um"
./ymm/check-shaders.ps1 -FxcPath C:\\path\\to\\fxc.exe
```

生成されたSaAohueYmm.dllをYMM4のuser/plugin/SaAohueYmm/へコピーし、YMM4を再起動します。

## 検証

- python tests/test_ymm_v02.py: 独立2次元畳み込みとfloat32分離処理の比較、透明境界、ノイズ、極性、UI/処理契約。
- dotnet run --project ymm/tests/KernelTests.csproj -c Release: 本番C#カーネルの合計・L1ノルム・範囲。
- check-shaders.ps1: 3シェーダーの座標入力と定数バッファ配置。
- YMM4の実画面での描画、旧プロジェクト読込、保存・再読込、アニメーション、GPU別の動作確認は別途必要です。参照テストはGPUの実行結果を保証しません。
