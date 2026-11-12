# C++ からの利用

対応する SDK をリンクし、絶対ルートパスを使います。`Load` は所有権を保持する不変のインストール済みスナップショットを返し、`BuildCoreEnvironment` はモジュール、サービス、実行リゾルバーを準備します。同じ環境をコンパイルと実行に渡してください。インストールだけで宣言をインポートしたり実行を開始したりはしません。Engine/Session が環境を使用すると、省略したバックエンドは環境の既定値と自動候補に従い、明示選択が優先します。

以下を同じディレクトリの CMakeLists.txt と main.cpp に保存します。構成時に CMAKE_PREFIX_PATH へ対応するインストール済み Core SDK を指定し、package-manager-embed ターゲットをビルドします。

```cmake
cmake_minimum_required(VERSION 3.25)
project(PackageManagerEmbed LANGUAGES CXX)
find_package(VeroScript 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptPackageManager 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
add_executable(package-manager-embed main.cpp)
target_link_libraries(package-manager-embed PRIVATE
	VeroScript::VeroScriptPackageManager VeroScript::VeroScriptBytecodeVm)
```

```cpp
#include "VeroScriptPackageManager.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include "VeroScriptBytecodeVmAssembly.h"
#include <iostream>
int main(int argc, char** argv)
{
	if (argc < 3 || argc > 4)
	{
		std::cerr << "Use absolute project root, entry script and optional stdlib directory paths\n";
		return 2;
	}
	using namespace VeroScript::Packages;
	FPackageManager Manager;
	FEnvironmentInput Input;
	Input.Root = argv[1];
	FDiagnostic Error;
	std::shared_ptr<const FInstalledEnvironment> Installed;
	if (!Manager.Load(Input, Installed, Error))
	{
		std::cerr << Error.Code << ": " << Error.Message;
		return 1;
	}
	FCoreEnvironmentOptions Options;
	Options.EntryRoot = argv[1];
	if (argc == 4)
	{
		Options.StdlibSearchPaths = {argv[3]};
	}
	std::shared_ptr<const VeroScript::FModuleEnvironment> Environment;
	if (!Installed->BuildCoreEnvironment(Options, Environment, Error))
	{
		std::cerr << Error.Code << ": " << Error.Message;
		return 1;
	}
	VeroScript::FCompileOptions Compile;
	Compile.ScriptCompileExtensions.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	Compile.ModuleEnvironment = Environment;
	auto Code = VeroScript::VeroScriptCompile::CompileFile(argv[2], Compile);
	if (!Code.bSuccess)
	{
		std::cerr << Code.Error.Message;
		return 1;
	}
	VeroScript::VeroScriptVm Vm(std::cout);
	VeroScript::BytecodeVm::Configure(Vm);
	std::string Failure;
	if (!Vm.SetModuleEnvironment(Environment, Failure))
	{
		std::cerr << Failure;
		return 1;
	}
	if (!Vm.Execute(*Code.Module, VeroScript::ERunMode::Script))
	{
		std::cerr << Vm.GetError().Message;
		return 1;
	}
	return Vm.GetProcessExitCode();
}
```

例はプロジェクトとエントリーの絶対パスに加え、第3引数として標準ライブラリの絶対ディレクトリを受け取ります。core.vs や compiler.vs を import する場合、対応する Core SDK の assets/stdlib を渡してください。BuildCoreEnvironment の前に FCoreEnvironmentOptions.StdlibSearchPaths へ追加してアクセスを許可します。拡張のインストールだけでは標準ライブラリは配備されません。

Combine(base, overlay) は明示的で、ダウンロード、ロック変更、プロセッサー実行を行いません。公開前に BaseBindings/BaseProcessors を合成し、PrepareBindings で必要なネイティブジェネリックを具現化します。その後別の Session レジストリを追加しません。環境変更前に Clear または Session 再作成が必要です。

`entries.native` は `id/path/linkage/roles` を宣言し、解析には `catalog` と `componentVersion` も必要です。`AnalysisOnly` は対象のファクトリを実行しません。静的モジュールにはホストの `StaticExtensions` クエリーと一貫した `ContentIdentity` が必要です。オブジェクト、コールバック、提供者は所有者を保持します。ネイティブ／GUI サービスは信頼するホスト統合として扱います。

LoadDeployment は検証済みオフライン閉包を SDK/キャッシュ/通信なしで開きます。entries.compiler によるソースコンパイルにはコンパイル環境が必要です。RuntimeOnly は登録せず、生成済み可搬バイトコードを実行できます。再読込は新しいスナップショットで休止中の利用者を切替え、実行中 Session は変更しません。

[SDK の例](../../../Examples/Embed/main.cpp) · [C++](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/embedding.md)

## C++ から依存一覧をインストール

既存のホスト関数内で `FRequest.RequirementFiles` を指定し `Execute` を呼びます。パスは対象プロジェクトのルートではなく `Environment.WorkingDirectory` が基準で、入れ子のファイルとローカル配布物は記述元のファイルが基準です。Install 専用です。次の断片は一覧ガイドの有効なローカル例と requirements.txt を前提とします。二つの絶対パスを置き換えてください。

```cpp
using namespace VeroScript::Packages;
FRequest Request;
Request.Operation = EOperation::Install;
Request.Environment.WorkingDirectory = "/absolute/workspace";
Request.Environment.Root = "/absolute/workspace/app";
Request.Options.Offline = true;
Request.RequirementFiles = {"requirements.txt"};
FResult Result;
FDiagnostic Error;
if (!FPackageManager{}.Execute(Request, Result, Error))
{
	std::cerr << Error.Code << ": " << Error.Message << " " << Error.Field;
}
```

## VM とインストール済みコンパイラーの方針

この例は静的 `VeroScriptBytecodeVm` SDK を明示的にリンクして選択します。インストール済み VM には `FInstalledEnvironment::LoadVmBackend(id, factory, error)` を使い、成功後に実行ホストへファクトリーを渡します。インストールだけでは生の Core VM に選択されません。実行中はファクトリーと環境所有者を保持してください。

`FCoreEnvironmentOptions.ScriptCompileExtensions` はインストール済み拡張の信頼するホスト方針です。ルートは `entries.compiler` から取得し、呼び出し側ルートで置換しません。Core は各コンパイル要求と組み合わせ、より厳しい有限予算と許可ルートの共通範囲を使います。コンパイルスクリプト用 VM はファクトリーかパスの片方だけを指定します。インストールフックは別の管理ドメインで、`FClientOptions.VmBackendFactory`／`VmBackendPath` を使用します。未指定時に製品の静的 VM を使用するのは、そのコンポーネントをビルドした場合だけです。

自動選択・明示選択・無効化の共通規則は次を参照してください： [サービスのバックエンド選択](../../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/ja-JP/index.md) · [en-US](../en-US/embedding.md) · [fr-FR](../fr-FR/embedding.md) · [ja-JP](embedding.md) · [ko-KR](../ko-KR/embedding.md) · [zh-CN](../zh-CN/embedding.md) · [zh-TW](../zh-TW/embedding.md)
