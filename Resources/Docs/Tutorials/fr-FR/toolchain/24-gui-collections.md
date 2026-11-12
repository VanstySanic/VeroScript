# 24 : Utiliser collections, navigation et dialogues

Ouvrez l’exemple GUI fourni, sélectionnez des éléments, triez une table virtuelle et récupérez le résultat d’un dialogue modal. Il contient 10 000 éléments de liste et lignes de table, avec seulement les lignes visibles matérialisées.

## 1. Préparer

Utilisez un Runtime GUI compatible avec son répertoire `Vm/` et les fournisseurs configurés : plateforme/graphismes `sdl3`, GUI `rmlui`, images `stb`, police/mise en page `font-backend`. Travaillez dans un projet GUI existant dont l’environnement installé fournit ces services ; copier les sources n’installe aucun fournisseur. Conservez Runtime complet et ressources des fournisseurs.

Copiez ces trois fichiers dans le même répertoire de projet inscriptible : [main.vs](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/main.vs), [page.vswml](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/page.vswml) et [page.vswcss](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/page.vswcss). Conservez leurs noms et positions relatives, puis ouvrez un terminal dans ce répertoire.

## 2. Vérifier

Les commandes fixent le délai des processeurs de compilation à 20 secondes ; les limites d’instructions et autres politiques de l’hôte restent applicables. Adaptez ces budgets au projet et à l’hôte. Une vérification réussie renvoie zéro sans ouvrir de fenêtre ni exécuter `main`.

```sh
veroscript --compiler-timeout 20000 --platform-backend sdl3 --graphics-backend sdl3 --gui-backend rmlui --image-backend stb --font-backend font-backend --text-layout-backend font-backend gui-check main.vs
```

## 3. Exécuter et interagir

Prévisualisez l’entrée script avec les mêmes options. Elle appelle `Application.Run()` ; fermez la fenêtre pour quitter. `--frames` est réservé aux entrées de prévisualisation ML ; l’utiliser avec cette entrée `.vs` renvoie une erreur d’utilisation.

```sh
veroscript --compiler-timeout 20000 --platform-backend sdl3 --graphics-backend sdl3 --gui-backend rmlui --image-backend stb --font-backend font-backend --text-layout-backend font-backend gui-preview main.vs
```

Sélectionnez les lignes de ListView/TreeView et naviguez avec les flèches. Changez ComboBox et les pages TabView. Cliquez un en-tête de table : le tri est ordinal UTF-8 et non numérique pour un texte ressemblant à un nombre. Ouvrez File puis le dialogue ; acceptez, annulez ou appuyez sur Échap. Le texte d’état indique le résultat. Le dialogue modal empêche les entrées normales du pointeur/clavier sur l’arrière-plan.

## 4. Observer la virtualisation

Faites défiler les deux vues de 10 000 lignes. Les données logiques restent présentes tandis que les widgets sont recyclés. La sélection appartient à la clé, pas à l’instance recyclée. La hauteur est fixée à 28 unités et le contenu personnalisé doit y tenir. Les hauteurs variables et arbres virtualisés sont exclus.

## 5. Exercice

Modifiez libellés ou état, puis revérifiez. Augmentez le nombre de lignes en gardant `count × rowHeight ≤ 16,777,216` et observez le défilement. Une fabrique personnalisée doit réinitialiser l’état recyclé et utiliser des clés stables, non vides et uniques. Consultez [Collections, navigation et dialogues](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/fr-FR/Reference/Framework.md) pour `SetVirtualSource`, marge de préchargement, possession et sélection.

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/toolchain/24-gui-collections.md) · [fr-FR](24-gui-collections.md) · [ja-JP](../../ja-JP/toolchain/24-gui-collections.md) · [ko-KR](../../ko-KR/toolchain/24-gui-collections.md) · [zh-CN](../../zh-CN/toolchain/24-gui-collections.md) · [zh-TW](../../zh-TW/toolchain/24-gui-collections.md)
