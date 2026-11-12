# VeroScript

VeroScript is a cross-platform, embeddable scripting language with a compiler, standard library, independent bytecode VM, optional native JIT and command-line tools. It provides C++ embedding, a C11 process ABI, Python and C# bindings, and a WebAssembly SDK.

[Website](https://veroscript.org/) · [Source code](https://github.com/VanstySanic/VeroScript) · [Documentation](Resources/Docs/README.md) · [Releases](#releases)

## Platforms

| Deployment | Target configurations |
| --- | --- |
| Desktop Runtime and SDKs | Windows (x86, x64, ARM64); Linux (x64, ARM64); macOS (x64, ARM64) |
| Mobile embedding | Android and iOS adapters (experimental) |
| Browser and JavaScript hosts | WebAssembly SDK |

Choose the package for your operating system and CPU architecture. Package availability and validation status vary by product and release. See the [platform overview](Resources/Docs/Overview/en-US/index.md#cross-platform-deployment) for the corresponding guides, dependencies and backend capabilities.

## Repository

The runtime, tools, optional services and language/platform/editor integrations are maintained together. Each independently used module has a source README; detailed guides are maintained by the owning module under `Resources/Docs`.

Shared interfaces and their consumers are reviewed together in the same repository.
Runtime, SDK, module and editor products are built and packaged separately; their
versions are maintained in [ReleaseVersions.json](Resources/Tools/Packaging/ReleaseVersions.json).

## Development approach

The vast majority of this project's code was developed with AI assistance. The maintainer is responsible for project maintenance, issue handling, and release decisions.

VeroScript also aims to be a scripting language suited to AI-assisted authoring and rapid iteration, and welcomes experimentation with vibe coding.

## Documentation

[Documentation index](Resources/Docs/README.md)

| Language | Overview | User | Developer |
| --- | --- | --- | --- |
| en-US | [en-US](Resources/Docs/Overview/en-US/index.md) | [User guide](Resources/Docs/User/en-US/index.md) | [Developer guide](Resources/Docs/Developer/en-US/index.md) |
| fr-FR | [fr-FR](Resources/Docs/Overview/fr-FR/index.md) | [Guide utilisateur](Resources/Docs/User/fr-FR/index.md) | [Guide de développement](Resources/Docs/Developer/fr-FR/index.md) |
| ja-JP | [ja-JP](Resources/Docs/Overview/ja-JP/index.md) | [ユーザーガイド](Resources/Docs/User/ja-JP/index.md) | [開発者ガイド](Resources/Docs/Developer/ja-JP/index.md) |
| ko-KR | [ko-KR](Resources/Docs/Overview/ko-KR/index.md) | [사용자 안내](Resources/Docs/User/ko-KR/index.md) | [개발자 안내](Resources/Docs/Developer/ko-KR/index.md) |
| zh-CN | [zh-CN](Resources/Docs/Overview/zh-CN/index.md) | [用户文档](Resources/Docs/User/zh-CN/index.md) | [开发文档](Resources/Docs/Developer/zh-CN/index.md) |
| zh-TW | [zh-TW](Resources/Docs/Overview/zh-TW/index.md) | [使用者文件](Resources/Docs/User/zh-TW/index.md) | [開發文件](Resources/Docs/Developer/zh-TW/index.md) |

[Tutorials](Resources/Docs/Tutorials/README.md) · [Examples](VeroScript/Resources/Scripts/examples/README.md)

## Build and integration

[Project development and contribution standards](Resources/Docs/Standards/README.md) apply only to work on this repository. They add no conditions to the MIT License and are omitted from User documentation and functional releases.

Start with [build and bundle maintenance](VeroScriptInstaller/Resources/Docs/Developer/en-US/build-and-bundle.md), [C++ embedding](Extensions/Language/Cpp/Resources/Docs/User/en-US/embedding.md), or the corresponding language guide above. Module-specific prerequisites, commands, defaults and acceptance boundaries are maintained in those guides.

## Source layout

[Source layout and module ownership](Resources/Docs/source-layout.md) · [Multimedia modules](Resources/Docs/foundation-modules.md) · [Module documentation index](Resources/Docs/README.md#modules--模块)

## Releases

[Release layout](VeroScriptInstaller/Resources/Docs/Developer/en-US/release-layout.md) · [Release validation](VeroScriptInstaller/Resources/Docs/Developer/en-US/release-validation.md) · [Packaging tools](Resources/Tools/Packaging/README.md)

The source repository includes code, public headers, required resources, tests,
examples, documentation, build and packaging scripts, CI workflows, dependency
lockfiles, and the required vendored sources and license notices.

Local scratch files, build outputs, tool caches and generated release collections
are excluded by [.gitignore](.gitignore). Keep experiments under `.tmp/`, builds
under `build/`, and release collections under `Release/`. Publish generated
packages through GitHub Release assets or the relevant package registries.

Before publishing, review `git status --short` and `git diff --cached --stat`.
Ignore rules do not remove files already tracked by Git. Build release artifacts
from the same committed source revision used for release validation.

## License

VeroScript's own code is released under the [MIT License](LICENSE). Commercial use,
modification, and redistribution are permitted. Copies or substantial portions
of the software, including binary distributions, must retain the copyright and
license notice. Modified source does not require a separate change label.

Release packages carry the complete notice in `LICENSE`. A single-file
application may embed the notice and expose it through a license view or
command; an SPDX identifier or a web link alone does not replace the notice.

Third-party components retain their own licenses. See
[third-party notices](Resources/Licenses/README.md).
