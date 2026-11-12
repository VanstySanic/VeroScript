# 仓库协议

注册源校验归档时须读取 ZIP 根的 `veroscript-package.json`，项目配置不能代替包清单。API 路由、JSON 格式标识及格式版本 `0.1.1` 保持一致。

客户端协议为 0.1.1，使用明确 API 基址、HTTPS、UTF-8 JSON 和一致 sourceId。此文描述仓库需实现的接口，不代表已托管服务。

官方 CLI 的 API 基址为 `https://veroscript.org/api`，逻辑 `sourceId` 为 `veroscript.official`，下列路由拼接到该基址。项目／锁文件来源及显式 API、镜像选项遵循现有选择规则；嵌入宿主自行配置来源。

| 方法 | 路径 | 契约 |
| --- | --- | --- |
| GET | `/search?q=...` | `sourceId + search JSON` |
| GET | `/packages/{name}` | `veroscript-registry-package` |
| GET | `/packages/{name}/versions/{version}` | `veroscript-registry-version` |
| GET | `/artifacts/{id}/download` | `ZIP` |
| POST | `/uploads` | `veroscript-upload` |
| PUT | `{upload.url}` | `ZIP` |
| POST | `/packages/{name}/versions` | `mode=append + sourceId/name/version/dependencies/conflicts/artifacts/idempotencyKey` |

版本对象为 veroscript-registry-version，含共同 dependencies/conflicts 及 artifacts，产物含 id、size、sha256、manifestSha256、target、requires，可选 blocked。包对象为 veroscript-registry-package，含 formatVersion/sourceId/name/versions，可选 withdrawn/blocked 版本状态。上传响应为 veroscript-upload，含 uploadId/method=PUT/url，可选 headers/expiresAt。 产物 ID 为非空 ASCII 字母、数字及 `.`、`_`、`-`，且不能是 `.` 或 `..`；仓库产物统一为 ZIP 归档，不使用 `kind` 字段。

同版只能是一个 portable 产物或互斥目标产物，不得 portable/native 模糊重叠。所有产物的规范名、版本、依赖、冲突相同。sha256 对完整 ZIP 字节计算，manifestSha256 对根 veroscript-package.json 原始字节计算并保留空白；.vspkg 与 .zip 共用协议。

## 同版本追加平台与后端接入

首次发布和后续补平台均使用同一个 `publish` 命令，只需传入新增的 `.vspkg`／`.zip` 文件；完全相同的已有产物无需再次上传。包名、版本、共同依赖和冲突必须一致。已有产物的 ID、字节、SHA-256、清单摘要、大小、目标和要求保持不变。portable 目标与所有目标重叠。操作系统、架构、environment、coreBuild 或共同 ABI 键的不同等值约束可以证明目标互斥；仅最低版本或所需能力不同，不能证明互斥。

后端需要配合以下改动：

1. 保留 `POST /packages/{name}/versions`，要求请求携带 `mode: "append"`。版本不存在时创建，已存在时将上传引用原子追加到版本；请求中的列表是增量，不是替换列表。请求体遵循[发布请求结构](../../../Schemas/veroscript-publication.schema.json)。
2. 校验包所有权、上传字节／清单身份，以及共同依赖和冲突不变。保留已有产物 ID 和全部不可变字段；完全相同的产物复用已有 ID。新增目标要与全部已有及本批产物比较，已封禁产物也参与重叠检查。修改、删除、重叠返回冲突；已封禁或撤回的版本禁止追加。任一冲突拒绝整批提交。
3. 并发检查与插入在同一个事务中完成。不重叠的并发追加可以同时成功；客户端预检不能替代服务端事务校验。
4. 持久化幂等键和规范请求，同键同请求返回原结果，同键不同请求拒绝。上传分配同样遵守此规则。完成的上传引用需保留用于重试；不能要求客户端持久化签名 URL 或认证头。 上传分配可在保持同一 uploadId 的前提下重新签发过期的临时 URL；相同字节的 PUT 重传必须安全幂等。
5. 返回包含全部已有及新增产物的完整版本，并保留稳定 ID，不能只返回本次增量。客户端核对所有曾观察到的产物身份及本次请求的全部产物，同时允许有效且不重叠的并发追加。
6. 官方源版本 GET 仅在版本确实不存在时返回 404，提交后返回最新完整版本。预检和恢复直接访问该源，携带 `Cache-Control: no-cache`，绕过镜像和安装元数据缓存。提交响应丢失时，完整匹配的 GET 可确认成功；否则保留上传引用及原提交请求／幂等键用于重试。
7. 追加后更新 ETag 及 CDN／镜像元数据；已有产物下载身份和字节不变，既有按目标锁定的锁文件继续有效。撤回／封禁是独立的可变状态，不代表允许替换已有产物。

协议版本保持 `0.1.1`，核心、Interop 和 JIT 接口不变。后端需先实现上述追加语义，再启用更新后客户端的实际发布。

错误含 code/message/requestId 和可选结构详情。GET 支持 ETag/If-None-Match 及有界重试；上传/提交 POST 不做盲目 GET 式重试。凭据限于准确主机/路径，恢复记录按逻辑源、API 和产物集合隔离。服务端实现授权、过期、校验、索引、存储、撤回/封禁、镜像同步和审计。

[结构定义](../../../Schemas) · [维护指南](index.md)

内置 HTTP 传输会按每一跳规范化后的实际 URL 重新校验凭据，包括重定向。自定义 `IHttpTransport` 必须在每次实际请求时遵守 `FHttpRequest.Credential`：返回空值时不发送 Authorization，且继续遵守原有的重定向来源／路径限制。仓库明确提供的上传签名认证与仓库凭据解析分别处理。同一路径附带查询参数时仍属于该凭据范围。

若百分号解码后的路径出现独立的 `.` 或 `..` 段（包括编码的分隔符），也不发送解析器提供的凭据；普通编码文件名和 UTF-8 路径仍可正常使用。

官方仓库须将 `vs-` 开头的包名保留给授权的官方发布者，覆盖包创建及后续发布。AsmJit 的规范包名为 `vs-asmjit`，服务后端 ID 为 `asmjit`。保留规则由服务端的所有权和发布授权实施；客户端继续通用名称校验，本地或私有源中的此前缀不证明官方来源。

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md) · [en-US](../en-US/registry-protocol.md) · [fr-FR](../fr-FR/registry-protocol.md) · [ja-JP](../ja-JP/registry-protocol.md) · [ko-KR](../ko-KR/registry-protocol.md) · [zh-CN](registry-protocol.md) · [zh-TW](../zh-TW/registry-protocol.md)
