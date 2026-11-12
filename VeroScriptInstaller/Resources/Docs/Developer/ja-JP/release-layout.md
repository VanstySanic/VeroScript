# 製品ごとのリリースと独立したバージョン

唯一の[パッケージ化規約](../../../../../Resources/Docs/Standards/ja-JP/PACKAGING.md)に従う。旧配置との互換や変換は提供せず、旧出力と不要候補を清掃して再構築する。製品バージョンは登録情報に従います。

製品優先の `Release/<business-route>/<target>/<version>/` とし、用途で `archives/`、`sdks/`、`packages/`、`installers/`、`symbols/` に分ける。共有包は `any`、WebAssembly SDK は `wasm32`、ネイティブは実 OS/CPU で区別する。`release-index.json` は `indexes/<target>.json` を指し、一製品が共有/ネイティブの複数マニフェストを参照できる。詳細契約は規約を正本とする。

共有レジストリと配布スクリプトは分類パス、形式検査、検証結果の記録を実装します。配布には実ビルドの成功とペイロード・一覧監査の合格が必要です。対象上の実行とインストーラーのライフサイクルは、失敗や未実行も含め別に記録します。純計画や静的検査だけではビルドや実行検証の証拠にはなりません。

所有と未使用が確認された出力・再生成可能な一時データだけを消し、ソース、資格情報、有用なツールとディスクを守る。旧集合は移行せず、新集合更新では引き続きロック、復旧可能コミット、閉包、ハッシュを検証する。文書更新だけではパッケージ化しない。

```text
Release/
  release-index.json
  indexes/<target>.json
  <business-route>/<target>/<version>/
    archives/
    sdks/
    packages/
    installers/
    symbols/
    release-manifest.json
    checksums.txt
```

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

`supportedTargets` は実ペイロードの対象、`consumerTargets` は参照可能な配布対象を示す。wasm32 SDK はネイティブホスト索引から参照できる。共有・原生の合成では実際のエコシステム版を一致させる。

## 計画と実装の入口

リポジトリのルートから実行します。`configuration` は登録済みの版、パス、製品範囲と形式版を読み、計画は同じ経路関数を使って、ビルド、ステージ作成、公開を行わずに生成します。`--target` と `--business` は繰り返し指定できます。`indexSelections` はネイティブと共有の両マニフェストを参照でき、任意項目や Wheel タグの確定には実成果物が必要です。

ソース入力とビルドコマンドは[ビルドとバンドル](build-and-bundle.md)、最終一覧の検証と制限は[公開前検証](release-validation.md)を参照してください。以下はメタデータのみの確認です。規約だけを変更する場合は上記の文書検査までにとどめます。

```text
python Resources/Tools/Packaging/veroscript_business_release.py configuration
python Resources/Tools/Packaging/veroscript_release_plan.py --target win-x64 --business Python --business Resources
```

<a id="release-qualification"></a>

## 一覧と検証記録

対象索引の `complete` は必須製品の一覧がそろうことを表します。`qualificationStatus` は記録済みの検証を集約し、`qualified` には一覧の完全性と必須検査の充足も必要です。実ビルド・整合性検査済みの成果物は、実行・端末検証が失敗・未実行でも配布できます。対象の準備完了を示す前に記録を確認し、記録なしは `not-recorded` と扱います。

製品エントリーは `qualification` schemaVersion 1 を持ち、`manifests[]` のパスと SHA-256 に結び付きます。`--records` は公開済み製品 ID を `checks` を持つオブジェクトに対応付ける JSON オブジェクトです。各検査に実際の `status` と空でない `reason` を記録し、`required` の既定は true、`evidenceSha256` は任意です。成功した `build` と明示的に必須とした `target-runtime` が必要です。`failed`、`not-run` も実結果として記録し、ペイロード変更時は旧記録を無効とします。記録処理は集合ロック内で対象索引とルートのハッシュを更新します。

```text
python Resources/Tools/Packaging/veroscript_qualification.py --repo-root . --release-directory Release --target win-x64 --records .tmp/acceptance-records.json
```

## 既存コレクションの事前確認

公開前に、既存ターゲットインデックスの `requiredBusinesses` が現行の製品登録と完全に一致することを確認します。集合が変わった場合は `.tmp` または `dist` の下に新しい空の専用出力を選び、ソースからコレクションを再構築します。既定の `Release` 出力も同じ確認を受けます。計画プレビューは `win-x86` を含む登録済みの全デスクトップ／モバイルターゲットを対象とし、`--target` で絞り込めます。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/release-layout.md) · [fr-FR](../fr-FR/release-layout.md) · [ja-JP](release-layout.md) · [ko-KR](../ko-KR/release-layout.md) · [zh-CN](../zh-CN/release-layout.md) · [zh-TW](../zh-TW/release-layout.md)
