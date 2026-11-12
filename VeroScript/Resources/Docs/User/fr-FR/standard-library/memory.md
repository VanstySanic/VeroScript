# Tampons d’octets gérés — std::memory

Importez `memory.vs` ou `core.vs` et utilisez les noms qualifiés `std::memory`. Un tampon contient des données binaires, une vue délimite une partie du tampon et `clone` crée une copie indépendante.

## Tampons et vues

L’affectation d’un tampon partage ses octets sans les copier. Une vue conserve son propriétaire et couvre un décalage et une longueur fixes. `readonly_view` interdit les écritures par cette vue. Une vue vide par défaut est valide. Un changement de taille ou de capacité invalide définitivement les vues existantes, même si le tampon retrouve sa taille précédente. Recréez les vues après ce changement. Un `resize` ou `reserve` sans effet les conserve ; un échec conserve l’état du tampon. Les octets nouvellement exposés sont mis à zéro.

## Opérations sur les octets

Les décalages et longueurs sont des `int64` non négatifs et doivent tenir dans la région choisie. Même une opération de longueur nulle vérifie la région, les limites et le droit d’écriture. `copy` accepte des plages qui se chevauchent. `read<T>` et `write<T>` acceptent les entiers signés/non signés de 8/16/32/64 bits et leurs alias, `float` et `double` ; l’ordre par défaut est `little`. Les décalages non alignés sont acceptés. `from_array` copie un `array<uint8>` (ou `array<unsigned char>`) ; `to_array` renvoie un instantané indépendant.

## Référence des API

Ce bloc décrit les signatures. `region`, `object`, `value`, `source` et `destination` désignent des arguments compatibles ; ce n’est pas un script complet.

```text
bool std::memory::native_addresses_available();
uint64 std::memory::address_of(&value);
uint64 std::memory::object_id(object);
uint64 std::memory::object_address(object);
uint64 std::memory::data_address(region);
void std::memory::copy(destination, int64 offset, source, int64 source_offset, int64 count);
void std::memory::fill(destination, int64 offset, int64 count, uint8 value);
bool std::memory::equal(left, int64 offset, right, int64 right_offset, int64 count);
std::memory::buffer std::memory::clone(region);
std::memory::buffer std::memory::from_array(array<uint8> bytes);
array<uint8> std::memory::to_array(region);
T std::memory::read<T>(region, int64 offset, std::memory::byte_order order = std::memory::byte_order::little);
void std::memory::write<T>(region, int64 offset, T value, std::memory::byte_order order = std::memory::byte_order::little);
```

| Type | Membres |
| --- | --- |
| `buffer` | `new buffer()`, `new buffer(int64 size)`, `size()`, `capacity()`, `get(int64 offset)`, `set(int64 offset, uint8 value)`, `resize(int64 size)`, `reserve(int64 capacity)`, `slice(int64 offset, int64 count)`, `readonly_slice(int64 offset, int64 count)` |
| `view` | `view()`, `size()`, `get`, `set`, `slice`, `readonly_slice` |
| `readonly_view` | `readonly_view()`, `size()`, `get`, `slice` → `readonly_view` |
| `byte_order` | `little = 0`, `big = 1` |

## Requêtes et permissions

`object_id` renvoie une identité `uint64` stable pour un objet partagé vivant dans une instance du runtime : classes, classes hôtes prises en charge, conteneurs partagés, canaux, fonctions et tampons. Un handle partagé nul renvoie 0. Les scalaires, chaînes, structs, vues et références faibles ne sont pas pris en charge. L’ID ne prolonge pas la durée de vie et n’est ni persistant ni comparable entre runtimes.

`address_of(&value)` observe l’emplacement final d’une référence existante, et non l’adresse de sa charge numérique. Les getters et résultats par valeur ne fournissent pas cette cible. `object_address` observe l’objet partagé et `data_address` les octets de la région. Ces nombres transitoires ne peuvent pas être déréférencés dans un script ou conservés comme identifiants durables. Vérifiez d’abord `native_addresses_available()` : la politique hôte peut refuser les adresses et WebAssembly ne fournit pas d’adresses natives. Les identités d’objet et opérations bornées restent disponibles.

## Limites et erreurs

L’hôte configure les quotas par tampon et pour les octets retenus. Une allocation ou un changement structurel refusé conserve l’ancien tampon. L’annulation, le délai ou le budget d’instructions peut interrompre les écritures par lots ; les écritures terminées restent visibles. Une vue périmée échoue ; une vue en lecture seule refuse l’écriture ; un conflit avec un prêt mémoire hôte renvoie Busy. Pour la persistance, copiez les octets : les instantanés d’objet par défaut ne sérialisent pas les handles de région. Les régions ne peuvent pas passer par les captures, arguments, résultats de tâches ou canaux.

## Exécution et API hôtes

Utilisez VM ou Mixed. Les sorties AOT autonomes C++, C# et Python refusent l’utilisation effective des opérations ou types mémoire ; un import inutilisé est permis. Les intégrations mobiles restent expérimentales et refusent les adresses natives par défaut. Le débogueur en pause permet de consulter les métadonnées, sans lecture/écriture mémoire arbitraire par DAP.

## Exemple

```VeroScript
import "memory.vs";

int main()
{
	int value = 42;
	ref int alias = value;
	if (std::memory::native_addresses_available())
	{
		assert(std::memory::address_of(&value) == std::memory::address_of(&alias));
	}

	auto bytes = new std::memory::buffer(8);
	auto sharedBytes = bytes;
	assert(std::memory::object_id(bytes) == std::memory::object_id(sharedBytes));
	std::memory::write<uint32>(bytes, 1, 0x01020304u, std::memory::byte_order::big);
	auto header = bytes.readonly_slice(1, 4);
	assert(header.get(0) == 1u);
	assert(std::memory::read<uint32>(header, 0, std::memory::byte_order::big) == 0x01020304u);
	std::memory::copy(bytes, 2, bytes, 1, 4);
	auto snapshot = std::memory::clone(bytes);
	assert(std::memory::object_id(bytes) != std::memory::object_id(snapshot));
	assert(std::memory::equal(bytes, 0, snapshot, 0, 8));
	bytes.resize(16); // header is now expired.
	assert(bytes.get(8) == 0u && snapshot.size() == 8);
	auto packet = std::memory::from_array(std::memory::to_array(snapshot));
	assert(std::memory::equal(packet, 0, snapshot, 0, 8));
	print("memory example passed");
	return 0;
}
```

[Tutoriel pas à pas](../../../../../../Resources/Docs/Tutorials/fr-FR/standard-library/20-managed-memory.md)

## Références hôtes

- [C++](../../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/embedding.md)
- [C#](../../../../../../Extensions/Language/CSharp/Resources/Docs/User/fr-FR/memory.md)
- [Python](../../../../../../Extensions/Language/Python/Resources/Docs/User/fr-FR/memory.md)
- [Interop](../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md)
- [WebAssembly](../../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/fr-FR/index.md)


<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../../en-US/standard-library/memory.md) · [fr-FR](memory.md) · [ja-JP](../../ja-JP/standard-library/memory.md) · [ko-KR](../../ko-KR/standard-library/memory.md) · [zh-CN](../../zh-CN/standard-library/memory.md) · [zh-TW](../../zh-TW/standard-library/memory.md)
