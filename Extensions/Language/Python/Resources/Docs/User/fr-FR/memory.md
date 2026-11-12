# Mémoire gérée depuis l’hôte

Voir [Propriété mémoire commune aux hôtes](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#host-memory-ownership).

Vérifiez `RuntimeCapabilities.MEMORY`. Les entiers Python conservent toute la précision des ID. `wrap_external` exige un buffer Python contigu ; libérez chaque `memoryview` exportée avant de fermer le prêt. Les gestionnaires de contexte imbriqués de l’exemple garantissent cet ordre. `current_memory()` n’est valide que dans un callback enregistré ; `argument_address(index)` interroge sa référence d’origine. Les objets Value possèdent leurs handles natifs et les libèrent à leur collecte.

## Quotas et propriété

Voir [Propriété mémoire commune aux hôtes](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#host-memory-ownership).

## Exemple

```python
from veroscript import Backend, ByteOrder, Session

with Session(backend=Backend.VM) as session:
	session.set_memory_limits(16 * 1024 * 1024, 32 * 1024 * 1024)
	memory = session.memory
	packet = memory.buffer(8)
	memory.write(packet, 1, "uint32", 0x01020304, ByteOrder.BIG)
	header = memory.slice(packet, 1, 4, read_only=True)
	assert memory.read(header, 0, "uint32", ByteOrder.BIG) == 0x01020304
	with memory.acquire_lease(packet, write=True) as lease:
		with memoryview(lease) as bytes_view:
			bytes_view[0] = 7
	assert memory.read(packet, 0, "uint8") == 7
	snapshot = memory.clone(packet)
	memory.resize(packet, 16)
	assert memory.size(snapshot) == 8
	print(memory.read(snapshot, 0, "uint8"))
```

## Prêts mémoire et stockage externe

Voir [Propriété mémoire commune aux hôtes](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#host-memory-ownership).

## Requêtes et callbacks

Voir [Propriété mémoire commune aux hôtes](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#host-memory-ownership).

[Référence](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/memory.md)

<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory.md) · [fr-FR](memory.md) · [ja-JP](../ja-JP/memory.md) · [ko-KR](../ko-KR/memory.md) · [zh-CN](../zh-CN/memory.md) · [zh-TW](../zh-TW/memory.md)
