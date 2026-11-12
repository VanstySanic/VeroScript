# 20 : Lire et copier un paquet binaire

Vous allez écrire une valeur de quatre octets, la consulter par une vue en lecture seule et conserver un instantané indépendant pendant la croissance du tampon. Installez le Runtime avec sa bibliothèque standard et choisissez VM ou Mixed ; l’AOT autonome ne prend pas cette fonction en charge.

## 1. Enregistrer le programme

Enregistrez ce programme dans `memory-packet.vs`. Allouez huit octets nuls. Écrivez à l’offset 1 en ordre big-endian : le premier octet de l’en-tête vaut 1.

```VeroScript
import "memory.vs";

int main()
{
	auto packet = new std::memory::buffer(8);
	std::memory::write<uint32>(packet, 1, 0x01020304u, std::memory::byte_order::big);
	auto header = packet.readonly_slice(1, 4);
	print(header.get(0));
	assert(std::memory::read<uint32>(header, 0, std::memory::byte_order::big) == 0x01020304u);
	auto snapshot = std::memory::clone(packet);
	packet.resize(16);
	print(snapshot.size());
	print(packet.size());
	return 0;
}
```

## 2. Vérifier et exécuter

```powershell
veroscript --backend=vm --check memory-packet.vs
veroscript --backend=vm memory-packet.vs
```

```text
1
8
16
```

Le programme affiche successivement 1, 8 et 16. La vue partage les octets ; le clone en possède une copie. Le passage à 16 invalide `header`, qui n’est plus consulté. L’instantané conserve huit octets.

## 3. Essayer

Passez lecture et écriture en ordre little : le premier octet devient 4. Remplacez le clone par une affectation : les deux noms partagent le tampon et les deux tailles deviennent 16. Enfin, recréez une vue après redimensionnement et vérifiez ses octets. Ne réutilisez pas la vue expirée.

## 4. Choisir l’opération

Utilisez une vue pour un accès borné temporaire, `clone` ou `to_array` pour copier et `from_array` pour restaurer les octets. Ne persistez pas les adresses natives ou ID d’objet. Pour les tampons et prêts hôtes, consultez le SDK correspondant.

[Référence](../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/memory.md)

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/standard-library/20-managed-memory.md) · [fr-FR](20-managed-memory.md) · [ja-JP](../../ja-JP/standard-library/20-managed-memory.md) · [ko-KR](../../ko-KR/standard-library/20-managed-memory.md) · [zh-CN](../../zh-CN/standard-library/20-managed-memory.md) · [zh-TW](../../zh-TW/standard-library/20-managed-memory.md)
