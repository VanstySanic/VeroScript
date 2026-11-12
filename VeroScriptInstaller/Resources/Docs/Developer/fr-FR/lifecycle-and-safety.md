# Cycle de vie et sécurité Installer

## Composants et staging

`Core/` possède validation manifest, bundle archive, package metadata, path safety, platform information, retry et install engine. `App/` possède graphical host/progress localized ; `Tool/` possède bundle/build entry point. Core fonctionne sans graphical app.

Installation stage chaque component, vérifie les fichiers copiés, écrit installation record puis commit transition. Record suit product identity, component root, owned file, manifest fingerprint et runtime PATH mutation.

Avant tout changement PATH/live component directory, engine écrit atomic transaction journal près de chaque affected component root et prend exclusive per-parent-directory transaction lock. Une opération ultérieure install/repair/uninstall/`LoadRecordAsync` récupère prepared transaction interrompue ou termine cleanup de durable committed transaction. Les copies journal sont réconciliées de façon conservative : un crash pendant marker final ne rollback pas un completed commit. Une copie déterministe est durable commit authority et supprimée en dernier ; post-commit backup cleanup est réessayé plus tard sans transformer successful installation en rollback report. Successful rollback est lui aussi terminal avant suppression journal, afin qu’un journal partiellement supprimé ne rejoue pas PATH change après nouvelle installation.

`LoadRecordAsync` n’est donc pas un read-only inspection API : il récupère les transactions près du requested component root avant ouverture de `.veroscript-install.json`. GUI repair/uninstall startup et Tool record loading utilisent ce chemin ; record déplacé temporairement vers backup peut être restauré avant affichage UI. Un installer concurrent actif peut provoquer lock error ; recovery ne contourne pas lock.

Installation vérifie selected destination root et, si fourni, root contenant `ExistingInstallRecordPath` avant old-record load. Cela couvre relocation interrompue réessayée avec autre nouvelle destination. Les états sont `prepared`, `committed`, `rolledback` ; terminal journal est cleanup marker et ne rejoue pas PATH change.

## Limites de sécurité

Destination root ne chevauche ni protected payload ni l’autre. Managed destination/ancestor sont contrôlés pour links. Relative path se résout sous intended root ; traversal, absolute escape, foreign entry et ambiguous ownership échouent fermés. Retried filesystem operation ne rend jamais partial operation successful.

## Réparation, déplacement et désinstallation

Repair exige product version/platform/architecture/manifest fingerprint du record. Relocation valide existing/sibling record, ownership, destination safety et staged path avant move. Uninstall stage d’abord all unowned file, retire owned PATH entry puis commit tous components dans one recoverable transaction. Il supprime uniquement recorded file/owned PATH entry et préserve unowned file. Pre-durable-commit failure rollback ; failed rollback conserve journal/backups pour recovery suivante. Post-durable-commit backup/journal cleanup failure ne signale pas success comme failure.

Ne supprimez jamais manuellement transaction journal, stage directory ou backup durant operation active ou recovery attendue. Rouvrez la même product operation avec original record/root. Si recovery échoue encore, conservez fichiers et inspectez filesystem/PATH error reportée avant retry.

## Couverture de régression

Test suite couvre install, update, relocation, optional-component removal, repair, uninstall, user-file preservation et PATH ownership. Recovery inclut multi-parent journal reconciliation, stale terminal journal, record loading avant GUI startup et relocation retry via old record root. Suivez la [procédure de validation de publication](release-validation.md) pour vérifier le candidat, consigner les résultats et distinguer les tests du moteur des essais manuels de l’interface graphique.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/lifecycle-and-safety.md) · [fr-FR](lifecycle-and-safety.md) · [ja-JP](../ja-JP/lifecycle-and-safety.md) · [ko-KR](../ko-KR/lifecycle-and-safety.md) · [zh-CN](../zh-CN/lifecycle-and-safety.md) · [zh-TW](../zh-TW/lifecycle-and-safety.md)
