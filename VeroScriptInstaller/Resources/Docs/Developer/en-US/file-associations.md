# Installer file-association implementation

Public user behavior is maintained in the [installation guide](../../User/en-US/index.md#file-associations). `Core/FileAssociationModels.cs` declares `FileAssociationOptions`, `FileAssociationRecord` and value-level ownership records. `InstallOptions.FileAssociations` defaults to all false. `InstallerEngine.SupportsFileAssociations` and `GetSupportedFileAssociations(manifest)` expose the selected registry adapter and package capabilities; install requires the selected Runtime, a Windows manifest and the corresponding declared executable/icons.

`Core/FileAssociationRegistration.cs` plans the changes; `Core/Platform/IFileAssociationRegistry.cs` separates registry access from planning. The Windows implementation writes current-user Classes, registered applications and owned capability entries. Handler identities are scoped to the installation. It does not write extension default ProgIDs or `UserChoice`; `.vspkg` receives only a guarded DefaultIcon entry. `Core/Platform/DefaultAppsSettings.cs` requests the settings page, and success means only that the page was requested.

The optional `fileAssociations` installation-record field stores options, owner identity, values and created keys. An absent field means no recorded associations. Journal the intent before applying changes, recheck the previous value and owner marker at application time, and roll back in reverse order. Repair/update must not reclaim later user edits: tombstones retain lost ownership. Prune only empty owned keys, preserving other applications and concurrent values.

The bundle Tool `install` command accepts flag-only `--associate-vs`, `--associate-vsapp`, `--package-icon` and `--choose-defaults`; the last requires at least one of the first two. These are Tool flags, not graphical installer arguments. The App owns the optional wizard page and `App/Resources/Localization/<locale>/associations.json`; retain six-language keys/placeholders and report settings-open failure separately from installation success.

When runtime verification is permitted, cover unavailable capabilities, deselected Runtime, existing owners, user edits, repair/update, partial application, recovery and uninstall through an isolated registry adapter/hive. Verify actual Windows shell behavior separately. Static documentation checks do not certify registry effects, icon refresh or default-app UI.

Registry key and value-name comparisons are case-insensitive, while ownership identifiers retain their exact value. Apply and rollback run off the UI thread. Rollback restores registration values before the managed files, suppresses the intermediate shell notification, and notifies only after both registry references and original icon paths are usable again.

<!-- docs-navigation -->

---

[Installer](index.md)

[en-US](file-associations.md) · [fr-FR](../fr-FR/file-associations.md) · [ja-JP](../ja-JP/file-associations.md) · [ko-KR](../ko-KR/file-associations.md) · [zh-CN](../zh-CN/file-associations.md) · [zh-TW](../zh-TW/file-associations.md)
