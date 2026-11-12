# ファイル関連付けの実装

公開動作は[インストールガイド](../../User/ja-JP/index.md#file-associations)で管理します。`Core/FileAssociationModels.cs` は `FileAssociationOptions`、`FileAssociationRecord`、値単位の所有記録を宣言します。`InstallOptions.FileAssociations` の初期値はすべて false です。`InstallerEngine.SupportsFileAssociations` と `GetSupportedFileAssociations(manifest)` はレジストリアダプターとパッケージの機能を公開し、導入には選択済み Runtime、Windows マニフェスト、対応する宣言済み実行ファイル・アイコンが必要です。

`Core/FileAssociationRegistration.cs` が変更を計画し、`Core/Platform/IFileAssociationRegistry.cs` が計画とレジストリアクセスを分離します。Windows 実装は現在のユーザーの Classes、登録アプリ、所有する機能情報に書き込みます。ハンドラー ID はインストールごとに分離します。拡張子の既定 ProgID と `UserChoice` は変更せず、`.vspkg` は保護された DefaultIcon だけを登録します。`Core/Platform/DefaultAppsSettings.cs` の成功は設定ページの要求だけを意味します。

インストール記録の任意フィールド `fileAssociations` は設定、所有者、値、作成したキーを保持し、省略時は関連付け記録なしとして扱います。変更前に意図をジャーナルへ記録し、適用時に以前の値と所有者マーカーを再確認し、逆順でロールバックします。修復・更新は後からのユーザー変更を取り戻さず、tombstone が所有権の喪失を記録します。所有する空のキーだけを削除し、他のアプリや並行書込みを保持します。

バンドル Tool の `install` は値なしフラグ `--associate-vs`、`--associate-vsapp`、`--package-icon`、`--choose-defaults` を受け付け、最後のフラグには最初の二つのうち一つ以上が必要です。これらは Tool 用で、GUI インストーラー引数ではありません。App は任意のウィザードページと `App/Resources/Localization/<locale>/associations.json` を管理します。六言語のキーとプレースホルダーを揃え、設定画面を開けない場合もインストールの成否と区別します。

実行を許可された場合は、利用できない機能、Runtime の選択解除、既存所有者、ユーザー変更、修復・更新、部分適用、回復、削除を分離したレジストリアダプター／ハイブで検証します。実際の Windows シェル動作は別途検証します。静的文書検査だけではレジストリの副作用、アイコン更新、既定アプリ画面を保証できません。

レジストリのキーと値名は大文字・小文字を区別せず、所有者 ID の値は完全一致で扱います。適用とロールバックは UI スレッド外で行います。ロールバックは管理ファイルより先に登録値を戻し、途中のシェル通知を抑制し、参照と元のアイコンパスがともに利用可能になってから通知します。

<!-- docs-navigation -->

---

[Installer](index.md)

[en-US](../en-US/file-associations.md) · [fr-FR](../fr-FR/file-associations.md) · [ja-JP](file-associations.md) · [ko-KR](../ko-KR/file-associations.md) · [zh-CN](../zh-CN/file-associations.md) · [zh-TW](../zh-TW/file-associations.md)
