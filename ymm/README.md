# Sa_aohue for YMM4

YMM4 用の映像エフェクトです。AE 版の Line Art 検出、反転、マスク出力、OKLab 彩度増幅を移植しています。

YMM4 の DLL があるフォルダと Windows SDK の Direct2D HLSL ヘッダーを指定してビルドします。`YMM4DirPath` は末尾の `\` を含めます。

```powershell
$Ymm4DirPath = 'C:\path\to\YMM4\'
$FxcPath = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe'
$D2DIncludePath = 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um'
dotnet build .\ymm\SaAohueYmm.csproj -c Release "-p:YMM4DirPath=$Ymm4DirPath" "-p:FxcPath=$FxcPath" "-p:D2DIncludePath=$D2DIncludePath"
```

生成された `ymm\bin\Release\net10.0-windows10.0.19041.0\SaAohueYmm.dll` を、YMM4 の `user\plugin\SaAohueYmm\` にコピーして再起動します。YMM4 と同じ .NET 10 SDK が必要です。

半径のぼかしは YMM4/Direct2D の Gaussian Blur を使います。AE 版の 3 回 box blur と同じ広がりを目標に `Radius / sqrt(3)` を渡すため、ピクセル単位の完全一致は保証しません。

検証状況: インストール済み YMM4 の DLL を参照した Release ビルドとシェーダーの座標入力チェックは成功しています。YMM4 上での読み込み・実描画・保存と再読み込みは未検証です。
