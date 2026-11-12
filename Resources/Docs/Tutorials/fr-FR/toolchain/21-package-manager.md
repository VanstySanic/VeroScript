# 21: Installer un paquet local

Utilisez un Runtime bureau avec gestion des paquets activée, dans un dossier de leçon vide. Cet exercice reste hors ligne et ne demande aucun registre.

## 1. Créer les fichiers

Enregistrez le manifeste sous library/veroscript-package.json, le module sous library/scripts/math.vs, le projet sous app/veroscript-project.json et son entrée sous app/main.vs. Conservez la relation des dossiers utilisée par la dépendance archive.

### library/veroscript-package.json

```json
{
	"format": "veroscript-package",
	"formatVersion": "0.1.1",
	"name": "demo-math",
	"version": "0.1.1",
	"entries": {
		"modules": {
			"demo/math.vs": "scripts/math.vs"
		}
	}
}
```

### library/scripts/math.vs

```VeroScript
export int answer()
{
	return 42;
}
```

### app/veroscript-project.json

```json
{
	"format": "veroscript-project",
	"formatVersion": "0.1.1",
	"name": "demo-app",
	"version": "0.1.1",
	"dependencies": {
		"demo-math": {
			"version": "0.1.1",
			"archive": "../demo-math-0.1.1.vspkg"
		}
	}
}
```

### app/main.vs

```VeroScript
import "demo/math.vs";
int main()
{
	print(answer());
	return 0;
}
```

## 2. Créer, installer et exécuter

Lancez ces commandes depuis le dossier de leçon. Le dernier script affiche 42 ; install/list/verify affichent auparavant leurs résultats. Le suffixe désigne une distribution d’installation ZIP, non une .vslib.

```sh
veroscript pack ./library --output ./demo-math-0.1.1.vspkg
veroscript install --path ./app --offline
veroscript list --path ./app --json
veroscript verify --path ./app
veroscript --backend=vm --path ./app ./app/main.vs
```

## 3. Observer et modifier

pack ne remplace jamais un fichier existant. Déplacez l’ancienne archive de la leçon sous un autre nom avant de recréer son chemin initial.

Remplacez 42 par 43 dans answer() et recréez l’archive. L’instantané installé reste à 42 jusqu’à une nouvelle installation. --locked détecte une archive modifiée et n’adopte pas silencieusement ses octets. Restaurez ou régénérez le verrou voulu si les sources changent.

## 4. Poursuivre

Installer n’importe pas automatiquement les modules : main.vs importe explicitement demo/math.vs. Une dépendance de compilation ne fait pas partie du déploiement runtime. Ne supprimez pas reçus/verrous et ne modifiez pas les fichiers installés pour mettre à jour.

[Référence](../../../../../VeroScriptPackageManager/Resources/Docs/User/fr-FR/index.md)

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/toolchain/21-package-manager.md) · [fr-FR](21-package-manager.md) · [ja-JP](../../ja-JP/toolchain/21-package-manager.md) · [ko-KR](../../ko-KR/toolchain/21-package-manager.md) · [zh-CN](../../zh-CN/toolchain/21-package-manager.md) · [zh-TW](../../zh-TW/toolchain/21-package-manager.md)
