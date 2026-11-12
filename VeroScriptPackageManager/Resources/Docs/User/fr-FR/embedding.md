# Consommer les environnements en C++

Liez le SDK compatible et utilisez des racines absolues. `Load` renvoie un instantané installé immuable avec baux ; `BuildCoreEnvironment` prépare modules, services et résolveurs d’exécution. Donnez le même environnement à la compilation et à l’exécution. L’installation seule n’importe pas les déclarations et ne lance rien. Quand Engine/Session utilise cet environnement, les choix omis suivent ses valeurs et candidats automatiques ; les choix explicites priment.

Enregistrez les fichiers suivants sous CMakeLists.txt et main.cpp dans le même répertoire. À la configuration, définissez CMAKE_PREFIX_PATH sur le Core SDK installé correspondant, puis compilez la cible package-manager-embed.

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

L’exemple accepte les chemins absolus du projet et de son entrée, puis un troisième argument facultatif : le répertoire absolu de la bibliothèque standard. Pour importer core.vs ou compiler.vs, indiquez assets/stdlib du Core SDK correspondant ; ajoutez-le à FCoreEnvironmentOptions.StdlibSearchPaths avant BuildCoreEnvironment pour en autoriser l’accès. Installer une extension ne déploie pas automatiquement la bibliothèque standard.

Combine(base, overlay) est explicite, sans téléchargement, modification de verrou ni processeur. Fusionnez BaseBindings/BaseProcessors avant publication ; PrepareBindings prépare les types génériques natifs concrets. N’ajoutez pas ensuite un registre Session privé différent. Faites Clear ou créez une Session avant de changer d’environnement.

`entries.native` déclare `id/path/linkage/roles` ; l’analyse exige aussi `catalog` et `componentVersion`. `AnalysisOnly` n’exécute pas les fabriques cibles. Les modules statiques demandent la requête `StaticExtensions` de l’hôte et un `ContentIdentity` cohérent. Objets, rappels et fournisseurs conservent leurs propriétaires. Les services natifs/GUI restent des intégrations hôtes de confiance.

LoadDeployment ouvre une fermeture hors ligne vérifiée sans SDK/cache/réseau. La compilation source entries.compiler exige son environnement de compilation ; RuntimeOnly n’enregistre pas ses processeurs mais peut exécuter le bytecode généré. Le rechargement crée un nouvel instantané et bascule un consommateur inactif, sans modifier les Sessions en cours.

[Exemple SDK](../../../Examples/Embed/main.cpp) · [C++](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/embedding.md)

## Installer une liste via C++

Dans une fonction hôte, renseignez `FRequest.RequirementFiles` puis appelez `Execute`. Les chemins dépendent de `Environment.WorkingDirectory`, pas de la racine projet choisie ; inclusions et distributions locales dépendent de leur fichier. Ce champ est réservé à Install. Le fragment suppose l’exemple local valide et requirements.txt du guide de listes. Remplacez les deux chemins absolus.

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

## VM et politique des compilateurs installés

L’exemple lie et sélectionne explicitement le SDK statique `VeroScriptBytecodeVm`. Pour un interpréteur installé, appelez `FInstalledEnvironment::LoadVmBackend(id, factory, error)` puis, après succès, transmettez la fabrique à l’hôte d’exécution. Installer un backend ne le sélectionne pas pour une VM Core brute. Conservez fabrique et propriétaires d’environnement pendant l’exécution.

`FCoreEnvironmentOptions.ScriptCompileExtensions` définit la politique de confiance des extensions installées. Les racines viennent de `entries.compiler` ; les racines fournies par l’appelant ne remplacent pas ces entrées. Core combine cette politique avec chaque compilation, conserve les budgets finis les plus stricts et intersecte les racines autorisées. Fournissez une fabrique VM ou un chemin, jamais les deux. Les hooks d’installation ont un domaine gouverné distinct, configuré par `FClientOptions.VmBackendFactory`/`VmBackendPath` ; sans choix, ils utilisent la VM statique du produit package-manager uniquement si ce composant a été construit.

Les règles communes de sélection automatique, explicite et désactivée sont décrites dans [Sélection des backends de service](../../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md) · [en-US](../en-US/embedding.md) · [fr-FR](embedding.md) · [ja-JP](../ja-JP/embedding.md) · [ko-KR](../ko-KR/embedding.md) · [zh-CN](../zh-CN/embedding.md) · [zh-TW](../zh-TW/embedding.md)
