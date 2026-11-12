# Achèvement et durée de vie du programme

Les captures par valeur des callbacks de script suivent les règles de liaison en lecture seule de Core. Pour appeler une méthode native qui modifie une valeur hôte capturée, utilisez un lambda `mutable` ou une capture par référence d’une liaison modifiable. Les captures avec initialisation et les variables statiques locales suivent la sémantique habituelle de Core. Un contexte conservé dans une variable statique locale appartient toujours à sa génération de programme et se ferme lorsque cette génération prend fin.

Network::operation/http::operation/websocket::operation acceptent un on_complete(fn(operation)->void), delegate/lambda/méthode liée. Les racines de génération Core gardent fonction/captures. Une inscription après fin attend encore Dispatch, jamais d’appel immédiat ; l’opération reçue est indépendante et possédée.

poll pilote HostDriven seulement ; dispatch(max_callbacks) s’exécute sur le thread créateur dans les deux modes. Il compte les enregistrements traités : 0 aucun, maximum 1 048 576. Une fin ne garantit pas la mise en queue. Les nouveaux éléments attendent le lot suivant ; pas de récursion sur ce contexte. Échec de callback : échec du script, résultat réseau inchangé, aucun nouvel appel ; la suite des callbacks du lot est sautée, même après erreur d’argument/allocation/native. L’état de dispatch se rétablit. Budgets VM/annulation/délais restent coopératifs et n’interrompent pas un C++ bloquant arbitraire.

I/O ne retient ni VM/ScriptInvoker/Args/ref empruntés. Dispatch emprunte les services courants ; nettoyage hors thread retire les racines au prochain point sûr propriétaire. Le retour d’une fonction de Session persistante conserve les mêmes générations. Fin VM de haut niveau, fermeture Session ou entrée en remplacement/reload invalident racines/contextes et annulent. Échec avant remplacement conserve l’ancien ; rollback de migration ne réanime pas un contexte invalidé : recréez-le. Les références natives externes ne réactivent pas les callbacks ; une inscription/dispatch retirée donne une erreur de génération.

Le callback peut shutdown sans s’attendre lui-même. Les callbacks en queue sont abandonnés, résultats conservés, cycles de capture défaits. Les racines hôtes ordinaires doivent être libérées avant remplacement ; les racines internes sont invalidées activement. Une initialisation persistante échouée révoque avant destruction des temporaires. Erreurs de nettoyage visibles, aucune inscription async pendant nettoyage/destruction. Autres hôtes/AOT exigent leur adaptateur ; aucun await ajouté.

## Exemples

```VeroScript
class CompletionFlag
{
public:
	bool done = false;
};
Network::context context = Network::create_context(false);
CompletionFlag flag = CompletionFlag();
Network::operation operation = Network::resolve(context, "localhost", 80, 5000);
operation.on_complete([flag](Network::operation completed) mutable -> void
{
	assert(completed.done());
	print(completed.message_id());
	flag.done = true;
});
for (int round = 0; round < 10000 && !flag.done; ++round)
{
	context.dispatch(64);
	if (!flag.done)
	{
		std::this_thread::sleep(1);
	}
}
assert(flag.done);
context.shutdown();
```

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/completion.md) · [fr-FR](completion.md) · [ja-JP](../../ja-JP/Reference/completion.md) · [ko-KR](../../ko-KR/Reference/completion.md) · [zh-CN](../../zh-CN/Reference/completion.md) · [zh-TW](../../zh-TW/Reference/completion.md)
