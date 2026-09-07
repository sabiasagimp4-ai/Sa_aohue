# Sa_aohue 0.1 — 実験版

局所的な閉塞を推定し、該当領域のChromaを増やすAfter Effects用Windows x64 Effect Plug-inです。
Pythonの静止画プロトタイプ、C++実装、ビルド済み `build/Sa_aohue.aex`、比較画像を含みます。
プラグイン本体は`Support Files\Plug-ins\Sabiasagi\`へ導入済みですが、AEを実際に起動しての動作検証はまだ行っていません。

## 1. 技術設計

品質優先の経路は、PythonでMoGe-2(ViT-B, normal付き)を実行し、metric point map・normal・camera intrinsicsからGTAO(Jimenez et al. 2016)を計算し、16-bit PNGのCavityをAEの外部レイヤーに渡す方式です。AI推論はローカルで行い、画像を外部へ送信しません。モデル取得時だけネットワークを使います。

1. sRGB静止画からMoGe-2でmetric point map(カメラ座標の3D点群, x=right/y=down/z=forward)・normal・camera intrinsicsを推定する。相対Depthではなく、metric scaleの実座標。
2. `--normal-blur`(既定6px)でnormalに円形ブラー(`gtao.py`の`disc_blur`、`cool_blur`の`gather_channel()`移植)をかけ再正規化する。0で無効化できる。7章参照(ブロックノイズ調査の経緯で追加したが、その調査では主因ではなかった)。
3. 各pixelでnormalをスクリーン空間のslice平面(既定9方向)へ投影し、符号付き角度`n`を得る。
4. 各slice方向、両側(各既定8点)へ実3D位置をbilinearサンプリングしてhorizon cosineを探索する。半径はmetric(既定0.5m)をintrinsicsでpixel換算するため、画像の解像度やフレーミングに依存しない。
5. 閉形積分 `Iarc(h,n) = (cosN + 2h·sin(n) - cos(2h-n)) / 4` でcosine重み付き可視性を解析的に評価する(モンテカルロ不要)。全slice平均が`visibility`。
6. `Cavity = 1 - visibility`。Cavityに応じてOKLabのa/bを同倍率で増やす。Lを維持し、色域外になる場合は倍率を二分探索で下げる。

`Cavity=0` は開放、`Cavity=1` は強い閉塞です。表示用 `AO=1-Cavity` とします。
実装は[GameTechDev/XeGTAO](https://github.com/GameTechDev/XeGTAO)のHLSL実装(`XeGTAO.hlsli` MainPass、MIT license)の数式をNumPyへ移植したもので、リアルタイム用のMIPチェーン・TAA・空間デノイザ・Hilbert曲線ノイズは実装していません(静止画オフライン用途のため単純な密なサンプリングで代替)。`prototype/gtao.py`参照。
normalを直接使うため、旧版(Depthの勾配だけで接平面補正するホライズン法)では拾えなかった苔のような細かい表面の凹凸もCavityに反映されます(6章の実写真比較を参照)。

**旧版(Depth Anything V2 Small + 相対Depthホライズン法)は、`src/core.h`・`Sa_aohue.cpp`のRGB ApproximationおよびExternal Depth入力の内部エンジンとして残っています。** MoGe-2+GTAOへの置き換えは品質優先のPython経路(External Cavity入力)の話で、この旧エンジン自体には変更を加えていません。旧エンジンの詳細:

- Depthの局所勾配を接平面として使い、8方向・各6点・2スケールで周囲の高さを調べ、接平面より上の遮蔽物のホライズンを求める。反対方向のホライズンの幾何平均を取り、一方向だけの輪郭反応を抑える。
- Depthをバイラテラル(edge-aware)ブラーで平滑化してからホライズン探索する(5x5窓、range+spatial重み)。
- 得られたCavityマップを後処理する(dead-band除去+再正規化、3x3ブラー、depth不連続部の信頼度抑制)。
- Normal画像を別途生成せず、Depthの勾配で接平面補正を行う。相対Depthと固定スケールによる見た目の指標で、校正された3D AOではない。

ネイティブ単独でも `RGB Approximation` が動きます。OKLab Lを小さく平滑化した疑似高さを用いるため、影・黒い模様の区別はできません。AI推定を装うモードではありません。

**新たに、AIも外部レイヤーも使わない第3の経路として `Line Art` をAEプラグイン本体(C++)に追加しました。** 線画(輪郭)検出→二値化→ブラーという素朴な画像空間処理で、幾何もAI推論も使わずCavity風のマップをその場で作ります。

1. 元RGBからOKLab Lを取り、2スケールの円形ブラー(半径1px・1.6px)の差分(Difference of Gaussians)で線を検出する。
2. しきい値(固定, ±0.004)で二値化する。`Invert Depth`チェックボックスでどちら側の勾配を線とみなすか切り替える。
3. 二値化した線をRadius(既定24px)でブラーし、Contrastでガンマ補正する。この結果をCavityとして使う。

ブラーは`aohue::discBlur`一本(`../cool_blur/plugin/CoolBlur_Core.h`の`gather_channel()`移植、edge=0のフラット円盤カーネル、box blurより等方的)。小半径(6px程度まで)はcool_blurと同じ169-tap閾値でO(radius²)の厳密円盤平均+アンチエイリアス縁、それを超える半径(最終段のRadiusブラー、最大512px)はcool_blurの`vogel_pattern()`(golden-angle螺旋、64/128/256点、bilinearサンプリング)を移植した固定サンプル数の円盤サンプリングに自動で切り替わる。半径に関わらずO(w×h×N)(N≤256)。旧版はここをsummed-area tableのbox blurにしていたが、角ばった見た目だったため円形サンプリングへ置き換えた。輪郭・線が密な領域(建物・木・路面標示など)ほどCavityが高くなり、空のような平坦領域は0になります。幾何・奥行きの推定は一切行わないため、「凹んでいるから暗い」ではなく「線・輪郭が集中しているから暗い」という、イラスト的な擬似AOです。

## 2. アルゴリズム選定と調査

一次資料確認日: 2026-09-07。参照READMEと一部LICENSEは `research/` に保存しました。

| 候補 | 特徴・採否 | ライセンス確認 |
|---|---|---|
| [MoGe](https://github.com/microsoft/MoGe), [MoGe-2論文](https://arxiv.org/abs/2507.02546) | `moge-2-vitb-normal`(104M)を品質優先経路に採用。metric point map・normal・camera intrinsicsを1回の推論で得られ、GTAOのview-space幾何入力として直接使える。単眼推定のためmetric scaleの絶対精度は保証されない。 | MoGe本体MIT。DINOv2部分Apache-2.0。商用制限記載なし。 |
| [Depth Anything V2](https://github.com/DepthAnything/Depth-Anything-V2), [論文](https://arxiv.org/abs/2406.09414) | Smallを旧版(RGB Approximation/External Depthのフォールバック経路)に採用。24.8Mパラメーター、相対Depth。細かな形状と動画の一貫性は保証されない。 | SmallはApache-2.0。Base/Large/GiantはCC-BY-NC-4.0。混同しない。 |
| [Depth Anything V1](https://github.com/LiheYoung/Depth-Anything) | 比較候補として確認。今回の推論はV2 Smallに統一。 | リポジトリ表示はApache-2.0。今回は組み込まない。 |
| [XeGTAO](https://github.com/GameTechDev/XeGTAO) / [GTAO論文](https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf) | `XeGTAO.hlsli`のMainPass閉形積分(Iarc式)を`prototype/gtao.py`へNumPy移植。MoGe-2のmetric point map/normalをview-space幾何として使う。MIPチェーン・TAA・空間デノイザ・Hilbert曲線ノイズは未移植(静止画オフライン用途のため)。 | MIT。実装コードのコピーライト表記を保持。 |
| [HBAO](https://developer.download.nvidia.com/presentations/2008/SIGGRAPH/HBAO_SIG08b.pdf) / SSAO系 | 方向別ホライズン探索を参考に、Cavity向けのCPU実装を新規作成。ランダム回転・時間ジッターは使わない。 | 論文の参照。第三者の実装コードは転載しない。 |
| [Depth Pro](https://github.com/apple/ml-depth-pro) | メトリックDepth候補。今回は小さいモデルでの成立確認を優先し、推論比較は未実施。 | Apple独自LICENSE。コード・重みに同じLICENSEを参照する旨をREADMEで確認。 |
| [Marigold](https://github.com/prs-eth/Marigold) | Depth・Normal・intrinsic推定候補。拡散モデルの推論負担を避け、今回は実行しない。 | コードApache-2.0、モデルRAIL++-M。コードと重みの条件が異なる。 |
| [Intrinsic](https://github.com/compphoto/Intrinsic) | 反射率と照明の分離は影誤検出への有力候補。ただし形状の一意な復元にはならない。 | LICENSEはacademic use only。製品実装には不採用。 |
| [DSINE](https://github.com/baegwangbin/DSINE), [論文](https://arxiv.org/abs/2403.00712) | 単画像Normal推定候補。Normalだけでは距離・遮蔽の情報が足りない。 | 非商用・内部/学術研究用途。商用製品開発研究にも制限があるため不採用。 |
| [OKLab](https://bottosson.github.io/posts/oklab/) | Lと色相を保つChroma増加に採用。sRGB primariesを前提に変換。 | 色空間定義を参照。外部ライブラリの組込みなし。 |

比較は資料調査と、MoGe-2+GTAO／Depth-Anything-V2+horizon法／輝度ベースの実画像比較です。他のAIモデルを実行して速度・精度を順位付けしたものではありません。
ライセンス情報は採用判断用の確認記録です。モデルやテスト写真の再配布権を一括で保証するものではありません。

## 3. 実装コードとUI

- `prototype/gtao.py`: **品質優先経路(新)。** MoGe-2推論、`sample()`(bilinear gather)、`gtao()`(GTAO visibility)、`cavity16.png`/`ao.png`/`composite.png`出力。
- `prototype/download_moge.py`: リビジョン固定のMoGe-2 ViT-B(normal)重み取得。TLS検証を維持。
- `prototype/check_gtao.py`: `gtao()`の合成不変条件テスト(平面≒開放、凹みは平面より閉塞、半径を上げても閉塞は減らない、inf/nan混入への耐性)。AI推論不要。
- `prototype/prototype.py`: 旧版(Depth Anything V2 Small)。AI Depth、NumPy版Cavity、OKLab、denoise/cleanup、画像出力。RGB Approximation/External Depthエンジンの検証・フォールバック用に維持。
- `prototype/download_model.py`: リビジョン固定のDepth-Anything-V2-Small重み取得。TLS検証を維持。
- `src/core.h`: SDK非依存のCavity・OKLab・denoise(バイラテラルDepth平滑化)・cleanup(Cavity後処理)。旧版エンジン(RGB Approximation/External Depth用)。
- `src/Sa_aohue.cpp`: AEパラメーター、8/16/32bpc、SmartFX、外部Depth/Cavity。denoise/cleanupを組み込み済み。External Cavity(baked)入力はKernelを完全バイパスするため、MoGe-2+GTAO側の変更でも無改修。
- `src/Sa_aohuePiPL.r`, `Win/Sa_aohue.vcxproj`: AE登録情報とWindowsビルド。
- `tests/`: C++とPythonの比較、SDKを使ったオフラインアダプターテスト。denoise/cleanupは生のCavity/mask関数の外側なので、この比較テストの対象外。

| UI | 意味 |
|---|---|
| Amount | Chroma増加率。100なら最大2倍。ただし実際にはCavityと色域制限で減る。0はCompositeで元画素をコピー。 |
| Radius | フル解像度のピクセル単位の探索半径。1–512。RGB Approximation/External Depth(旧エンジン)では探索半径、Line Artでは二値化後のブラー半径として使う。External Cavityでは焼き込み済みのため無効。 |
| Contrast | `Cavity^(1/Contrast)`。大きい値ほど弱い応答も持ち上がる。External Cavity・Line Artでも適用される。 |
| Detail | Radiusの1/4スケールを混ぜる割合。0は大きなスケールのみ、100は小さなスケールのみ。旧エンジンのみ。 |
| AO/Cavity Preview | Composite選択中はCavityを一時表示。AO/Cavity選択時はその表示を維持。 |
| Output Mode | Composite / AO / Cavity。プレビューも元アルファを維持。 |
| Geometry Source | RGB Approximation / External Depth / External Cavity / Line Art。品質優先(MoGe-2+GTAO)は`External Cavity`、AI/外部レイヤー不要の軽量経路は`Line Art`を使う。 |
| Depth Layer | External Depth/Cavityの入力レイヤー欄。External Depthは赤チャンネルをDepthとして読み、白を手前と解釈。External Cavityは赤チャンネルをCavity値[0,1]としてそのまま読む(Kernelをバイパス)。Line Artでは未使用。 |
| Invert Depth | External DepthではDepth、External Cavityでは`1-Cavity`(黒が閉塞の素材)に切り替える。Line Artでは線検出のDifference-of-Gaussiansのどちら側を線とみなすか切り替える。 |
| Edge Protect | RGB Approximation/External Depth(旧エンジン)では大きなDepth差の寄与を抑える(真の急な凹みも弱くなる場合がある)。**Line Artでは「Line Threshold」として機能する** — DoGの二値化しきい値を0〜0.03の範囲で調整(0で微小な勾配も全て線、100で強い輪郭だけ)。既定50で0.015。 |
| RGB Encoding | sRGB / Linear sRGB。AEプロジェクトに合わせて手動指定。Line ArtもRGB Approximationと同じデコードを経てからLを取る。 |

AEのプリマルチプライ画素をアンプリマルチプライして色を処理し、元アルファで戻します。透明画素、非有限値、負値、SDR色域外のHDR画素はCompositeでそのまま保持します。HDRの彩度強調は今回の対象外です。
チェックアウトした入力の近傍を読むため、SmartFXは全入力を要求します。ROIごとの最小最大正規化は行いません。半径にはdownsampleとpixel aspect ratioを反映します。MFR対応フラグとGPUレンダーフラグは宣言していません。

## 4. ビルドとプロトタイプの実行

必要環境: Windows x64、Visual Studio 2022 C++ v143、Windows SDK 10.0.26100.0、Adobe After Effects SDK 25.6。

この環境では次でビルドします。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

SDKが別の場所なら `-SdkRoot '...SDK root...'` を指定します。スクリプトは導入済みae-plugin-devスキルのビルドヘルパーを使用します。
ヘルパーのない環境では、VS x64 Developer Command Promptで次を実行できます。

```bat
set "AE_SDK_ROOT=C:\path\to\ae25.6_61.64bit.AfterEffectsSDK"
msbuild Win\Sa_aohue.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

出力: `build/Sa_aohue.aex`。C++ランタイムは静的リンク。SDK本体は同梱しません。
PiPLツールは非ASCIIの入力パスに失敗したため、中間ファイルには相対パスを使用しています。vcxprojはCRLFを保持してください。

Python(品質優先、MoGe-2+GTAO):

```powershell
python -m pip install -r prototype/requirements.txt
python prototype/download_moge.py
python prototype/gtao.py input.png --out results/my_image
```

`prototype/requirements.txt`はMoGe(git+MIT)を後から`pip install`し、そのままだと`huggingface-hub>=1.0`が入って`transformers`(旧エンジン用、`<1.0`要求)が壊れるため、直後に`huggingface-hub<1.0,>=0.34.0`へ再固定しています。両エンジンを同一環境で使う場合はこの順序を守ってください。
モデルは `Ruicheng/moge-2-vitb-normal`(104M, MIT)、リビジョン `ca5f0e07ff01d3e5a364c1d954ed12ee1814b368`。CUDA対応PyTorchがあればGPU、なければCPUで動作します。
出力は `cavity16.png`（16-bit, AEのExternal Cavity入力用）、`points.npy`、`ao.png`、`cavity.png`、`composite.png`、`comparison.png`、`depth_preview.png`(Z値の可視化のみ、AE入力には使わない)です。比較シートは全てPNG(可逆)。以前は`.jpg`(PIL既定品質75の非可逆)で保存しており、ブロックノイズの調査中に見つけて直しました(7章参照)。
半径はmetric単位(既定0.5m)。MoGe-2の推定scaleを信頼するため、被写体のスケール感が実際と大きく異なる画像では半径の実感が変わります(7章参照)。
`--slices`(既定9)・`--steps`(既定8)でサンプル数を調整できます。増やすほど滑らかで遅くなります。

Python(旧エンジン、Depth Anything V2 Small。RGB Approximation/External Depthの検証・フォールバック用):

```powershell
python prototype/download_model.py
python prototype/prototype.py input.png --out results/my_image
```

モデルは `depth-anything/Depth-Anything-V2-Small-hf`、リビジョン `5426e4f0f36572d16453bbda7a8389317b1bef99`。
出力は `depth.png`（16-bit）、`depth.npy`、`ao.png`、`cavity.png`、`composite.png`、`comparison.png`、`radius.png` です(比較シートも含め全てPNG)。
Depthの画像サイズは既定で入力と同じです。実験を速くする場合は `--max-size 640`。その場合、AEでも同じサイズの `input.png` を使用してください。
既存Depthを使う場合は `--depth path/to/depth.png` を指定します。Pythonの入力は8-bit sRGB RGBとして読みます。ICC色管理やRGBAの保持はPython版にはありません。

## 5. AEへの導入方法

1. AEをユーザー自身で終了する。
2. `build/Sa_aohue.aex` を `C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\Plug-ins\Sabiasagi\` にコピーする。書込み権限が必要。(この環境では導入・コピー済み。AEは未起動。)
3. AEを起動し、対象レイヤーへ `Sabiasagi > Sa_aohue` を適用する。
4. 品質優先(MoGe-2+GTAO)なら、Pythonで生成した `cavity16.png` を読み込み、同じサイズの素材と同じコンポに置く。データとして解釈し、色変換を避けるためフッテージの「RGBを保持」を有効にする。
5. `Geometry Source=External Cavity`、`Depth Layer=読み込んだcavity16.png` を指定する。必要ならレイヤーは非表示にする。半径はPython側の`--radius`(metric)で既に焼き込み済みなので、AE側のRadius/Detail/Edge Protectは無効(旧エンジン専用)。
6. `AO/Cavity Preview` で閉塞マスクを確認し、Contrastを調整する。Previewを解除し、Amountを調整する。

旧エンジン(RGB Approximation/External Depth)を使う場合は、Pythonで生成した `depth.png` を同様に読み込み、`Geometry Source=External Depth`、`Depth Layer=読み込んだDepth` を指定する。この経路のみRadius/Detail/Edge Protectが効く。

`Line Art`はPython・外部レイヤーどちらも不要です。`Geometry Source=Line Art`を選ぶだけで、元素材から線検出→二値化→ブラーを直接計算します(Depth Layer欄は無視されます)。Radiusでブラー広がり、Invert Depthで検出する線の向きを調整します。

外部レイヤー(Depth/Cavityいずれも)は元素材と同じ画素座標・寸法・タイミングを必要とします。コンポ上のTransformを整列用途に頼らず、必要な変換やエフェクトはプリコンポーズしてください。寸法や取得原点が一致しない場合はエラーになります。
広色域／ACESへの自動変換はありません。sRGB primariesで作業し、線形プロジェクトでは `Linear sRGB` を選択してください。

AEへの起動・終了・プロジェクト操作(レイヤーの実適用、Geometry Source切り替え等)は今回未実施です。プラグイン本体のコピー(Line Art追加後のビルドへ上書き)のみ実施しました。

## 6. テスト結果

実行環境: Python 3.13、PyTorch 2.8.0+cu126、Transformers 4.56.1、huggingface-hub 0.36.2、CUDA利用可能、MSVC v143。

### MoGe-2 + GTAO(新, 品質優先経路)

| 検証 | 結果 |
|---|---|
| 平面(合成、`prototype/check_gtao.py`) | カメラ正対の平面(3D幾何、法線=カメラ方向)で、外周10px除く内側の`visibility`最小0.918・平均0.972。理論上は1.0だが、XeGTAO自体もスクリーン空間近似のバイアスを認めており(`research/XeGTAO.md`の"slight overdarkening"の記述)、この程度の残差は妥当と判断。0への丸め処理はしていない(旧エンジンの平面テストのような厳密な0一致ではない)。 |
| 凹み(合成) | 平面に正規分布状の凹みを加えた場合、中心`visibility`は平面(1.0)より明確に低下(半径0.3mで0.759、半径0.6mで0.688)。半径を上げると同じ凹みをより強く検出することを確認。 |
| inf/nan混入耐性 | point/normalにinf/nanを混入させた画素をmask=falseにしても、出力`visibility`が全画素有限であることを確認。 |
| 実写真比較(`samples/demo04.jpg`, 苔と球体, 512px) | 旧エンジン(Depth-Anything-V2, Radius24)はCavity平均0.0082で苔の質感をほぼ拾えず、球体との接触部のみ薄く反応。MoGe-2+GTAO(半径0.5m)はCavity平均0.267で、苔の凹凸をnormalベースで細かく検出(`results/gtao_demo04/comparison.png` vs `results/legacy_demo04/comparison.png`)。 |
| ブロックノイズ調査 | 目視で球体上に格子状ノイズを確認したが、`cavity16.png`の16-bit生ピクセル値を直接ダンプして調べたところ、隣接ピクセルは連続的に変化しており量子化・格子パターンは見られなかった(結論: データ自体の欠陥ではない可能性が高い)。調査中に本物のバグを発見: 比較シート(`comparison.jpg`等)がPILの既定品質75で非可逆JPEG保存されていた。全比較シートをPNGへ変更済み。MoGe-2 normalへの円形ブラー前処理(`--normal-blur`)、完全に平坦なnormalでも同じ見た目のパターンが再現することを確認済みで、主因ではないと判断(念のためオプションとして残した)。 |
| 速度(512px, RTX、`prototype/gtao.py`) | MoGe-2推論約3.4秒、GTAO計算(9 slices×8 steps, NumPy CPU)約8.9秒。旧エンジンのDepth推定約4.5秒、Cavity計算約6.6秒と同程度〜やや遅い。GPU化・ベクトル化余地は未着手。 |
| 依存関係の衝突 | MoGeを`pip install`すると`huggingface-hub>=1.0`が入り、`transformers`(旧エンジン用)が壊れる。`huggingface-hub<1.0,>=0.34.0`への再固定で両立を確認(`prototype/requirements.txt`)。 |

### 旧エンジン(Depth Anything V2 + horizonホライズン法)

| 検証 | 結果 |
|---|---|
| 初期ホライズン方式 | 黒い模様への反応は輝度方式より少ないが、輪郭ハローが強い。初期画像を `results/demo01/` に保存。 |
| 改善 | 小さい側の片側勾配と、反対方向の閉塞の幾何平均を採用。単独段差の応答を抑制。`results/demo01_refined/`。 |
| 平面・斜面・単独段差 | 既知Depthの合成テストに合格。 |
| Radius | 小凹部の中心はR6で約0.505。大凹部はR6で約0.0237、R32で約0.1266。約5.3倍。 |
| 影／黒模様の平面 | 正しい平面DepthならCavityは0。AI DepthではCavity>0.1の画素が生Cavityで約5.5%（`prototype/check.py`）。denoise+cleanup適用後は約1.65%に減少（平均Cavityも0.0389→0.0221）。RGBからの完全な識別には失敗。 |
| Denoise+Cleanup | ノイズ付き既知Depth(σ=.01)の平坦領域で、生Cavity最大0.186 → denoise+cleanup後0.0013。平面・斜面・単独段差の既存合成テストは不変(境界±2px窓に収まるよう設計)。 |
| OKLab | Python合成テストのL誤差は約1.7e-16、色相誤差は約2.1e-14 rad。8-bit保存後の誤差を意味しない。 |
| C++対Python | Depth 5種類×Radius 4条件、20条件合格。最大Cavity絶対誤差2.47e-5、RGB絶対誤差8.81e-5。float演算の差を許容。 |
| C++ AO時間 | 640×416で約0.29秒、CPU単発。AI推論・色処理・AEオーバーヘッドを含まない。4K実時間性能は未測定。 |
| AI推論時間 | 静止画3枚で約4.3秒／回。各プロセスのモデル読込みを含む。定常推論ベンチマークではない。 |
| AEアダプター | 8/16/32画素、非密なrowbytes、アルファ、Amount=0完全一致、原点をずらした切り出し一致、AO/Cavity相補、Depth寸法不一致、RGB fallback、HDR/NaN保持をオフライン検証。 |
| ビルド | Release x64成功、警告0・エラー0。WindowsのLoadLibrary、EffectMain、PluginDataEntryFunction2、PiPLリソース16000を確認。 |
| AE本体 | 未検証。実ホストのSmartFX取得範囲・ROI・外部レイヤー・UI・32bpc・MFR・動画は別途確認が必要。 |

### Line Art(新, ネイティブ軽量経路)

| 検証 | 結果 |
|---|---|
| テクスチャなし平面 | 直RGBが定数(alphaのみ変化)の合成画素で、Cavity=0(元画素とbyte完全一致)を確認。DoGが真に0になる場合の丸め処理は入れていない。 |
| 実エッジへの反応 | 色付きの段差エッジ(合成)でCavityが非0になることを確認。Amount=0/gamutバイパスと同じ経路なので、グレー(a=b=0)画素ではChroma変化が起きない点に注意(彩度のない画素はChroma boost自体が無効)。 |
| Invert Depth | 同じエッジ画像でInvert ON/OFFの出力が異なることを確認(どちら側の勾配を線とみなすか切り替わる)。 |
| タイル/クロップ一致 | テクスチャなし平面・実エッジ双方でタイル出力とフル出力の該当領域がbyte一致。 |
| discBlur(円盤/Vogel) | 小半径は厳密円盤平均、大半径はVogel螺旋サンプリング(cool_blur移植)に自動切替。C++単体の数値検証はまだ`check_native.py`側に追加していない(`check_ae.cpp`のLine Artテストで間接検証のみ)。 |
| 実写真確認 | `prototype/prototype.py`のOKLab Lと同じ式をPython(scipy `uniform_filter`)で再現し、`samples/demo01.jpg`で線検出→ブラーの見た目を確認(`results/lineart_demo01.jpg`)。**これはC++の`discBlur`ではなくscipyの近似ブラーによる概念確認で、C++実装の数値一致は検証していない。** |

再実行:

```powershell
python prototype/check_gtao.py
python prototype/check.py
powershell -NoProfile -ExecutionPolicy Bypass -File tests/build.ps1
```

`prototype/check_gtao.py` はAIモデル不要(合成幾何のみ)。`prototype/check.py` はAIモデル(旧エンジン)を使用します。`tests/build.ps1` のC++比較・SDKアダプターテストはAI不要です。
結果のJSONは `results/check/metrics.json`、`results/native_metrics.json`、`results/binary_check.json`。
静止画はDepth Anything V2リポジトリの `assets/examples/demo01.jpg`（街路）、`demo03.jpg`（静物画）、`demo04.jpg`（苔と球体）をローカル評価に使用しました。写真の個別再配布条件を確定していないため、製品配布物へ含めないでください。

## 7. 残っている問題

- **ブロックノイズ報告への対応は未完了です。** ユーザー報告(「AIを用いて行うもので全部ブロックノイズがひどすぎる」)を受けて調査しましたが、`cavity16.png`の生ピクセル値ダンプでは格子状の量子化は確認できず、平坦normalでも同じ見た目の模様が出ることも確認しました。見つかった実バグ(比較シートの非可逆JPEG保存)は修正しましたが、それが報告の原因だったかは未確認です。目視確認がチャット上の画像プレビュー経由だったため、表示側の再圧縮の可能性を否定できていません。どのファイル・どの表示経路(AEプレビュー本体か、生成PNGを別ビューアで開いた場合か)で発生するか、具体的な再現手順が要ります。
- MoGe-2のmetric point mapは単眼推定です。「校正された」半径(metric単位)は幾何がGTAOの数式に対して正しい単位系にあるという意味で、実寸そのものが正確とは限りません。被写体スケールの誤認識は半径の実効値に直接影響します。
- GTAOはスクリーンスペース手法である以上、単一視点から見える面しか使えません。thin occluder(細い形状)の過剰遮蔽、片側からしか見えない隅の見逃しは旧エンジンと同じ原理的限界として残ります(XeGTAO自身も"Thin occluder conundrum"として認めている問題)。
- `prototype/gtao.py`のnormalの座標系(camera space前提)はMoGe公式ドキュメントに明記がなく、実写真での視覚的妥当性(6章)で間接確認したのみです。軸符号の取り違えを完全には排除できません。
- GTAOのMIPチェーン・空間/時間デノイザ・Hilbert曲線ノイズは未移植です。静止画を密なサンプリング(既定9 slices×8 steps)で置き換えているため、動画へフレーム毎適用するとちらつく可能性があります(旧エンジンと同じ制約)。
- GTAOの幾何学的ground truthや複数シーンでの精度評価は未実施です。6章の実写真1枚の比較だけで製品品質とは判断しません。
- MoGe-2 ViT-B(104M)は旧エンジンのDepth-Anything-V2-Small(24.8M)より重く、推論+GTAO計算で6章記載の通り旧エンジンと同程度〜やや遅くなります。GTAO計算はNumPy CPU実装でGPU化していません。
- Line Artは幾何・奥行きを一切見ません。輪郭・模様の密度をCavityの代わりに使う擬似AOで、「凹んでいるから暗い」という意味付けはありません。閉塞の実写真的妥当性を検証する対象ではなく、意図してイラスト的な効果です。
- Line ArtのDoGしきい値(±0.004)・カーネル半径(1px/1.6px)は固定値で、素材のコントラストやノイズ量に応じたUI調整はありません。低コントラストの写真では線がほとんど検出されず、逆に高周波ノイズの多い素材では線が過剰検出される可能性があります。
- Line Artの`discBlur`(円盤/Vogel螺旋)はC++単体の数値検証(`check_native.py`相当)をまだ追加していません。`check_ae.cpp`のレンダーパイプライン経由テストのみで、境界条件や大半径での挙動を独立検証していません。Vogel螺旋サンプリングは固定256点なので、極端に大きい半径(数百px)ではboxBlur/SATに比べてわずかに粒状感が残る場合があります(cool_blur自身もこの精度限界からFFTエンジンへ切り替えている領域)。
- RGBの単眼Depthは推測です。平らな模様、影、反射、透明体、細い形状、小凹部では誤る場合があります。denoise+cleanupで平坦・影領域の誤検出は約1/3に減りましたが(6章参照)、静物画の比較でも輪郭沿いの彩度増加は残っています。ゼロにはなりません。
- Bilateral enclosureは輪郭誤検出を減らす代わりに、片側の接触陰影や開いた隅を見逃します。Depthの相対高さと固定スケールによる見た目の指標で、校正された3D AOではありません。denoise/cleanupの窓・閾値(5x5/3x3、dead-band .025、confidence .04-.08)は固定値で、素材ごとの調整UIはありません。
- AO/Cavityの幾何学的ground truthによる精度評価、ユーザー素材での視覚評価は未実施です。テスト写真での改善だけで製品品質とは判断しません。
- Python版は各画像のDepthを個別に正規化します。動画へそのまま適用するとちらつく可能性があります。時間平滑化、光フロー、カット検出、動画Depthモデルは未実装です。
- AE内AI自動推論、GPU AO、Temporal Stability UIは未実装。推定Depthを焼き込む経路を最小構成として採用しました。
- 全入力チェックアウトは大画像でメモリと再計算量が増えます。タイル別halo、ホストの並列処理、GPU化は実測後の課題です。cleanupのため、SmartRenderは出力タイルが画面の一部でも入力全体のCavityマップを毎回計算します(プロキシ/部分タイル描画では既存より再計算量が増える場合があります)。
- HDR画素の彩度変調、任意作業色空間の自動変換は未対応。RGB Encodingは手動指定です。
- RGB Approximationの平滑化幅は描画画素で固定です。プレビュー解像度による見た目の差を厳密には排除していません。
- AEランタイム検証と署名・配布パッケージ・商用ライセンス全体の監査は未完了です。
