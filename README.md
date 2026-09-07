# Sa_aohue 0.1 — 実験版

線画(輪郭)検出を起点に、輪郭が密な領域のOKLab Chromaを増やすAfter Effects用Windows x64 Effect Plug-inです。
AI推論も外部Depthレイヤーも使いません。プラグイン本体は`Support Files\Plug-ins\Sabiasagi\`へ導入済みです(AEを実際に起動しての動作検証はまだ行っていません)。

## 1. アルゴリズム

1. 元RGB(プリマルチプライ解除後)をOKLab Lへ変換する。`RGB Encoding`でsRGB/Linear sRGBを指定する。
2. 2スケールの円形ブラー(半径1px・1.6px、厳密なアンチエイリアス円盤平均)の差分(Difference of Gaussians)で線を検出する。`Line Threshold`(0〜0.03の可変しきい値)で二値化する。`Invert Lines`でどちら側の勾配を線とみなすか切り替える。
3. 二値化した線を`Radius`でブラーし、`Contrast`でガンマ補正する(`Cavity^(1/Contrast)`)。この結果を疑似Cavityとして使う。
4. Cavityに応じてOKLabのa/bを同倍率で増やす(`Amount`)。Lは維持し、色域外になる場合は倍率を二分探索で下げる。透明画素・非有限値・SDR色域外のHDR画素はCompositeでそのまま保持する。

輪郭・線が密な領域(建物・木・路面標示など)ほどCavityが高くなり、平坦な領域は0になります。幾何・奥行きの推定は一切行わないため、「凹んでいるから暗い」ではなく「線・輪郭が集中しているから暗い」という、イラスト的な擬似AOです。

### ブラー実装

手順2の1px/1.6px円盤ブラーは厳密なO(radius²)アンチエイリアス円盤平均(sub-pixel半径の差を保つ必要があるため)。
手順3の`Radius`ブラー(最大512px)は、AEの「高速ボックスブラー」と同じ手法 — 水平・垂直の移動窓平均(summed-runningsum、半径に関わらずO(w×h)/pass)を3回繰り返してガウシアンに近似する(各passの半径は`radius/√3`)。半径が大きいほど有利で、旧来の円盤/Vogelサンプリング(半径に応じてO(radius²)〜O(w×h×256))から置き換えた。

## 2. UI

| UI | 意味 |
|---|---|
| Amount | Chroma増加率。100なら最大2倍。実際にはCavityと色域制限で減る。0はCompositeで元画素をコピー。 |
| Radius | 二値化後のブラー半径(px, 1–512)。 |
| Contrast | `Cavity^(1/Contrast)`。大きい値ほど弱い応答も持ち上がる。 |
| Output Mode | Composite / Lines(黒線・白背景) / Lines Inverted(白線・黒背景)。 |
| Invert Lines | DoGのどちら側の勾配を線とみなすか切り替える。 |
| Line Threshold | DoGの二値化しきい値を0〜0.03の範囲で調整(0で微小な勾配も全て線、100で強い輪郭だけ)。既定50で0.015。 |
| RGB Encoding | sRGB / Linear sRGB。AEプロジェクトに合わせて手動指定してからLを取る。 |

AEのプリマルチプライ画素をアンプリマルチプライして色を処理し、元アルファで戻します。チェックアウトした入力の近傍を読むため、SmartFXは全入力を要求します。外部レイヤーは不要です。

## 3. 実装ファイル

- `src/core.h`: SDK非依存。OKLab変換・Chroma増加(`chroma`)・DoG線検出とブラー(`lineArt`, `smallDisc`, `fastBlur`/`boxPass`)。
- `src/Sa_aohue.cpp`: AEパラメーター、8/16/32bpc、SmartFX。
- `src/Sa_aohuePiPL.r`, `Win/Sa_aohue.vcxproj`: AE登録情報とWindowsビルド。
- `tests/core_bridge.cpp`, `tests/check_native.py`: `lineArt`/`chroma`をNumPy独立実装と比較(AI不要)。
- `tests/check_ae.cpp`: SDKを使ったオフラインアダプターテスト(8/16/32bpc、タイル/クロップ一致、Amount=0完全一致、Output Mode相補、NaN/色域外、Line Art edge+invert)。
- `tests/render_bridge.cpp`: 任意画像でAEを起動せず`render()`を直接叩くための診断用DLL。

## 4. ビルドとテスト

必要環境: Windows x64、Visual Studio 2022 C++ v143、Windows SDK 10.0.26100.0、Adobe After Effects SDK 25.6。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\build.ps1
```

`build.ps1`はSDKが別の場所なら `-SdkRoot '...SDK root...'` を指定します。導入済みae-plugin-devスキルのビルドヘルパーを使用します。
ヘルパーのない環境では、VS x64 Developer Command Promptで次を実行できます。

```bat
set "AE_SDK_ROOT=C:\path\to\ae25.6_61.64bit.AfterEffectsSDK"
msbuild Win\Sa_aohue.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

出力: `build/Sa_aohue.aex`。C++ランタイムは静的リンク。SDK本体は同梱しません。
`tests/build.ps1`は`core_bridge.dll`をビルドして`tests/check_native.py`(NumPy独立実装との数値比較)を実行し、続けて`check_ae.cpp`(SDKアダプターテスト)をビルド・実行します。両方ともAI/外部モデル不要です。

## 5. AEへの導入方法

1. AEをユーザー自身で終了する。
2. `build/Sa_aohue.aex` を `C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\Sabiasagi\` にコピーする(書込み権限が必要)。
3. AEを起動し、対象レイヤーへ `Sabiasagi > Sa_aohue` を適用する。外部レイヤーやPython前処理は不要 — 適用するだけで線検出→Cavity→Chroma増加が動く。

AEへの起動・終了・プロジェクト操作(レイヤーの実適用等)は今回未実施です。プラグイン本体のコピーのみ実施しました。

## 6. テスト結果

実行環境: MSVC v143、Python 3.13 / NumPy(`check_native.py`のみ)。

| 検証 | 結果 |
|---|---|
| C++対NumPy(`check_native.py`) | 平坦・勾配・段差・ガウシアン凹み・乱数の5パターン×Radius(1/6/24/64)×Invert(0/1)、計40条件。line_art_mean_abs_error最大約3.1e-7。`chroma`のRGB最大絶対誤差約8.8e-5。 |
| SDKアダプター(`check_ae.cpp`) | 8/16/32bpc、非密rowbytes、アルファ、Amount=0完全一致、原点をずらした切り出し一致、Lines/Lines Inverted相補、src欠落時ゼロ埋め、色域外/NaN保持、実エッジへの反応、Invertでの出力反転、PiPLバージョン/フラグをオフライン検証。 |
| ビルド | Release x64成功、警告0・エラー0。 |
| AE本体 | 未検証。実ホストのSmartFX取得範囲・ROI・UI・32bpc・MFR・動画は別途確認が必要。 |

再実行:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/build.ps1
```

## 7. 残っている問題

- Line Artは幾何・奥行きを一切見ません。輪郭・模様の密度をCavityの代わりに使う擬似AOで、「凹んでいるから暗い」という意味付けはありません。
- DoGのカーネル半径(1px/1.6px)は固定値です。しきい値(`Line Threshold`)のみ可変で、低コントラストの写真では線がほとんど検出されず、逆に高周波ノイズの多い素材では線が過剰検出される可能性があります。
- 全入力チェックアウトは大画像でメモリと再計算量が増えます。タイル別halo、ホストの並列処理、GPU化は実測後の課題です。
- HDR画素の彩度変調、任意作業色空間の自動変換は未対応。RGB Encodingは手動指定です。
- AEランタイム検証と署名・配布パッケージ・商用ライセンス全体の監査は未完了です。

## 8. 過去の実験(廃止)

MoGe-2+GTAO(品質優先のmetric AO)、Depth Anything V2+horizon法(RGB Approximation/External Depth/External Cavity)は検討・実装しましたが、`Line Art`単体で十分と判断し、プラグイン本体からは削除しました。当時のPythonプロトタイプ(`prototype/gtao.py`, `prototype/prototype.py`等)と一次資料調査記録(`research/`)はリポジトリに残していますが、現在の`src/`とは連動していません。

## 9. 出力を維持した性能改善

線検出とブラーのバッファを再利用し、固定カーネル計算を画素ループの外へ移し、縦方向ブラーのメモリアクセスをまとめています。パラメーター・色変換・線の判定式は維持しています。CompositeのAmount=0では解析を省略します。

旧実装との厳密比較と計算コアのベンチマークは `python tests/run_portable.py` で実行できます。詳細と測定条件は [docs/PERFORMANCE.md](docs/PERFORMANCE.md) を参照してください。**同梱の `.aex` は更新前のバイナリです。最適化を使用するにはWindowsで再ビルドが必要です。**
