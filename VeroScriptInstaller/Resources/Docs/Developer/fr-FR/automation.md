# Automatisation de l’installateur

Pour les mainteneurs et auteurs d’outils de déploiement. Exécutez Tool depuis les sources avec le SDK .NET 9. Un bundle d’installateur n’est pas une archive Runtime portable. Les opérations graphiques sont décrites dans le [guide d’installation](../../User/fr-FR/index.md).

Ne déplacez pas les fichiers pour relocaliser une installation. Pour déplacer ou mettre à jour de façon sûre, fournissez l'ancien enregistrement à l'outil :

Cet exemple illustre le cycle complet : vérification, installation, déplacement, réparation et désinstallation. Exécutez uniquement les étapes nécessaires. Il sélectionne seulement `runtime` et fonctionne donc avec un bundle préparé avec `--source-runtime` seul. Ajoutez `examples` à `--components` uniquement si le manifeste contient ce composant ; une mise à jour retire les composants optionnels omis. `--root` est la racine du produit ; le composant Runtime se place dans son sous-répertoire `Runtime`.

```powershell
$tool = '.\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj'
$bundle = 'D:\Bundles\runtime-installer-bundle'
dotnet run --project $tool -c Release -- verify --bundle $bundle
dotnet run --project $tool -c Release -- install --bundle $bundle --root 'D:\Apps\VeroScript' --components runtime
dotnet run --project $tool -c Release -- install --bundle $bundle --root 'D:\Apps\VeroScriptMoved' --existing-record 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json' --components runtime
dotnet run --project $tool -c Release -- repair --bundle $bundle --record 'D:\Apps\VeroScriptMoved\Runtime\0.1.1\.veroscript-install.json'
dotnet run --project $tool -c Release -- uninstall --record 'D:\Apps\VeroScriptMoved\Runtime\0.1.1\.veroscript-install.json'
```

Un bundle d'installateur développé contient `manifest.json` et `payload/`; ce n'est pas un ZIP Runtime. `--add-to-path` est un commutateur sans valeur qui doit être fourni explicitement. L'omettre lors d'une mise à jour ou relocalisation retire l'entrée PATH possédée précédemment. Un downgrade est refusé sauf avec `--allow-downgrade`.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/automation.md) · [fr-FR](automation.md) · [ja-JP](../ja-JP/automation.md) · [ko-KR](../ko-KR/automation.md) · [zh-CN](../zh-CN/automation.md) · [zh-TW](../zh-TW/automation.md)
