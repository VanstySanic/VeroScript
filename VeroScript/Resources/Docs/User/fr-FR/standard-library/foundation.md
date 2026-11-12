# Fondations de la bibliothèque standard

<a id="foundation-imports"></a>

Importez core.vs ou les modules nécessaires. std::io::read_* exige io.vs. Les fonctions d’environnement, d’horloge et de console restent fournies directement par le runtime ; un nom qualifié ne remplace pas un import.

parse/try_parse prennent en charge les entiers de largeur fixe, float, double et bool et leurs alias. Les espaces ASCII extérieurs sont ignorés, puis le texte doit correspondre entièrement. Un seul signe est permis ; les non-signés refusent -0. La base explicite (entiers seulement, 2..36) ne supprime aucun préfixe. Les flottants décimaux doivent être finis et représentables ; bool accepte seulement true/false. Un échec de texte ou de plage renvoie false sans modifier la sortie de try_parse ; arguments invalides, limites et annulation restent des erreurs.

Les positions restent des offsets en octets UTF-8. replace utilise des correspondances non chevauchantes et conserve le texte si le motif est vide ; split conserve les parties vides. trim et les conversions de casse suivent ASCII. UTF-8 est strict : NUL, BOM et sauts de ligne sont conservés, sans remplacement des octets invalides. region désigne des surcharges réelles pour buffer/view/readonly_view.

<a id="io-contract"></a>

Seule une fin normale de flux produit un optional vide. Une ligne vide reste présente et la dernière ligne sans saut final est retournée. LF et un CR final sont supprimés ; MaxTextBytes mesure le contenu restant, en comptant les CR internes ; le token laisse son séparateur final. read_line conserve son ancien résultat vide à EOF ; les tokens stricts signalent EOF. read_bytes refuse de dépasser la limite explicite ; zéro autorise seulement un fichier vide. Les emprunts mémoire sont vérifiés. Une écriture échouée peut déjà avoir modifié le fichier ; flush ne garantit pas la persistance.

Les chemins sont analysés lexicalement, sans accès disque : native, posix ou windows. C:a et /a ne sont pas absolus sous Windows, contrairement à un UNC complet ; la sortie utilise /. NUL, UNC incomplet et préfixes de périphérique sont refusés. La normalisation ne prouve pas le confinement. sum_as convertit chaque élément en R avant une addition vérifiée et refuse un tableau vide ; accumulate_as retourne alors sa valeur initiale. R est un type numérique primitif explicite, hors bool. clamp refuse les types implicites mélangés et les bornes inversées ou NaN, et conserve une entrée NaN.

Les fabriques de synchronisation attribuent un rôle privé au canal. send/recv et les changements de capacité génériques sont refusés ; fermeture et requêtes restent disponibles. Le mutex non récursif appartient à l’exécuteur logique ; déverrouillage étranger interdit, auto-essai temporisé false, fermeture au départ du propriétaire. Le sémaphore conserve un compteur et refuse les libérations excessives. synchronized prépare ses instantanés avant publication ; un échec arithmétique conserve la valeur. Aucune garantie matérielle sans verrou.

Les alias de tâche partagent leur état Core. await consomme un résultat unique après la fin du worker ; erreur ou résultat absent/multiple échoue sans réception infinie. await_for renvoie un optional vide seulement lors d’un délai dépassé, sans désactiver la tâche. Un échec avant consommation conserve le résultat. Le groupe valide les capacités nulles, invalides et répétées ; une défaillance partielle conserve les consommations et size indique les éléments restants. when_all conserve le tableau mais consomme les tâches. cancel_all demande toutes les annulations avant nettoyage ; les workers restent détenus jusqu’à leur sortie réelle. Un appel hôte bloqué ne peut pas être interrompu de force.

Les limites hôte MaxTextBytes/MaxReadBytes/MaxSplitParts sont des uint64 par opération ; zéro n’ajoute pas de limite. Les workers les héritent et les traitements natifs utilisent le budget courant par blocs. reverse/rotate/shuffle conservent le multiensemble après interruption ; la voie observée publie une permutation privée sans clone. shuffle refuse une même identité pour tableau et moteur et ne restaure pas les tirages consommés. AOT autonome refuse explicitement les opérations exigeant Core avant toute sortie ; les imports inutilisés restent permis.

Voir [Module de chaîne](string.md#integer-formatting).

setenv(name, "") conserve une valeur vide existante ; unsetenv supprime, même si la variable est absente. try_getenv distingue absence et chaîne vide. Nom non vide, sans = ni NUL ; valeur sans NUL. La casse des noms suit Windows ou POSIX.

## API

```text
// string.vs
T std::strings::parse<T>(string text);
T std::strings::parse<T>(string text, int base);
bool std::strings::try_parse<T>(string text, ref T output);
bool std::strings::try_parse<T>(string text, ref T output, int base);
std::memory::buffer std::strings::encode_utf8(string text);
string std::strings::decode_utf8(region bytes);
bool std::strings::is_valid_utf8(string text);
bool std::strings::is_valid_utf8(region bytes);
// io.vs
optional<string> std::io::try_read_line();
optional<string> std::io::try_read_token();
std::memory::buffer std::io::read_bytes(string path);
std::memory::buffer std::io::read_bytes(string path, int64 max_bytes);
void std::io::write_bytes(string path, region source);
void std::io::append_bytes(string path, region source);
// path.vs: each query has both one- and two-argument overloads
bool std::filesystem::is_absolute(string path);
bool std::filesystem::is_absolute(string path, std::filesystem::path_style style);
string std::filesystem::root_path(string path);
string std::filesystem::root_path(string path, std::filesystem::path_style style);
string std::filesystem::lexically_normal(string path);
string std::filesystem::lexically_normal(string path, std::filesystem::path_style style);
// path_style: native, posix, windows
// numeric.vs / math.vs: R explicit, T inferred
R std::numeric::accumulate_as<R, T>(array<T> values, R initial);
R std::numeric::sum_as<R, T>(array<T> values);
T std::clamp<T>(T value, T low, T high);
// runtime-provided
optional<string> std::environment::try_getenv(string name);
void std::environment::unsetenv(string name);
```

## SDK

```text
C++: FEngineConfig.StandardLibraryLimits;
     VeroScriptSession::SetStandardLibraryLimits(FStandardLibraryLimits);
     VeroScriptVm::SetStandardLibraryLimits(FStandardLibraryLimits);
C:   vs_interop_session_set_stdlib_limits(session, &limits);
     vs_interop_stdlib_limits { struct_size, reserved=0, max_text_bytes,
                                max_read_bytes, max_split_parts };
C#:  VeroScriptSession.SetStandardLibraryLimits(VeroScriptStandardLibraryLimits);
Python: Session.set_stdlib_limits(max_text_bytes=0, max_read_bytes=0, max_split_parts=0);
WebAssembly: runtime.setStandardLibraryLimits(maxTextBytes, maxReadBytes, maxSplitParts);
```

Voir [Module de concurrence — Demande d'annulation](concurrent.md#concurrency-aot-boundary).

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/foundation.md) · [fr-FR](foundation.md) · [ja-JP](../../ja-JP/standard-library/foundation.md) · [ko-KR](../../ko-KR/standard-library/foundation.md) · [zh-CN](../../zh-CN/standard-library/foundation.md) · [zh-TW](../../zh-TW/standard-library/foundation.md)
