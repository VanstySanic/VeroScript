// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export interface GuiMember {
	readonly name: string;
	readonly mark?: string;
	readonly type: string;
	readonly bindable?: boolean;
	readonly style?: boolean;
	readonly values?: readonly string[];
	readonly description?: string;
}

export interface GuiType {
	readonly name: string;
	readonly mark?: string;
	readonly bases: readonly string[];
	readonly properties: readonly GuiMember[];
	readonly events: readonly GuiMember[];
	readonly description?: string;
}

export interface GuiSchema {
	readonly schemaVersion: 1;
	readonly productVersion?: string;
	readonly types: readonly GuiType[];
	readonly routedEvents: readonly GuiMember[];
}

export function names(member: { readonly name: string; readonly mark?: string }): readonly string[] {
	return [...new Set([member.name.split('::').at(-1)!, ...(member.mark ? [member.mark] : [])])];
}

export function readSchema(value: unknown): GuiSchema {
	const object = (item: unknown): Record<string, unknown> => {
		if (!item || typeof item !== 'object' || Array.isArray(item)) {
			throw new Error('schema.object');
		}
		return item as Record<string, unknown>;
	};
	const text = (item: unknown): string => {
		if (typeof item !== 'string' || !item || item.length > 16384 || item.includes('\0')) {
			throw new Error('schema.string');
		}
		return item;
	};
	const optionalText = (item: unknown) => item === undefined ? undefined : text(item);
	const flag = (item: unknown): boolean | undefined => {
		if (item === undefined || typeof item === 'boolean') return item;
		throw new Error('schema.boolean');
	};
	const list = <T>(items: unknown, read: (item: unknown) => T): T[] => {
		if (!Array.isArray(items) || items.length > 4096) {
			throw new Error('schema.array');
		}
		return items.map(read);
	};
	const member = (item: unknown): GuiMember => {
		const value = object(item);
		return { name: text(value.name), mark: optionalText(value.mark), type: text(value.type),
			bindable: flag(value.bindable), style: flag(value.style), description: optionalText(value.description),
			values: value.values === undefined ? undefined : list(value.values, text) };
	};
	const root = object(value);
	if (root.schemaVersion !== 1) {
		throw new Error('schema.version');
	}
	const types = list(root.types, item => {
		const type = object(item);
		return { name: text(type.name), mark: optionalText(type.mark), description: optionalText(type.description),
			bases: list(type.bases ?? [], text), properties: list(type.properties ?? [], member), events: list(type.events ?? [], member) };
	});
	if (new Set(types.map(type => type.name)).size !== types.length) {
		throw new Error('schema.duplicateType');
	}
	return { schemaVersion: 1, productVersion: optionalText(root.productVersion), types, routedEvents: list(root.routedEvents ?? [], member) };
}

/** Bundled metadata is a completion hint. Native compilation owns final access and ambiguity checks. */
export class SchemaIndex {
	constructor(schemas: readonly GuiSchema[]) {
		for (const schema of schemas) {
			for (const type of schema.types) {
				this.types.set(type.name, type);
			}
			for (const event of schema.routedEvents) {
				this.routed.set(event.name, event);
			}
		}
		for (const type of this.types.values()) {
			for (const alias of names(type)) {
				this.tags.set(alias, this.tags.has(alias) ? undefined : type);
			}
		}
	}

	widgets(): readonly { tag: string; type: GuiType }[] {
		return [...this.tags].flatMap(([tag, type]) => type ? [{ tag, type }] : []);
	}

	widget(tag: string): GuiType | undefined {
		return this.tags.get(tag);
	}

	properties(tag: string): readonly GuiMember[] {
		return this.members(this.widget(tag), 'properties');
	}

	events(tag: string): readonly GuiMember[] {
		return [...this.routed.values(), ...this.members(this.widget(tag), 'events')];
	}

	styleProperties(): readonly GuiMember[] {
		const properties = new Map<string, GuiMember>();
		for (const type of this.types.values()) {
			for (const property of type.properties) {
				if (property.style) {
					properties.set(property.mark ?? property.name, property);
				}
			}
		}
		return [...properties.values()];
	}

	private members(type: GuiType | undefined, key: 'properties' | 'events'): readonly GuiMember[] {
		if (!type) return [];
		const cacheKey = `${type.name}\0${key}`;
		const cached = this.inherited.get(cacheKey);
		if (cached) return cached;
		const seen = new Set<string>();
		const members = new Map<string, GuiMember>();
		const visit = (current: GuiType | undefined, depth: number): void => {
			if (!current || seen.has(current.name) || depth >= 64) return;
			seen.add(current.name);
			for (const base of current.bases) {
				visit(this.types.get(base), depth + 1);
			}
			for (const member of current[key]) {
				members.set(member.name, member);
			}
		};
		visit(type, 0);
		const result = [...members.values()];
		this.inherited.set(cacheKey, result);
		return result;
	}

	private readonly types = new Map<string, GuiType>();
	private readonly tags = new Map<string, GuiType | undefined>();
	private readonly routed = new Map<string, GuiMember>();
	private readonly inherited = new Map<string, readonly GuiMember[]>();
}
