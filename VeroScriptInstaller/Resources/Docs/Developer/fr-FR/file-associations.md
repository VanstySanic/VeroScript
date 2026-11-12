# Implémentation des associations de fichiers

Le comportement public est décrit dans le [guide d’installation](../../User/fr-FR/index.md#file-associations). `Core/FileAssociationModels.cs` déclare `FileAssociationOptions`, `FileAssociationRecord` et la propriété de chaque valeur. `InstallOptions.FileAssociations` initialise toutes les options à false. `InstallerEngine.SupportsFileAssociations` et `GetSupportedFileAssociations(manifest)` exposent l’adaptateur de registre et les capacités du paquet ; l’installation exige Runtime sélectionné, un manifeste Windows et les exécutables/icônes correspondants déclarés.

`Core/FileAssociationRegistration.cs` planifie les changements ; `Core/Platform/IFileAssociationRegistry.cs` sépare l’accès au registre de la planification. L’implémentation Windows écrit les Classes, applications enregistrées et capacités appartenant à l’utilisateur courant. Chaque installation possède ses identifiants de gestionnaire. Elle ne modifie ni les ProgID par défaut des extensions ni `UserChoice` ; `.vspkg` reçoit seulement une entrée DefaultIcon protégée. `Core/Platform/DefaultAppsSettings.cs` demande la page des paramètres ; son succès indique seulement que la page a été demandée.

Le champ facultatif `fileAssociations` du registre d’installation conserve options, propriétaire, valeurs et clés créées. Son absence signifie qu’aucune association n’a été enregistrée. Journalisez l’intention avant application, revérifiez valeur antérieure et marqueur du propriétaire, puis annulez en ordre inverse. Réparation/mise à jour ne doit pas reprendre les modifications ultérieures : les tombstones conservent la perte de propriété. Ne supprimez que les clés possédées vides, en préservant les autres applications et écritures concurrentes.

La commande `install` du Tool de bundle accepte les indicateurs sans valeur `--associate-vs`, `--associate-vsapp`, `--package-icon` et `--choose-defaults` ; le dernier exige au moins l’un des deux premiers. Ils appartiennent au Tool, pas à l’installateur graphique. App maintient la page facultative et `App/Resources/Localization/<locale>/associations.json` ; alignez clés et paramètres dans les six langues et distinguez échec d’ouverture des paramètres et installation réussie.

Lorsque l’exécution est autorisée, vérifiez capacités indisponibles, Runtime désélectionné, propriétaires existants, modifications utilisateur, réparation/mise à jour, application partielle, récupération et désinstallation avec un adaptateur ou une ruche isolée. Validez séparément le shell Windows réel. Les contrôles statiques ne certifient pas les effets du registre, les icônes ni les paramètres des applications par défaut.

Les clés et noms de valeurs du registre sont comparés sans tenir compte de la casse, mais les identifiants de propriétaire conservent leur valeur exacte. Application et annulation s’exécutent hors du thread UI. L’annulation restaure les valeurs avant les fichiers gérés, supprime la notification intermédiaire et avertit le shell lorsque références et chemins des icônes d’origine sont de nouveau utilisables.

<!-- docs-navigation -->

---

[Installer](index.md)

[en-US](../en-US/file-associations.md) · [fr-FR](file-associations.md) · [ja-JP](../ja-JP/file-associations.md) · [ko-KR](../ko-KR/file-associations.md) · [zh-CN](../zh-CN/file-associations.md) · [zh-TW](../zh-TW/file-associations.md)
