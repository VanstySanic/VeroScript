// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as vscode from 'vscode';
import { Catalogs } from './catalog';
import { Attribute, containingElements, decodeAttribute, editorSourceLimit, Markup, parseMarkup, tagAt } from './markup';
import { fileReference } from './paths';
import { GuiMember, names, SchemaIndex } from './schema';
import { parseStyle, styleContext, StyleModel, styleStates } from './style';

interface DocumentModel {
	readonly version: number;
	readonly languageId: string;
	readonly text: string;
	readonly markup?: Markup;
	readonly style?: StyleModel;
}

const markupLanguage = 'veroscript-gui-markup';
const styleLanguage = 'veroscript-gui-style';
const selectors = [{ language: markupLanguage }, { language: styleLanguage }];
const directives: Record<string, readonly string[]> = {
	component: ['type', 'mark', 'base'], template: [], import: ['src'], style: ['src'], script: ['src', 'type']
};

function range(document: vscode.TextDocument, start: number, end: number): vscode.Range {
	return new vscode.Range(document.positionAt(start), document.positionAt(end));
}

function wordRange(document: vscode.TextDocument, position: vscode.Position): vscode.Range {
	const pattern = document.languageId === styleLanguage ? /(?:--)?[A-Za-z_][\w-]*/ : /[A-Za-z_][\w:.-]*/;
	return document.getWordRangeAtPosition(position, pattern) ?? new vscode.Range(position, position);
}

function details(member: GuiMember): vscode.MarkdownString {
	const result = new vscode.MarkdownString().appendCodeblock(`${member.name}: ${member.type}`, 'veroscript');
	if (member.bindable) {
		result.appendMarkdown('\n\n').appendText(vscode.l10n.t('Bindable property.'));
	}
	if (member.style) {
		result.appendMarkdown('\n\n').appendText(vscode.l10n.t('Style-enabled property.'));
	}
	if (member.description) {
		result.appendMarkdown('\n\n').appendText(member.description);
	}
	return result;
}

function xmlText(value: string): string {
	return value.replaceAll('&', '&amp;').replaceAll('"', '&quot;').replaceAll("'", '&apos;').replaceAll('<', '&lt;');
}

export class GuiProviders implements vscode.Disposable {
	constructor(private readonly catalogs: Catalogs, private readonly output: vscode.LogOutputChannel) {
		this.resources.push(
			vscode.languages.registerCompletionItemProvider(selectors, { provideCompletionItems: (document, position, token) => this.complete(document, position, token) }, '<', '/', ':', ' ', '"', "'", '.', '#'),
			vscode.languages.registerHoverProvider(selectors, { provideHover: (document, position, token) => this.hover(document, position, token) }),
			vscode.languages.registerDocumentLinkProvider(selectors, { provideDocumentLinks: (document, token) => token.isCancellationRequested ? [] : this.links(document) }),
			vscode.languages.registerDocumentSymbolProvider(selectors, { provideDocumentSymbols: (document, token) => token.isCancellationRequested ? [] : this.symbols(document) }),
			vscode.languages.registerFoldingRangeProvider(selectors, { provideFoldingRanges: (document, _context, token) => token.isCancellationRequested ? [] : this.folding(document) }),
			vscode.languages.registerDocumentHighlightProvider({ language: markupLanguage }, {
				provideDocumentHighlights: (document, position, token) => token.isCancellationRequested ? [] : this.highlights(document, position)
			}),
			vscode.languages.registerColorProvider(selectors, {
				provideDocumentColors: (document, token) => token.isCancellationRequested ? [] : this.colors(document),
				provideColorPresentations: (color, context) => {
					const channel = (value: number) => Math.round(Math.min(1, Math.max(0, value)) * 255).toString(16).padStart(2, '0');
					const text = '#' + [color.red, color.green, color.blue, ...(color.alpha < 1 ? [color.alpha] : [])].map(channel).join('');
					const result = new vscode.ColorPresentation(text);
					result.textEdit = vscode.TextEdit.replace(context.range, text);
					return [result];
				}
			}),
			vscode.workspace.onDidOpenTextDocument(document => this.schedule(document)),
			vscode.workspace.onDidChangeTextDocument(event => this.schedule(event.document)),
			vscode.workspace.onDidCloseTextDocument(document => {
				clearTimeout(this.timers.get(document.uri.toString()));
				this.timers.delete(document.uri.toString());
				this.diagnostics.delete(document.uri);
			})
		);
		for (const document of vscode.workspace.textDocuments) {
			this.schedule(document);
		}
	}

	dispose(): void {
		for (const timer of this.timers.values()) {
			clearTimeout(timer);
		}
		for (const resource of this.resources) {
			resource.dispose();
		}
		this.diagnostics.dispose();
	}

	private model(document: vscode.TextDocument): DocumentModel {
		const previous = this.models.get(document);
		if (previous?.version === document.version && previous.languageId === document.languageId) return previous;
		const text = document.getText();
		const model: DocumentModel = { text, version: document.version, languageId: document.languageId,
			markup: document.languageId === markupLanguage ? parseMarkup(text) : undefined,
			style: document.languageId === styleLanguage ? parseStyle(text) : undefined };
		this.models.set(document, model);
		return model;
	}

	private schedule(document: vscode.TextDocument): void {
		if (document.languageId !== markupLanguage && document.languageId !== styleLanguage) return;
		const key = document.uri.toString();
		clearTimeout(this.timers.get(key));
		this.timers.set(key, setTimeout(() => {
			this.timers.delete(key);
			if (document.isClosed) return;
			const model = this.model(document);
			const issues = model.markup?.issues ?? (model.text.length > editorSourceLimit
				? [{ message: 'Document exceeds the editor analysis limit.', args: [], start: 0, end: 0 }] : []);
			this.diagnostics.set(document.uri, issues.map(issue => {
				const diagnostic = new vscode.Diagnostic(range(document, issue.start, Math.max(issue.start + 1, issue.end)),
					vscode.l10n.t(issue.message, ...issue.args), vscode.DiagnosticSeverity.Information);
				diagnostic.source = 'VeroScript GUI';
				diagnostic.code = 'gui.editor.structure';
				return diagnostic;
			}));
		}, 200));
	}

	private async complete(document: vscode.TextDocument, position: vscode.Position, token: vscode.CancellationToken): Promise<vscode.CompletionItem[]> {
		if (token.isCancellationRequested) return [];
		const model = this.model(document);
		if (model.text.length > editorSourceLimit) return [];
		const offset = document.offsetAt(position);
		try {
			const index = await this.catalogs.get(document.uri, token);
			if (token.isCancellationRequested || document.isClosed || document.version !== model.version || document.languageId !== model.languageId) return [];
			if (model.style) return this.styleCompletion(document, position, offset, model, index);
			const markup = model.markup!;
			const tag = tagAt(markup, offset);
			if (!tag) {
				if (!model.text.trim()) {
					const item = new vscode.CompletionItem('page', vscode.CompletionItemKind.Snippet);
					item.insertText = new vscode.SnippetString('<Stack id="root" Width="100%" Height="100%">\n\t<Text Text="${1:Hello, VeroScript GUI!}" />\n\t$0\n</Stack>');
					return [item];
				}
				return [];
			}
			if (offset <= tag.nameEnd) {
				const parents = containingElements(markup, tag.start);
				const candidates = tag.closing ? parents.map(element => element.tag.name).reverse()
					: parents.at(-1)?.tag.name === 'component' ? ['template', 'import', 'style', 'script']
						: [...index.widgets().map(widget => widget.tag), ...(parents.length === 1 ? ['import', 'style', 'script'] : []),
							...(parents.length === 0 ? ['component'] : [])];
				const prefix = model.text.slice(tag.nameStart, offset);
				return [...new Set(candidates)].filter(name => name.startsWith(prefix)).map(name => {
					const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Class);
					item.range = range(document, tag.nameStart, tag.nameEnd);
					item.detail = index.widget(name)?.name;
					return item;
				});
			}
			const attribute = tag.attributes.find(attribute => attribute.valueStart !== undefined && offset >= attribute.valueStart && offset <= attribute.valueEnd!);
			if (attribute) {
				if (attribute.name === 'src' || attribute.name === 'Image' || attribute.name === 'image') {
					return await this.pathCompletion(document, offset, attribute, token);
				}
				if (attribute.name.startsWith('bind:') || attribute.name.startsWith('on:')) return [];
				const property = this.properties(index, tag.name).find(property => names(property).includes(attribute.name));
				return (property?.values ?? []).map(value => {
					const item = new vscode.CompletionItem(value, vscode.CompletionItemKind.EnumMember);
					item.range = range(document, attribute.valueStart!, attribute.valueEnd!);
					item.insertText = xmlText(value);
					return item;
				});
			}
			if (tag.closing) return [];
			const replacement = wordRange(document, position);
			const prefix = document.getText(new vscode.Range(replacement.start, position));
			const existing = new Set(tag.attributes.filter(attribute => !(attribute.start <= offset && offset <= attribute.start + attribute.name.length)).map(attribute => attribute.name));
			const members = this.properties(index, tag.name);
			const candidates: { name: string; member?: GuiMember }[] = [];
			if (Object.hasOwn(directives, tag.name)) {
				candidates.push(...directives[tag.name]!.map(name => ({ name })));
			}
			else
			{
				candidates.push({ name: 'id' }, { name: 'class' });
				for (const property of members) {
					const aliases = names(property);
					if (aliases.some(name => existing.has(name) || existing.has(`bind:${name}`))) {
						continue;
					}
					candidates.push(...aliases.map(name => ({ name, member: property })));
					if (property.bindable) {
						candidates.push(...aliases.map(name => ({ name: `bind:${name}`, member: property })));
					}
				}
				for (const event of index.events(tag.name)) {
					const aliases = names(event);
					if (aliases.some(name => existing.has(`on:${name}`))) {
						continue;
					}
					candidates.push(...aliases.map(name => ({ name: `on:${name}`, member: event })));
				}
			}
			const hasValue = /^\s*=/.test(model.text.slice(document.offsetAt(replacement.end)));
			return candidates.filter(candidate => candidate.name.startsWith(prefix) && !existing.has(candidate.name)).map(candidate => {
				const item = new vscode.CompletionItem(candidate.name, vscode.CompletionItemKind.Property);
				item.range = replacement;
				const expression = /^(bind|on):/.test(candidate.name);
				item.insertText = hasValue ? candidate.name
					: new vscode.SnippetString().appendText(candidate.name + (expression ? '="{ ' : '="')).appendTabstop(1).appendText(expression ? ' }"' : '"');
				if (candidate.member) {
					item.detail = candidate.member.type;
					item.documentation = details(candidate.member);
				}
				return item;
			});
		}
		catch (error)
		{
			if (!(error instanceof vscode.CancellationError)) {
				this.output.error(String(error));
			}
			return [];
		}
	}

	private properties(index: SchemaIndex, tag: string): readonly GuiMember[] {
		return index.properties(index.widget(tag) ? tag : 'Widget');
	}

	private styleCompletion(document: vscode.TextDocument, position: vscode.Position, offset: number, model: DocumentModel, index: SchemaIndex): vscode.CompletionItem[] {
		const context = styleContext(model.style!, model.text, offset);
		if (!context) return [];
		const replacement = context.kind === 'value'
			? document.getWordRangeAtPosition(position, /[^\s;{}():"',]+/) ?? new vscode.Range(position, position)
			: wordRange(document, position);
		const prefix = document.getText(new vscode.Range(replacement.start, position));
		const properties = index.styleProperties();
		const preceding = model.text[document.offsetAt(replacement.start) - 1];
		if (context.kind === 'selector' && (preceding === '.' || preceding === '#')) return [];
		const afterColon = preceding === ':';
		const hasValue = /^\s*:/.test(model.text.slice(document.offsetAt(replacement.end)));
		const candidates = context.kind === 'property' ? properties.map(property => ({ label: property.mark ?? property.name, property }))
			: context.kind === 'value' ? (properties.find(property => names(property).includes(context.property!))?.values ?? []).map(label => ({ label }))
				: afterColon ? styleStates.map(label => ({ label }))
					: [...index.widgets().map(widget => ({ label: widget.tag })), ...styleStates.map(label => ({ label: ':' + label }))];
		return candidates.filter(candidate => candidate.label.startsWith(prefix)).map(candidate => {
			const item = new vscode.CompletionItem(candidate.label, context.kind === 'property' ? vscode.CompletionItemKind.Property : vscode.CompletionItemKind.Value);
			item.range = replacement;
			const property = 'property' in candidate ? candidate.property as GuiMember : undefined;
			item.insertText = property && !hasValue
				? new vscode.SnippetString().appendText(candidate.label + ': ').appendTabstop(1).appendText(';') : candidate.label;
			if (property) {
				item.documentation = details(property);
			}
			return item;
		});
	}

	private async pathCompletion(document: vscode.TextDocument, offset: number, attribute: Attribute, token: vscode.CancellationToken): Promise<vscode.CompletionItem[]> {
		const version = document.version;
		const language = document.languageId;
		const raw = document.getText(range(document, attribute.valueStart!, offset));
		const slash = Math.max(raw.lastIndexOf('/'), raw.lastIndexOf('\\'));
		const directory = fileReference(decodeAttribute(raw.slice(0, slash + 1)) || '.', document.uri);
		if (!directory) return [];
		try {
			const entries = await vscode.workspace.fs.readDirectory(directory);
			if (token.isCancellationRequested || document.isClosed || document.version !== version || document.languageId !== language) return [];
			const tail = document.getText(range(document, offset, attribute.valueEnd!));
			const nextSlash = tail.search(/[/\\]/);
			const end = nextSlash < 0 ? attribute.valueEnd! : offset + nextSlash;
			const prefix = decodeAttribute(raw.slice(slash + 1));
			const compare = (value: string) => process.platform === 'win32' ? value.toLowerCase() : value;
			return entries.filter(([name]) => compare(name).startsWith(compare(prefix))).slice(0, 512).map(([name, type]) => {
				const folder = (type & vscode.FileType.Directory) !== 0;
				const item = new vscode.CompletionItem(name, folder ? vscode.CompletionItemKind.Folder : vscode.CompletionItemKind.File);
				item.range = range(document, attribute.valueStart! + slash + 1, end);
				item.insertText = xmlText(name + (folder && nextSlash < 0 ? '/' : ''));
				item.sortText = `${folder ? '0' : '1'}${name}`;
				if (folder) {
					item.command = { title: 'Suggest', command: 'editor.action.triggerSuggest' };
				}
				return item;
			});
		}
		catch
		{
			return [];
		}
	}

	private async hover(document: vscode.TextDocument, position: vscode.Position, token: vscode.CancellationToken): Promise<vscode.Hover | undefined> {
		if (token.isCancellationRequested) return undefined;
		const model = this.model(document);
		if (model.text.length > editorSourceLimit) return undefined;
		const offset = document.offsetAt(position);
		try {
			const index = await this.catalogs.get(document.uri, token);
			if (token.isCancellationRequested || document.isClosed || model.version !== document.version || document.languageId !== model.languageId) return undefined;
			const word = document.getText(wordRange(document, position));
			const tag = model.markup && tagAt(model.markup, offset);
			if (tag && offset >= tag.nameStart && offset <= tag.nameEnd) {
				const type = index.widget(tag.name);
				if (type) return new vscode.Hover(new vscode.MarkdownString().appendCodeblock(type.name, 'veroscript'), range(document, tag.nameStart, tag.nameEnd));
			}
			const attribute = tag?.attributes.find(attribute => offset >= attribute.start && offset <= attribute.start + attribute.name.length);
			const property = attribute ? [...this.properties(index, tag!.name), ...index.events(tag!.name)].find(member => names(member).includes(attribute.name.replace(/^(bind|on):/, '')))
				: model.style && styleContext(model.style, model.text, offset)?.kind === 'property' ? index.styleProperties().find(member => names(member).includes(word)) : undefined;
			return property ? new vscode.Hover(details(property), wordRange(document, position)) : undefined;
		}
		catch (error)
		{
			if (!(error instanceof vscode.CancellationError)) {
				this.output.error(String(error));
			}
			return undefined;
		}
	}

	private links(document: vscode.TextDocument): vscode.DocumentLink[] {
		const model = this.model(document);
		if (model.text.length > editorSourceLimit) return [];
		const links: vscode.DocumentLink[] = [];
		if (model.markup) {
			for (const tag of model.markup.tags) {
				for (const attribute of tag.attributes) {
					if (!['src', 'Image', 'image'].includes(attribute.name) || attribute.value === undefined) {
						continue;
					}
					const target = fileReference(attribute.value, document.uri);
					if (target) {
						links.push(new vscode.DocumentLink(range(document, attribute.valueStart!, attribute.valueEnd!), target));
					}
				}
			}
		}
		if (model.style) {
			for (const reference of model.style.references) {
				const target = fileReference(reference.value, document.uri);
				if (target) {
					links.push(new vscode.DocumentLink(range(document, reference.start, reference.end), target));
				}
			}
		}
		return links;
	}

	private symbols(document: vscode.TextDocument): vscode.DocumentSymbol[] {
		const model = this.model(document);
		if (model.style) {
			return model.style.rules.filter(rule => rule.selector.length > 0).map(rule => new vscode.DocumentSymbol(rule.selector, '', vscode.SymbolKind.Class,
				range(document, rule.start, rule.end), range(document, rule.start, rule.bodyStart - 1)));
		}
		const result: vscode.DocumentSymbol[] = [];
		const nodes = model.markup!.elements.map(element => {
			const id = element.tag.attributes.find(attribute => attribute.name === 'id')?.value;
			return new vscode.DocumentSymbol(element.tag.name + (id ? ` #${id}` : ''), '', vscode.SymbolKind.Object,
				range(document, element.tag.start, element.end), range(document, element.tag.nameStart, element.tag.nameEnd));
		});
		for (const [index, element] of model.markup!.elements.entries()) {
			if (element.parent === undefined) {
				result.push(nodes[index]!);
			}
			else
			{
				nodes[element.parent]!.children.push(nodes[index]!);
			}
		}
		return result;
	}

	private folding(document: vscode.TextDocument): vscode.FoldingRange[] {
		const model = this.model(document);
		const spans = model.style ? model.style.rules : model.markup!.elements.map(element => ({ start: element.tag.start, end: element.end }));
		return spans.flatMap(span => {
			const start = document.positionAt(span.start).line;
			const end = document.positionAt(span.end).line - 1;
			return end > start ? [new vscode.FoldingRange(start, end)] : [];
		});
	}

	private highlights(document: vscode.TextDocument, position: vscode.Position): vscode.DocumentHighlight[] {
		const model = this.model(document).markup!;
		const offset = document.offsetAt(position);
		const tag = tagAt(model, offset);
		if (!tag || offset < tag.nameStart || offset > tag.nameEnd) return [];
		const element = model.elements.find(element => element.tag === tag || element.closingStart === tag.start);
		if (!element) return [];
		const paired = model.tags.find(candidate => candidate.start === element.closingStart && candidate.name === element.tag.name);
		return [element.tag, ...(paired ? [paired] : [])].map(candidate => new vscode.DocumentHighlight(range(document, candidate.nameStart, candidate.nameEnd)));
	}

	private colors(document: vscode.TextDocument): vscode.ColorInformation[] {
		const model = this.model(document);
		if (model.text.length > editorSourceLimit) return [];
		const spans = model.markup ? model.markup.tags.flatMap(tag => tag.attributes.filter(attribute =>
			['Background', 'background-color', 'TextColor', 'color', 'BorderColor', 'border-color', 'SelectionColor', 'selection-color'].includes(attribute.name) && attribute.valueStart !== undefined)
			.map(attribute => ({ start: attribute.valueStart!, end: attribute.valueEnd! })))
			: model.style!.values;
		const result: vscode.ColorInformation[] = [];
		let opaque = 0;
		for (const span of spans) {
			for (const match of model.text.slice(span.start, span.end).matchAll(/#([\da-fA-F]{8}|[\da-fA-F]{6})(?![\w-])/g)) {
				const start = span.start + match.index;
				while (model.style?.opaque[opaque] && model.style.opaque[opaque]!.end <= start) {
					++opaque;
				}
				if (model.style?.opaque[opaque] && model.style.opaque[opaque]!.start <= start) {
					continue;
				}
				const channels = match[1]!.match(/../g)!.map(channel => Number.parseInt(channel, 16) / 255);
				result.push(new vscode.ColorInformation(range(document, start, start + match[0].length), new vscode.Color(channels[0]!, channels[1]!, channels[2]!, channels[3] ?? 1)));
			}
		}
		return result;
	}

	private readonly models = new WeakMap<vscode.TextDocument, DocumentModel>();
	private readonly timers = new Map<string, ReturnType<typeof setTimeout>>();
	private readonly resources: vscode.Disposable[] = [];
	private readonly diagnostics = vscode.languages.createDiagnosticCollection('veroscript-gui-structure');
}
