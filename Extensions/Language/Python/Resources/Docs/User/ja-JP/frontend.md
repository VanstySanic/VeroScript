# Python フロントエンド

`VeroScriptPythonFrontend` は Python BindGen と Python から VeroScript への
トランスパイラーが共有する静的フロントエンドです。入力モジュールを
import または実行せず、型付き Python の宣言モデルを生成します。

ソース位置、モジュールと import、型付き関数、クラス、dataclass、整数
enum、注釈付きフィールド、引数、デコレーター、ドキュメント、正規化
された型注釈を保持します。モジュールのトップレベル実行、動的または
解決できない型、表現できない構文は診断になります。

```powershell
python -m veroscript_python_frontend input.py -o input.python-ast.json
```

複数ファイルはプロジェクトとして解析できます。

```powershell
python -m veroscript_python_frontend package/models.py package/service.py `
  --project-root . --output-dir generated/frontend
```

各入力の AST と `frontend.manifest.json` を準備してから書き出し、失敗時は
以前の出力の復元を試みます。API は
`parse_source`、`parse_file`、`parse_files` を提供します。構造の取得には
本モジュール、ホスト契約には BindGen、VeroScript 生成には Transpiler を
使用してください。

## 診断のフォールバック

`--lang` で診断言語を選択します。`VERO_LANG` とシステム言語より優先されます。翻訳メッセージがない場合や使用できない場合は英語へフォールバックします。診断コードとソース位置はどの言語でも変わりません。

`--lang` は VeroScript カタログを使うツールメッセージに適用されます。`argparse` のヘルプや引数解析エラー、Python、OS、外部コンパイラーから渡されるテキストは翻訳せず、それぞれの言語設定に従います。

## オプションとプロジェクト出力

単一ファイルモードでは `--module-name` で論理モジュール名を指定し、`--version` でツールのバージョンを表示します。プロジェクトモードはパッケージ構成を保持し、入力の重複、同じモジュール名、`--project-root` 外のファイルを拒否します。`--output-dir` は `-o` または `--module-name` と併用できません。出力は UTF-8 で、置換前にまとめて準備されます。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](../zh-TW/frontend.md)
