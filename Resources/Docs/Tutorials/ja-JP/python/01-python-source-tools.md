# 01: Python ソースツールのパイプライン

前提: 対応する Python Frontend・BindGen・Transpiler package を導入し、`veroscript` を使用可能にします。書込可能なディレクトリに対応する型付き `host_api.py`、`app.py`、`package/models.py`、`package/service.py` を用意し、パス変更は統一します。実行時 binding には設定済み Python SDK Session も必要です。[Python SDK ガイド](../../../../../Extensions/Language/Python/Resources/Docs/User/ja-JP/index.md)を参照してください。

## 1. 宣言を解析する

```powershell
python -m veroscript_python_frontend host_api.py -o generated/host.python-ast.json
```

生成された AST で module、関数、class、source location を確認します。
入力 module は import も実行もされません。

## 2. ホスト契約を生成する

```powershell
python -m veroscript_python_bindgen host_api.py `
  --catalog generated/bindings.json `
  --api-reference generated/host.vs.txt `
  --validate --veroscript veroscript
```

Catalog の関数を、ホストが登録する実際の callable と照合します。Catalog
は登録コードの代わりにはなりません。

## 3. ソースを変換する

```powershell
python -m veroscript_python_transpiler app.py `
  -o generated/app.vs --source-map generated/app.vs.map.json `
  --veroscript veroscript
veroscript --check generated/app.vs
```

意味を保てない Python 機能は静かに近似されず、診断になります。

## 4. project mode を使う

module 間に import 関係がある場合は、すべての入力を同じ project として渡し、
宣言と import 解決を一貫させます。

```powershell
python -m veroscript_python_bindgen package/models.py package/service.py --project-root . --output-dir generated/bindings --validate --veroscript veroscript
python -m veroscript_python_transpiler package/models.py package/service.py --project-root . --output-dir generated/veroscript --veroscript veroscript
```

生成された manifest、source map、出力を対応する組として保持し、異なる入力
集合の artifact を混ぜないでください。runtime ではアプリケーションの管理下で
module を import し、結合した Catalog を一度に登録します。

登録例は設定済み `session`、明示 import した `package.models`/`package.service` の module object `models`/`service`、および `generated/bindings/bindings.json` から読んだ mapping または `generate_binding_project` の結果 `project_bindings` を前提とします。source tool が module を代わりに import することはありません。

```Python
from veroscript_python_bindgen import bind_modules

bind_modules(session, (models, service), project_bindings)
```

この helper は session を変更する前に module の網羅性を確認し、module 境界を
またぐメンバーより先に project の型を登録します。

## Step 5: runtime binding と value を確認する

実行時に登録する enum には `IntEnum` または `IntFlag` を使用します。通常の
`enum.Enum` は拒否されます。enum のメンバー名と整数値は Catalog と完全一致し、
instance method と `staticMethod` の descriptor 種別も一致しなければなりません。

`Value(None)` は script の `null` を表し、`Value.priority_queue("int", [3, 1, 2])`
は priority queue を作成します。nullable でない値への `None` の代入は
`TypeError` です。Catalog と runtime module のずれを検出するため、これらを
host test に追加してください。

## 練習

1. 注釈付き enum を追加し、Catalog の値を確認します。
2. 注釈のない local を追加し、診断を確認します。
3. 関連する二つの module を project mode で処理し、manifest を比較します。
4. 生成した Catalog を Python host に読み込み、登録された実装が生成済み
   signature と一致することを確認します。

スタンドアロン Python artifact が必要な場合は、次に
[Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/ja-JP/aot.md) を参照してください。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/python/01-python-source-tools.md) · [fr-FR](../../fr-FR/python/01-python-source-tools.md) · [ja-JP](01-python-source-tools.md) · [ko-KR](../../ko-KR/python/01-python-source-tools.md) · [zh-CN](../../zh-CN/python/01-python-source-tools.md) · [zh-TW](../../zh-TW/python/01-python-source-tools.md)
