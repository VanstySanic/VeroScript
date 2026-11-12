# 安装器文件关联实现

公开行为统一维护在[安装指南](../../User/zh-CN/index.md#file-associations)。`Core/FileAssociationModels.cs` 声明 `FileAssociationOptions`、`FileAssociationRecord` 及值级所有权记录；`InstallOptions.FileAssociations` 默认全部为 false。`InstallerEngine.SupportsFileAssociations` 与 `GetSupportedFileAssociations(manifest)` 暴露登记适配器与包能力；安装要求已选择 Runtime、Windows manifest 和对应的已声明可执行文件／图标。

`Core/FileAssociationRegistration.cs` 负责规划；`Core/Platform/IFileAssociationRegistry.cs` 分离登记访问与规划。Windows 实现在当前用户下写入 Classes、已注册应用和所拥有的能力项，处理程序身份按安装隔离。不修改扩展名默认 ProgID 或 `UserChoice`；`.vspkg` 只登记受保护的 DefaultIcon。`Core/Platform/DefaultAppsSettings.cs` 的成功仅表示已请求设置页面。

安装记录中的可选 `fileAssociations` 保存选项、所有者身份、值及所创建的键；缺失字段表示无关联记录。应用前记录事务意图，应用时重新检查旧值和所有者标记，回滚按逆序进行。修复／更新不能重新接管后续用户修改，tombstone 保留失去所有权的状态。仅清理所拥有的空键，保留其他应用和并发值。

bundle Tool 的 `install` 接受无值开关 `--associate-vs`、`--associate-vsapp`、`--package-icon`、`--choose-defaults`，最后一个至少要求前两个中的一个。这些是 Tool 参数，不是图形安装器参数。App 负责可选向导页和 `App/Resources/Localization/<locale>/associations.json`；六语言保持键与占位符一致，设置打开失败与安装成功分别报告。

允许执行时，通过隔离的登记适配器／注册表 hive 覆盖能力缺失、取消 Runtime、已有所有者、用户修改、修复／更新、部分应用、恢复和卸载，再单独验证真实 Windows shell。静态文档校验不代表注册表副作用、图标刷新或默认应用 UI 已通过验收。

注册表键与值名比较不区分大小写，所有者标识的值仍须精确匹配。应用与回滚在 UI 线程外执行。回滚先恢复登记值，再恢复受管理文件，抑制中间 shell 通知，待注册表引用和原始图标路径均可用后再通知。

<!-- docs-navigation -->

---

[Installer](index.md)

[en-US](../en-US/file-associations.md) · [fr-FR](../fr-FR/file-associations.md) · [ja-JP](../ja-JP/file-associations.md) · [ko-KR](../ko-KR/file-associations.md) · [zh-CN](file-associations.md) · [zh-TW](../zh-TW/file-associations.md)
