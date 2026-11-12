# Backends VM remplaçables

<a id="vm-provider-assembly"></a>

Core possède les programmes, valeurs, cadres logiques, opérations du langage, contrôles et dispatch. La boucle complète appartient à VeroScriptBytecodeVm dans Modules/Execution/VeroScriptBytecodeVm. Son ID est vs-bytecode ; son paquet est vs-bytecode-vm. Core ne sélectionne implicitement ni VM ni JIT. Runtime assemble explicitement la bibliothèque dynamique officielle dans Vm/ ; les produits statiques Interop, Web et mobiles injectent également leur VM explicitement.

## Assemblage statique

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
```

```cpp
#include "VeroScriptBytecodeVm.h"
#include "VeroScriptVm.h"

auto factory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
VeroScript::VeroScriptVm vm(output);
std::string error;
if (!vm.SetVmBackendFactory(factory, error))
{
	throw std::runtime_error(error);
}
```

## Assemblage dynamique

```cpp
if (!vm.SetVmBackendPath("./Vm", error))
{
	throw std::runtime_error(error);
}
```

```text
veroscript --vm-backend vs-bytecode app.vs
veroscript --vm-backend ./custom-vm app.vs
veroscript --vm-backend ./Vm/VeroScriptVmBytecode.dll app.vs
```

Le choix appartient au domaine et se fige au début effectif de l’exécution. Session.Clear() ne le déverrouille pas. Chaque contexte actif possède son instance ; une réentrée synchrone utilise la même instance active. Le backend officiel peut réutiliser une instance entièrement inactive dans le même domaine et thread. Un échec de configuration conserve le choix précédent ; un échec avant entrée reste corrigeable.

Les processeurs de compilation nécessitent aussi FCompileOptions::ScriptCompileExtensions.VmBackendFactory ou .VmBackendPath. Analyse et compilation ordinaires sans exécution de processeurs ne nécessitent aucune VM. Les workers et contextes de destruction isolés héritent du choix et des contrôles existants.

## Compilation autonome et SDK

Le contrat contient uniquement des en-têtes et ne lie aucun binaire Core. Le backend appelle les services de son véritable Core propriétaire. L’hôte lie Core et une VM statique, ou charge une VM dynamique depuis Core. Un SDK combiné peut contenir les deux ; la liaison seule ne les enregistre pas.

```text
cmake --install <build> --component VeroScriptExecutionContract --prefix <sdk>
cmake --install <build> --component VeroScriptBytecodeVmSdk --prefix <sdk>
cmake -S Modules/Execution/VeroScriptBytecodeVm -B <vm-build> -DCMAKE_PREFIX_PATH=<sdk>
```

## Contraintes d’exécution et de distribution

EExecutionBackend::Vm/Mixed/Jit reste une politique distincte ; aucun JIT n’est choisi par défaut. Une VM absente ou insuffisante provoque une erreur, sans répéter la préparation des arguments ni masquer un repli. Limites, annulation, permissions et nettoyage borné partagé conservent la sémantique Core. Un nettoyage incomplet ou un état backend invalide impose une réinitialisation de Session. La version est 0.1.1 ; empreintes publiques, compilateur, STL, CRT, plateforme et dispositions doivent aussi correspondre. Windows utilise un /MD correspondant ; iOS/Web utilisent un assemblage statique. Les tests de bureau ne remplacent pas la validation des outils et appareils mobiles.

La distribution indépendante fournit une archive portable ZIP/tar et un fichier installable `.vspkg` dont le manifeste se trouve à la racine. Installez ce dernier avec `veroscript install ./vs-bytecode-vm-0.1.1-<target>.vspkg --path=<project>`. L’installation enregistre le service `vm` et l’identifiant `vs-bytecode` sans modifier le choix d’un domaine existant. Une bibliothèque extraite peut aussi être sélectionnée par fichier ou répertoire.

## Compilation sans VM

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::VeroScript)
```

```cpp
#include "VeroScriptCompile.h"

auto compiled = VeroScript::VeroScriptCompile::CompileSource(
	"int answer(){return 42;}", "answer.vs");
// compiled.bSuccess / compiled.Error / compiled.Module
```

Les hôtes GUI et les autres hôtes du SDK C++ transmettent leur VM via `FEngineConfig` ; les scripts de balisage utilisent `Gui::FMarkupScriptOptions::Engine`. Les adaptateurs conservent ce choix ; le SDK C++ brut ne choisit pas implicitement de backend. Les tests autonomes des modules du dépôt peuvent fournir explicitement `-DVEROSCRIPT_TEST_VM_LIBRARY=<fichier du backend dynamique correspondant>`.
