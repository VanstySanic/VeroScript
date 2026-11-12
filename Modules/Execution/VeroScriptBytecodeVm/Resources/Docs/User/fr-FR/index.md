# VM de bytecode

`VeroScriptBytecodeVm` est l’interpréteur de bytecode, ID `vs-bytecode`, distribution `vs-bytecode-vm`. Le choix d’interpréteur est distinct de la politique `Vm`, `Mixed` ou `Jit`. Un hôte Core/C++ brut n’a pas d’interpréteur implicite.

## Utiliser le Runtime installé

Conservez le Runtime compatible complet, dont `Vm/`. La sélection automatique utilise d’abord les candidats activés de l’environnement installé choisi ; l’interpréteur livré sert de repli produit si aucun candidat VM automatique n’est utilisable. Un choix explicite ou une désactivation prime sans être remplacé par ce repli. Les commandes suivantes choisissent l’ID livré ou un dossier décompressé compatible. Installez le `.vspkg` dans l’environnement choisi avant de sélectionner son ID. L’installation seule ne remplace pas un domaine figé ; les ID personnalisés exigent le support des paquets et l’environnement sélectionné.

```sh
veroscript --vm-backend=vs-bytecode script.vs
veroscript --vm-backend=./custom-vm script.vs
```

## Intégrer le SDK statique

Dans une cible CMake existante nommée `app`, recherchez les deux SDK compatibles et liez les deux cibles. Configurez ensuite chaque VM brute avant exécution. Lier la bibliothèque ne suffit pas. Les hôtes C++ Session/Engine configurent `FEngineConfig` ou l’API explicite correspondante.

```cmake
find_package(VeroScript 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE
	VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
```

```cpp
#include "VeroScriptVm.h"
#include "VeroScriptBytecodeVmAssembly.h"
#include <sstream>

std::ostringstream output;
VeroScript::VeroScriptVm vm(output);
VeroScript::BytecodeVm::Configure(vm);
```

## Sélection et compatibilité

Le domaine fige son interpréteur à l’engagement de l’exécution ; `Clear()` et rechargement de Session conservent ce choix. Un échec de configuration préserve la sélection précédente. Créez une nouvelle Session pour la changer après gel. Les processeurs de compilation script exigent aussi une VM explicite, contrairement à l’analyse/compilation ordinaire sans ces processeurs. Faites correspondre en-têtes SDK, Core, compilateur/runtime, architecture et identité du backend ; un numéro de version identique ne garantit pas la compatibilité binaire. Les produits statiques mobiles/Web assemblent explicitement leur VM sans charger une bibliothèque desktop.

[C++](../../../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/embedding.md) · [CLI](../../../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md)

Les règles communes de sélection automatique, explicite et désactivée sont décrites dans [Sélection des backends de service](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md#backend-selection).

`BytecodeVm::Configure(FEngineConfig&)` renseigne les champs de repli produit. La surcharge Session utilise `SetFallbackVmBackendFactory`, en préservant choix explicites et candidats de l’environnement. En revanche, `Configure(vm)` sur un `VeroScriptVm` brut appelle directement `SetVmBackendFactory` ; cette VM de bas niveau ne résout pas elle-même les candidats installés. Cette distinction s’applique aussi à l’exemple de SDK statique ci-dessus.

<!-- docs-navigation -->

---

[User](../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
