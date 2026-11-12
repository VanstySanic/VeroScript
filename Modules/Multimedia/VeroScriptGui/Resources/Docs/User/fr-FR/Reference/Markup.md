# VSWML et VSWCSS

> Cette page décrit le chargement natif `FMarkupProgram`. Les imports VS, le schéma des annotations, les builders, les propriétés/événements personnalisés et les fabriques typées sont décrits dans le [guide du framework](Framework.md). Les restrictions ci-dessous concernent cette API native.

Utilisez l’API publique `VeroScriptGuiMarkup.h` pour charger le balisage dans un arbre `FWidget` avec des propriétés typées et des événements routés, puis rattachez-le à un contexte existant. Texte, polices, images, dessin personnalisé et contrôles de base sont pris en charge. Le chargeur natif ne déclare que `Widget` par défaut ; utilisez la compilation VS avec Schema pour la bibliothèque de contrôles et ses propriétés/événements typés, ou fournissez explicitement des fabriques natives. Le format ne nécessite aucun moteur de mise en page ou de rendu particulier.

## Chargement et syntaxe

Lisez l’entrée avec `ReadMarkupFile`, définissez `FMarkupCompileOptions.Read`, puis appelez `FMarkupProgram::Compile` et `Create`. Passez la racine au `FContext` existant. `FMarkupDiagnostic::Format` décrit les erreurs. Les références `src` se résolvent relativement au fichier déclarant, indépendamment du répertoire de travail ; un `FMarkupReader` personnalisé peut employer des identités de modules ou de ressources opaques. `GetDependencies` expose les instantanés immuables des sources directement lues ; Core gère les imports VS transitifs. La compilation ne nécessite ni fenêtre ni GPU. Chaque `Create` produit des widgets et un état script indépendants. Les instances restent valides après la destruction du programme compilé. Rechargement et recompilation sont explicites ; aucune migration automatique d’état n’est fournie.

```cpp
#include "VeroScriptGuiMarkup.h"
#include <utility>

bool LoadMarkupUi(const std::string& Entry,
				  std::shared_ptr<VeroScript::Gui::FWidget>& OutRoot,
				  std::string& Error)
{
	using namespace VeroScript;
	Gui::FMarkupSource Source;
	Gui::FMarkupDiagnostic Diagnostic;
	if (!Gui::ReadMarkupFile({}, Entry, Source, Error))
	{
		return false;
	}
	Gui::FMarkupCompileOptions Options;
	Options.Read = Gui::ReadMarkupFile;
	auto Program = Gui::FMarkupProgram::Compile(Source, Options, Diagnostic);
	if (!Program)
	{
		Error = Diagnostic.Format();
		return false;
	}
	auto Root = Program->Create({}, Diagnostic);
	if (!Root)
	{
		Error = Diagnostic.Format();
		return false;
	}
	OutRoot = std::move(Root);
	Error.clear();
	return true;
}
```

Rattachez la racine renvoyée à un `FContext` existant avec `SetRoot`, puis effectuez un `Update` réussi avant le rendu. La fonction de chargement ne crée ni fenêtre, ni périphérique, ni backend.

Syntaxe XML sensible à la casse : UTF-8, une racine, attributs entre guillemets, commentaires, CDATA et entités XML/numériques. bind: et on: sont intégrés sans xmlns. Tout widget enregistré peut être racine et contenir plusieurs enfants.

La racine accepte plusieurs import/style et au plus un script. Les styles utilisent src ou du contenu intégré. Une racine ordinaire décrit une instance ; component exige type et un template, avec mark/base/id facultatifs. Le nom court du type et son mark sont utilisables comme balises. base accepte un composant ML importé ou une fabrique native ; les cycles sont rejetés.

Les propriétés acceptent leur nom original et leur Mark, sans double source pour la même propriété. id/class restent en minuscules en ML ; leurs valeurs sont sensibles à la casse. Chaque template possède un espace privé d'ID, de styles et de script. Find(componentRoot, id) y accède. Les enfants ajoutés au site d'utilisation conservent leur portée de déclaration. Les sous-arbres internes des fabriques natives sont privés aussi ; AddChild ultérieur n'enregistre pas automatiquement une portée ML.

Un attribut ordinaire fixe une valeur. `bind:gap="{ Count + 1 }"` ou un nom de méthode installe un getter ; `on:pointer-down="{ Count = Count + 1; }"` ou un nom installe un gestionnaire. Les contrôles/propriétés/événements inconnus, le texte brut et les syntaxes non prises en charge produisent un diagnostic.

## Styles et scripts

Sélecteurs : type/Mark, *, classe, ID, composés, descendant, >, listes ; états root, hover, active, focus, focus-within, disabled, checked, read-only. Priorité : défaut → thème → template → utilisation → valeur/getter local. Dans une portée : spécificité, puis ordre source. SetTheme, SetClasses et SetId affectent Update suivant. Set remplace le getter ; Clear révèle le style/défaut courant. Les erreurs ne deviennent pas des valeurs par défaut. La priorité des portées passe avant la spécificité entre portées. `FMarkupProgram::SetTheme` définit ou retire le thème du runtime des instances. Seules les propriétés autorisées pour les styles sont acceptées en CSS. Les propriétés de base exposent des getters ; les schémas GuiProperty distinguent les valeurs simples des propriétés Attribute liables sur les contrôles concrets.

Longueurs : nombre, px/dp, %, auto ; padding : une à quatre longueurs positives ou nulles. Les couleurs sRGB sont converties en RGBA linéaire. Les variables héritent des valeurs calculées du parent et acceptent var/fallback ; cycles, absence et types incorrects sont diagnostiqués avec leur source. L'héritage des variables reste dans un même runtime ML ; la typographie hérite comme décrit ci-dessous, les autres propriétés ordinaires n'adoptent pas les règles HTML. !important, règles @, sélecteurs frères/attributs, animations CSS et RCSS arbitraire sont rejetés. Les états se mettent à jour dans Update ; transmettre annulations, perte de focus et réinitialisations Platform.

Le ML/CSS statique ne dépend que de Gui. Activer VEROSCRIPT_GUI_SCRIPTING=ON, lier VeroScript::GuiScript, configurer FMarkupScriptOptions.Engine et passer MakeMarkupScriptFactory à Create. Le SDK C++ Runtime/Core doit correspondre. Un script intégré déclare les membres de l'état par instance ; une page ordinaire peut utiliser `<script src="logic.vs" type="App::Logic" />`. L'arbre est complet avant la construction du script et l'installation des liaisons ; montage et première mise en page restent à effectuer. Une page reçoit son propre objet d’état. L’état d’un composant dérive de `VS::Gui::Widget` et représente sa racine native existante. Les constructeurs peuvent consulter les `elements` déjà initialisés, mais doivent attendre la première mise à jour pour le montage et la mise en page.

Pour cette API native, l’hôte peut aussi fournir un `IMarkupScriptContext` comme troisième argument de `FMarkupProgram::Create`, à la place d’un bloc script. Ces deux sources de contexte ne se combinent pas ; un composant utilise son propre contexte.

Noms intégrés : self, context, elements ; événements : sender, event, arg0/arg1. Le contexte externe respecte les droits d'accès et le masquage local VS. elements.Find peut être nul, Require échoue si absent. Les méthodes nommées exigent @GuiMethod avec Mark facultatif ; les noms d’annotation GUI et leurs options ignorent la casse ASCII, mais les annotations Core restent sensibles à la casse par défaut. La réflexion `find_arguments(name)` exige toujours le nom exact enregistré ; les valeurs Mark restent sensibles à la casse. Gestionnaires routés void : sans argument, Event, Widget sender, ou les deux. Event conserve les données d'entrée et des références faibles ; les méthodes de propagation/action par défaut ne sont valides que pendant la distribution synchrone.

attribute.vs, std::attribute.get/set et localisation/formatage Core s'utilisent par appels VS ordinaires ; Text et les contrôles de saisie sont disponibles. Les chaînes ML ne deviennent pas implicitement des clés de localisation ; lier le texte à des appels ordinaires de localisation/formatage Core. Les catalogues publiés ModuleEnvironment restent inchangés et sont composés avec Gui. Opérations sur le thread propriétaire ; appeler CollectMarkupScripts avant arrêt si la dernière référence a été libérée ailleurs.

Les fabriques natives doivent retourner des widgets neufs sur le thread propriétaire, descendants compris ; les nœuds déjà initialisés ou associés au ML sont rejetés. Les expressions/blocs de liaison réservent `self`, `context`, `elements`, `sender`, `event`, `arg0`, `arg1` : variables locales, paramètres lambda, variables foreach/catch et alias de type locaux ne peuvent pas les redéclarer.

Les contrôles de base, collections, navigation et dialogues modaux sont disponibles. Le CLI avec GUI fournit `gui-check`, `gui-compile` et `gui-preview` ; consultez le [guide du framework](Framework.md) pour les commandes, les schémas et les fabriques typées. Utilisez les exemples de [compteur](../../../../Examples/Markup/counter.vswml), de [composant](../../../../Examples/Markup/card.vswml) et de [style partagé](../../../../Examples/Markup/layout.vswcss), fournis avec la documentation Resources intégrée. Ne supposez pas qu’une archive SDK fonctionnelle contient un dossier d’exemples.

## Texte, polices, images et dessin

Text est une chaîne UTF-8, liée éventuellement par attribute, jamais interprétée comme RML. Le texte brut ML et le contenu mixte ne sont pas acceptés. Polices, styles du texte, direction et langue héritent du parent. Direction définit la base auto/ltr/rtl de chaque paragraphe ; auto suit son premier caractère fort, avec LTR par défaut. Language agit uniquement sur le façonnage. RmlUi gère les séquences bidirectionnelles en conservant les valeurs UTF-8 et sélections logiques. ML `Direction="auto" Language="ar"` et CSS `direction: auto; language: ar;` désignent les mêmes propriétés ; left/right gardent leur sens physique.

Voir [Contrats du framework GUI — Texte, polices, images et dessin](Framework.md#image-resources).

La disposition par défaut place l’image, le texte puis les widgets enfants ; cet ordre est conservé après modification des propriétés ou reconstruction de l’arbre.

Les getters Image, BackgroundImage et MaskImage renvoient des identités/URL résolues pour les valeurs directes comme liées ; un transfert à un autre module conserve ainsi leur origine.

Voir [Contrats du framework GUI — Texte, polices, images et dessin](Framework.md#compositor-properties).

[Framework](Framework.md)

| Propriété | Nom CSS | Utilisable en style |
| --- | --- | --- |
| Text, Image | text, image | Non |
| FontFamily, FontSize, FontWeight, FontStyle | font-family, font-size, font-weight, font-style | Oui |
| TextColor, TextAlign, WhiteSpace, TextOverflow | color, text-align, white-space, text-overflow | Oui |
| LineHeight, LetterSpacing, Direction, Language | line-height, letter-spacing, direction, language | Oui |
| CornerRadius, ClipContent | border-radius, clip-content | Oui |
| BorderWidth, BorderColor | border-width, border-color | Oui |
| Cursor | cursor | Oui |
| BackgroundImage, MaskImage, BoxShadow | background-image, mask-image, box-shadow | Oui |
| Transform, Filter, BackdropFilter | transform, filter, backdrop-filter | Oui |

Voir [Contrats du framework GUI — Texte, polices, images et dessin](Framework.md#native-resource-services).

Cursor est de type `VS::Gui::Cursor`. En ML, utilisez `Cursor="pointer"` ; en CSS, `cursor: pointer`. La propriété est héritée et accepte les liaisons Attribute. `Auto` choisit le comportement du contrôle et `None` masque le curseur. Voir [Framework](Framework.md) pour les services de pointeur et les notifications de frontière de fenêtre.

Les propriétés personnalisées commencent par `--`. `var(--name, fallback)` utilise la valeur héritée ou le repli, qui peut être imbriqué. Par exemple, ce style fixe un écart de 8 pixels ; retirez `--space` pour obtenir le repli de 4 pixels :

```css
:root
{
	--space: 8px;
	gap: var(--space, 4px);
}
```

<!-- docs-navigation -->

---

[GUI](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/Markup.md) · [fr-FR](Markup.md) · [ja-JP](../../ja-JP/Reference/Markup.md) · [ko-KR](../../ko-KR/Reference/Markup.md) · [zh-CN](../../zh-CN/Reference/Markup.md) · [zh-TW](../../zh-TW/Reference/Markup.md)
