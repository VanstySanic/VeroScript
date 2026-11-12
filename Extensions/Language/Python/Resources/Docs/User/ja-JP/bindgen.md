# Python BindGen

`VeroScriptPythonBindGen` は、対応する型付き Python 宣言から Binding
Catalog v1 JSON を生成します。モジュールを import したりデコレーターを
実行したりせず、静的に解析します。Catalog はホスト API の契約であり、
実行時の実装ではありません。

公開関数、クラス、dataclass、`IntEnum`/`IntFlag`、注釈付きフィールド、コンストラ
クター、インスタンス/静的/クラスメソッド、property を扱います。動的・
未型付けのシグネチャ、可変長引数、既定引数、モジュール global、未対応の
継承、表現できない型は拒否されます。

```powershell
python -m veroscript_python_bindgen host_api.py `
  --module-name host --catalog host.bindings.json `
  --api-reference host.vs.txt --validate --veroscript veroscript
```

`--timeout <seconds>` は外部検証の時間を制限します。値は有限で正数でなければ
なりません。

## プロジェクト単位の生成

```powershell
python -m veroscript_python_bindgen package/models.py package/service.py `
  --project-root . `
  --output-dir generated/bindings `
  --validate `
  --veroscript veroscript
```

`--output-dir` でプロジェクトモードを選択します。入力が 1 個でも使用でき、複数入力では必須です。`--project-root` と `--manifest` はこのモードでのみ有効で、`--manifest` はマニフェストの出力先を変更します。`--catalog`、`--api-reference`、`--module-name` とは併用できません。出力はまとめて準備され、失敗時は可能な範囲で元に戻されます。マニフェストは対応するカタログと参照ファイルと一緒に使用してください。

プロジェクトモードは統合した `bindings.json`、入力ごとの API reference、
`bindgen.manifest.json` を生成します。ライブラリ API には
`generate_bindings`、`generate_bindings_from_source`、
`generate_bindings_from_file`、`generate_binding_project`、
`validate_catalog`、`bind_module`、`bind_modules` があります。binding helper は
アプリケーションが明示的に import したモジュールだけを使用し、BindGen 自体は
解析対象を import または実行しません。`bind_modules` はセッションを変更する前に
プロジェクトの全モジュールが揃っていることを検証します。

bind 時には enum が `IntEnum` または `IntFlag` を継承し、Catalog のメンバー名と
整数値に完全一致する必要があります。Catalog の instance method は instance
method のまま、`staticMethod` は static/class method のままでなければなりません。
不一致は session を変更する前に拒否されます。

複数のモジュールが同名の型を宣言する場合は、`models.Item` のような修飾付き
注釈を使用してください。曖昧な非修飾名は、`Int8` などのスカラー別名と一致
しても拒否されます。現在のモジュールで宣言した型は、プロジェクトの非修飾
別名およびスカラー別名より優先されます。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
