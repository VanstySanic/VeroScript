# VeroScript C++ modules

C++ embedding, AOT generation, declaration analysis, binding generation and source conversion. These modules share one localized documentation collection.

## Components

| Module | Responsibility |
| --- | --- |
| [VeroScriptCpp](VeroScriptCpp/README.md) | High-level C++ embedding, engine and session APIs over Core. |
| [VeroScriptCppAot](VeroScriptCppAot/README.md) | VeroScript-to-C++ source generation and deterministic source bundles. |
| [VeroScriptCppFrontend](VeroScriptCppFrontend/README.md) | Strict C++ declaration frontend shared by binding generation and source conversion. |
| [VeroScriptCppBindGen](VeroScriptCppBindGen/README.md) | Binding Catalog generation from the C++ declaration model. |
| [VeroScriptCppTranspiler](VeroScriptCppTranspiler/README.md) | Semantics-preserving conversion of the supported C++ subset to VeroScript. |

## Documentation

[Documentation index](Resources/Docs/README.md)

| Language | User | Developer |
| --- | --- | --- |
| en-US | [User guide](Resources/Docs/User/en-US/index.md) | [Developer guide](Resources/Docs/Developer/en-US/index.md) |
| fr-FR | [Guide utilisateur](Resources/Docs/User/fr-FR/index.md) | [Guide de développement](Resources/Docs/Developer/fr-FR/index.md) |
| ja-JP | [ユーザーガイド](Resources/Docs/User/ja-JP/index.md) | [開発者ガイド](Resources/Docs/Developer/ja-JP/index.md) |
| ko-KR | [사용자 안내](Resources/Docs/User/ko-KR/index.md) | [개발자 안내](Resources/Docs/Developer/ko-KR/index.md) |
| zh-CN | [用户文档](Resources/Docs/User/zh-CN/index.md) | [开发文档](Resources/Docs/Developer/zh-CN/index.md) |
| zh-TW | [使用者文件](Resources/Docs/User/zh-TW/index.md) | [開發文件](Resources/Docs/Developer/zh-TW/index.md) |

## License

[MIT license](../../../LICENSE) · [Third-party notices](../../../Resources/Licenses/README.md)
