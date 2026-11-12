# Python トランスパイラー

`VeroScriptPythonTranspiler` は、厳密に型付けされた Python 3.11 の
サブセットを VeroScript ソースへ変換します。Python フロントエンドの
宣言解析を共有し、Python runtime と VeroScript-to-Python AOT からは独立
しています。

型付き関数、通常の class、整数 enum、注釈付きフィールドと local、
コンストラクター、メソッド、リテラル、呼び出し、代入、条件、`while`、
通常の式を扱います。任意精度整数、動的 import、async、内包表記、
generator、descriptor、property、多重継承、reflection、未注釈 local は
暗黙に変換せず診断にします。

`@dataclass` は拒否されます。自動生成されるコンストラクター、等値比較などの
実行時動作は未実装です。`metaclass` を含むクラスのキーワード引数も、実行時動作が
未実装のため拒否されます。対応するサブセット内でコンストラクターとメソッドを
明示した通常のクラスを使用してください。
組み込みの `str(...)` 変換は、Python の書式化の意味論が未実装のため拒否されます。
現在のモジュールで宣言するか明示的にインポートした、`str` という名前のユーザークラスや関数は引き続き呼び出せます。

クラス本体のフィールド既定値は、不変のスカラーリテラル、対応する純粋な定数式、
既知の整数列挙型のメンバーに限られます。共有コンテナー、オブジェクト、任意の名前参照、
関数呼び出しは拒否されます。
インスタンスの値は明示的な `__init__` で初期化してください。コンテナーは型注釈付きの
ローカル変数に作成してから、インスタンスフィールドへ代入します。

```powershell
python -m veroscript_python_transpiler input.py `
  -o input.vs --source-map input.vs.map.json `
  --veroscript veroscript
```

`--timeout <seconds>` は外部コンパイラーによる検証の時間を制限します。値は有限で
正数でなければなりません。ライブラリ API では両方のオプション型の
`timeout_seconds` で指定できます。

既定では生成ソースを VeroScript compiler で検証します。プロジェクト
モードは相対 import、出力の module layout、source map、
`transpile.manifest.json` をまとめて処理します。API は `transpile`、
`transpile_source`、`transpile_file`、`transpile_files` を提供します。

`as` でプロジェクトのモジュールや宣言を改名するインポートは拒否されます。元の名前を使用してください。
`import typing as t` などのメタデータのインポートは引き続きサポートされます。

## 検証とプロジェクトモード

外部コンパイラによる検証は既定で有効です。別の工程が生成コードの検証を明示的に担当する場合に限り `--no-validate` を使ってください。`--timeout` の既定値は `120.0` 秒で、プロジェクト全体ではなく各外部呼び出しを制限します。ソース、ソースマップとマニフェストは置換前に準備し、書き込みに失敗した場合は復元を試みます。未解決または曖昧なインポートは拒否されます。

```powershell
python -m veroscript_python_transpiler package/models.py package/service.py `
  --project-root . `
  --output-dir generated/veroscript `
  --veroscript veroscript
```

`len(str)` は UTF-8 バイト数ではなく Unicode コードポイント数を返します。補助平面文字と結合文字も個々のコードポイントとして数えます。文字列リテラルの長さには実行時依存はありません。それ以外の宣言済み文字列には線形の Core UTF-8 バッファ補助処理を使い、標準ライブラリの `string.vs` をインポートします。この経路は Python の O(1) 長さ取得と異なり、O(n) の時間と O(n) の一時領域を使います。Core ランタイムが必要で、現在の単独 Python AOT は明示的に拒否します。生成ファイルや既存の `string.vs` でこの依存を隠さないでください。コンテナー長は従来の Core 操作を使います。文字列／コンテナー型を確定できない式は、まず型注釈付きローカル変数へ格納してください。

Python のグローバル、インポートまたはローカル束縛が `print`、`len`、`abs`、`min`、`max` を使い、Core が組み込み関数として横取りする呼び出しは拒否されます。通常のメソッドと宣言済みの `str` は引き続き呼び出せます。既定値のない注釈だけのフィールドは、読み出す前に明示的に初期化してください。Python の属性欠如や完全な確定代入解析は、このサブセットでは扱いません。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
