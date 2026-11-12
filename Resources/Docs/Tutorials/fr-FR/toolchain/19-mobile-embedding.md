# 19 : Exécuter un script dans une application mobile

Utilisez un SDK mobile correspondant dans une application Android/iOS. L’application doit déjà héberger le CPython cible ou utiliser MAUI .NET 10 ; les paquets desktop ne fournissent pas les binaires natifs mobiles. Ces intégrations sont expérimentales.

[Python](../../../../../Extensions/Language/Python/Resources/Docs/User/fr-FR/mobile.md) · [CSharp](../../../../../Extensions/Language/CSharp/Resources/Docs/User/fr-FR/mobile.md)

1. Suivez le guide de la liaison choisie pour déployer les bibliothèques natives et installer les ressources de bibliothèque standard et de localisation. Terminez l’installation avant de créer une Session.

2. Sur un même thread de travail, chargez les catalogues, choisissez la langue, créez/configurez une Session VM, chargez `import "core.vs"; void main() { print(42); }`, vérifiez chargement et exécution, collectez le callback print et fermez la Session. Renvoyez le `42` collecté au thread UI.

3. Réessayez avec `void main() { int x = ; }` et une langue autre que l’anglais. Affichez les diagnostics et vérifiez la réactivité UI. Restaurez le script valide, imposez des limites d’exécution et validez l’application signée sur chaque cible prise en charge.

[en-US](../../en-US/toolchain/19-mobile-embedding.md) · [fr-FR](19-mobile-embedding.md) · [ja-JP](../../ja-JP/toolchain/19-mobile-embedding.md) · [ko-KR](../../ko-KR/toolchain/19-mobile-embedding.md) · [zh-CN](../../zh-CN/toolchain/19-mobile-embedding.md) · [zh-TW](../../zh-TW/toolchain/19-mobile-embedding.md)

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/toolchain/19-mobile-embedding.md) · [fr-FR](19-mobile-embedding.md) · [ja-JP](../../ja-JP/toolchain/19-mobile-embedding.md) · [ko-KR](../../ko-KR/toolchain/19-mobile-embedding.md) · [zh-CN](../../zh-CN/toolchain/19-mobile-embedding.md) · [zh-TW](../../zh-TW/toolchain/19-mobile-embedding.md)
