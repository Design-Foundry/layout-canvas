import { Component, computed, effect, input, signal } from '@angular/core';
import { FormsModule, type NgModel } from '@angular/forms';
import type {
  ComponentDefinition,
  ComponentValues,
  NodePatch,
  NodeProps,
  OklchColor,
} from '@canvas/bridge';
import { LucentButton } from '@designfoundry/lucent-angular';
import type { ComponentSnapshot, SnapshotNode } from './component-package';
import { componentOptions } from './component-options';

const EMPTY: ComponentDefinition = { properties: [], axes: [], rules: [] };
const NAME = /^[a-z][a-z0-9-]{0,63}$/;
const RULE_FIELDS: Record<string, string> = {
  w: 'Width',
  h: 'Height',
  opacity: 'Opacity',
  cornerRadius: 'Corner radius',
  'textStyle.fontSize': 'Font size',
  'textStyle.lineHeight': 'Line height',
  'textStyle.letterSpacing': 'Letter spacing',
  'textStyle.align': 'Text alignment',
};

@Component({
  selector: 'app-component-properties-panel',
  imports: [FormsModule, LucentButton],
  templateUrl: './component-properties-panel.html',
  styleUrl: './component-properties-panel.scss',
})
export class ComponentPropertiesPanel {
  readonly node = input.required<NodeProps>();
  readonly snapshot = input<() => unknown>(() => null);
  readonly save = input.required<(id: string, definition: ComponentDefinition) => boolean>();
  readonly colorFromHex = input.required<(hex: string, alpha: number) => OklchColor | null>();
  readonly saveValues = input.required<(id: string, values: ComponentValues) => boolean>();
  private readonly pendingDefinition = signal<ComponentDefinition | null>(null);
  protected readonly definition = computed(
    () => this.pendingDefinition() ?? this.node().definition ?? EMPTY,
  );
  private readonly pendingValues = signal<ComponentValues | null>(null);
  private readonly selectedValues = computed(
    () => this.pendingValues() ?? this.node().componentValues ?? {},
  );
  protected readonly error = signal('');
  protected readonly hasValues = computed(() => Object.keys(this.selectedValues()).length > 0);
  protected readonly targets = computed(() => {
    const node = this.node();
    const data = this.snapshot()() as ComponentSnapshot | null;
    const all = data?.nodes ?? [];
    const result: SnapshotNode[] = [];
    const visit = (id: string) => {
      const current = all.find((n) => n.id === id);
      if (!current || (id !== node.id && current.type === 'component')) return;
      result.push(current);
      if (current.type !== 'instance')
        all.filter((n) => n.parent === id).forEach((n) => visit(n.id));
    };
    visit(node.id);
    return result;
  });
  protected readonly swapTargets = computed(() =>
    this.targets().filter((n) => n.type === 'instance'),
  );
  protected readonly swapChoices = computed(() => {
    this.node();
    const data = this.snapshot()() as ComponentSnapshot | null;
    return data && this.swapTarget() ? componentOptions(data, this.swapTarget()) : [];
  });
  protected readonly allMains = computed(() => {
    this.node();
    const data = this.snapshot()() as ComponentSnapshot | null;
    return (data?.nodes ?? []).filter((n) => n.type === 'component');
  });
  protected propertyName = '';
  protected propertyTarget = '';
  protected propertyDefault = true;
  protected textName = '';
  protected textTarget = '';
  protected textDefault = '';
  protected readonly textTargets = computed(() => this.targets().filter((n) => n.type === 'text'));

  protected addText(): void {
    const name = this.textName.trim();
    if (!NAME.test(name) || !this.textTarget) {
      this.error.set('Choose a text layer and a property name.');
      return;
    }
    if (
      this.store({
        ...this.definition(),
        texts: [
          ...(this.definition().texts ?? []),
          { name, target: this.textTarget, default: this.textDefault.normalize('NFC') },
        ],
      })
    )
      this.textName = '';
  }
  protected removeText(name: string): void {
    const texts = (this.definition().texts ?? []).filter((t) => t.name !== name);
    this.store({ ...this.definition(), texts: texts.length ? texts : undefined });
  }
  protected textDefaultChanged(name: string, value: string, control: NgModel): void {
    const previous = (this.definition().texts ?? []).find((t) => t.name === name)?.default;
    if (
      !this.store({
        ...this.definition(),
        texts: (this.definition().texts ?? []).map((t) =>
          t.name === name ? { ...t, default: value.normalize('NFC') } : t,
        ),
      })
    )
      control.control.setValue(previous, { emitEvent: false, emitViewToModelChange: false });
  }
  protected axisName = '';
  protected axisChoices = '';
  protected ruleTarget = '';
  protected ruleField = 'cornerRadius';
  protected ruleValue = '8';
  protected cell: Record<string, string> = {};
  protected swapName = '';
  protected readonly swapTarget = signal('');
  protected swapDefault = '';
  protected swapAllowed: string[] = [];

  constructor() {
    effect(() => {
      this.node().id;
      this.pendingDefinition.set(null);
      this.pendingValues.set(null);
      this.error.set('');
      this.propertyTarget = '';
      this.textTarget = '';
      this.ruleTarget = '';
      this.cell = {};
      this.swapTarget.set('');
      this.swapAllowed = [];
    });
  }

  protected label(id: string): string {
    return this.targets().find((n) => n.id === id)?.['name'] || 'Layer';
  }
  protected value(name: string, fallback: boolean | string): boolean | string {
    return this.selectedValues()[name] ?? fallback;
  }
  protected choose(
    name: string,
    value: boolean | string,
    control?: NgModel,
    fallback: boolean | string = '',
  ): void {
    const previous = this.value(name, fallback);
    const isText = this.definition().texts?.some((t) => t.name === name);
    const values = {
      ...this.selectedValues(),
      [name]: isText && typeof value === 'string' ? value.normalize('NFC') : value,
    };
    if (!this.applyValues(values))
      control?.control.setValue(previous, { emitEvent: false, emitViewToModelChange: false });
  }
  protected reset(name?: string): void {
    const values = { ...this.selectedValues() };
    if (name) delete values[name];
    this.applyValues(name ? values : {});
  }
  private applyValues(values: ComponentValues): boolean {
    if (this.saveValues()(this.node().id, values)) {
      this.pendingValues.set(values);
      this.error.set('');
      return true;
    } else
      this.error.set(
        'This choice is invalid or would strand overrides. Reset overrides in the affected layer before swapping, or check the supported text characters.',
      );
    return false;
  }
  protected mainLabel(id: string): string {
    return this.allMains().find((n) => n.id === id)?.['name'] || 'Component';
  }
  protected allowSwap(id: string, enabled: boolean): void {
    this.swapAllowed = enabled
      ? [...new Set([...this.swapAllowed, id])]
      : this.swapAllowed.filter((choice) => choice !== id);
  }
  protected setSwapTarget(id: string): void {
    this.swapTarget.set(id);
    this.swapAllowed = [];
    this.swapDefault = '';
  }
  protected addSwap(): void {
    const name = this.swapName.trim();
    if (!NAME.test(name) || !this.swapTarget() || !this.swapAllowed.includes(this.swapDefault)) {
      this.error.set(
        'Choose a name, an instance layer, allowed components and a default from those choices.',
      );
      return;
    }
    const d = this.definition();
    if (
      this.store({
        ...d,
        swaps: [
          ...(d.swaps ?? []),
          {
            name,
            target: this.swapTarget(),
            choices: [...this.swapAllowed],
            default: this.swapDefault,
          },
        ],
      })
    )
      this.swapName = '';
  }
  protected removeSwap(name: string): void {
    const d = this.definition();
    const swaps = (d.swaps ?? []).filter((s) => s.name !== name);
    this.store({ ...d, swaps: swaps.length ? swaps : undefined });
  }
  protected swapDefaultChanged(name: string, value: string, control?: NgModel): void {
    const d = this.definition();
    const previous = (d.swaps ?? []).find((s) => s.name === name)?.default;
    const ok = this.store({
      ...d,
      swaps: (d.swaps ?? []).map((s) => (s.name === name ? { ...s, default: value } : s)),
    });
    if (!ok)
      control?.control.setValue(previous, { emitEvent: false, emitViewToModelChange: false });
  }
  protected store(definition: ComponentDefinition): boolean {
    const ok = this.save()(this.node().id, definition);
    if (ok) this.pendingDefinition.set(definition);
    this.error.set(
      ok
        ? ''
        : 'This change is invalid, uses unsupported text, or removes a choice used by an instance. Reset its property or text-layer overrides first.',
    );
    return ok;
  }
  protected addProperty(): void {
    const name = this.propertyName.trim();
    if (!NAME.test(name) || !this.propertyTarget) {
      this.error.set(
        'Choose a layer and a name such as show-icon. Use lowercase letters, numbers and hyphens.',
      );
      return;
    }
    const definition = this.definition();
    if (
      this.store({
        ...definition,
        properties: [
          ...definition.properties,
          { name, target: this.propertyTarget, default: this.propertyDefault },
        ],
      })
    )
      this.propertyName = '';
  }
  protected addAxis(): void {
    const name = this.axisName.trim();
    const values = this.axisChoices.split(',').map((v) => v.trim());
    if (
      !NAME.test(name) ||
      !values.every((v) => NAME.test(v)) ||
      new Set(values).size !== values.length
    ) {
      this.error.set(
        'Use lowercase names, with unique choices separated by commas, such as compact, large.',
      );
      return;
    }
    const definition = this.definition();
    if (
      this.store({
        ...definition,
        axes: [...definition.axes, { name, values, default: values[0] }],
      })
    ) {
      this.axisName = '';
      this.axisChoices = '';
    }
  }
  protected propertyDefaultChanged(name: string, value: boolean): void {
    const definition = this.definition();
    this.store({
      ...definition,
      properties: definition.properties.map((p) =>
        p.name === name ? { ...p, default: value } : p,
      ),
    });
  }
  protected axisChoicesChanged(name: string, value: string, control: NgModel): void {
    const definition = this.definition();
    const axis = definition.axes.find((axis) => axis.name === name);
    if (!axis) return;
    const restore = () =>
      control.control.setValue(axis.values.join(', '), {
        emitEvent: false,
        emitViewToModelChange: false,
      });
    const values = value.split(',').map((choice) => choice.trim());
    if (
      values.length > 32 ||
      !values.every((choice) => NAME.test(choice)) ||
      new Set(values).size !== values.length
    ) {
      this.error.set('Use 1–32 unique lowercase choices separated by commas.');
      restore();
      return;
    }
    if (!values.includes(axis.default)) {
      this.error.set('Choose another default before removing the current default choice.');
      restore();
      return;
    }
    if (definition.rules.some((rule) => rule.when[name] && !values.includes(rule.when[name]))) {
      this.error.set('Remove or update variant styles using this choice before removing it.');
      restore();
      return;
    }
    if (
      values.length === axis.values.length &&
      values.every((choice, i) => choice === axis.values[i])
    )
      return;
    if (
      !this.store({
        ...definition,
        axes: definition.axes.map((axis) => (axis.name === name ? { ...axis, values } : axis)),
      })
    )
      restore();
    else if (this.cell[name] && !values.includes(this.cell[name])) delete this.cell[name];
  }
  protected axisDefaultChanged(name: string, value: string, control: NgModel): void {
    const definition = this.definition();
    if (
      !this.store({
        ...definition,
        axes: definition.axes.map((a) => (a.name === name ? { ...a, default: value } : a)),
      })
    )
      control.control.setValue(definition.axes.find((axis) => axis.name === name)?.default, {
        emitEvent: false,
        emitViewToModelChange: false,
      });
  }
  protected removeProperty(name: string): void {
    const definition = this.definition();
    this.store({ ...definition, properties: definition.properties.filter((p) => p.name !== name) });
  }
  protected removeAxis(name: string): void {
    const definition = this.definition();
    this.store({
      ...definition,
      axes: definition.axes.filter((a) => a.name !== name),
      rules: definition.rules.filter((r) => !(name in r.when)),
    });
  }
  protected removeRule(index: number): void {
    const definition = this.definition();
    this.store({ ...definition, rules: definition.rules.filter((_, i) => i !== index) });
  }
  protected ruleFields(
    patch: NodePatch,
  ): { key: string; label: string; value: string; editable: boolean }[] {
    return Object.entries(patch).flatMap(([key, value]) => {
      if (key === 'fillVisible' && patch.fill !== undefined) return [];
      if (key === 'textStyle' && value)
        return Object.entries(value).map(([field, metric]) => ({
          key: `textStyle.${field}`,
          label: RULE_FIELDS[`textStyle.${field}`] ?? field,
          value: String(metric),
          editable: typeof metric === 'number' || field === 'align',
        }));
      return [
        {
          key,
          label: RULE_FIELDS[key] ?? key,
          value: typeof value === 'object' ? 'Authored value' : String(value),
          editable: key in RULE_FIELDS && typeof value === 'number',
        },
      ];
    });
  }
  protected removeRuleField(index: number, key: string): void {
    const definition = this.definition(),
      rule = definition.rules[index];
    if (!rule || !this.ruleFields(rule.patch).some((field) => field.key === key)) return;
    let patch: NodePatch;
    if (key.startsWith('textStyle.')) {
      const textStyle = Object.fromEntries(
        Object.entries(rule.patch.textStyle ?? {}).filter(
          ([field]) => `textStyle.${field}` !== key,
        ),
      );
      const { textStyle: previous, ...rest } = rule.patch;
      patch = Object.keys(textStyle).length ? { ...rest, textStyle } : rest;
    } else
      patch = Object.fromEntries(
        Object.entries(rule.patch).filter(
          ([field]) => field !== key && !(key === 'fill' && field === 'fillVisible'),
        ),
      );
    const rules = definition.rules.flatMap((current, i) =>
      i !== index ? [current] : Object.keys(patch).length ? [{ ...rule, patch }] : [],
    );
    this.store({ ...definition, rules });
  }
  protected ruleFieldChanged(index: number, key: string, raw: string, control: NgModel): void {
    const definition = this.definition(),
      rule = definition.rules[index];
    const field = rule && this.ruleFields(rule.patch).find((field) => field.key === key);
    if (!rule || !field?.editable) return;
    const restore = () =>
      control.control.setValue(field.value, { emitEvent: false, emitViewToModelChange: false });
    const value = key === 'textStyle.align' ? raw : Number(raw);
    const max =
      key === 'opacity'
        ? 1
        : key === 'textStyle.fontSize'
          ? 512
          : key === 'textStyle.lineHeight'
            ? 4
            : key === 'textStyle.letterSpacing'
              ? 128
              : Infinity;
    const min = key === 'textStyle.fontSize' || key === 'textStyle.lineHeight' ? 1 : 0;
    if (
      (typeof value === 'number' &&
        (!raw.trim() || !Number.isFinite(value) || value < min || value > max)) ||
      (key === 'textStyle.align' && !['left', 'center', 'right'].includes(raw))
    ) {
      this.error.set(
        'Enter a supported value: nonnegative geometry, opacity 0–1, font size 1–512, line height 1–4, spacing 0–128 or left/center/right alignment.',
      );
      restore();
      return;
    }
    const patch: NodePatch = key.startsWith('textStyle.')
      ? {
          ...rule.patch,
          textStyle: { ...rule.patch.textStyle, [key.slice('textStyle.'.length)]: value },
        }
      : { ...rule.patch, [key]: value };
    if (
      !this.store({
        ...definition,
        rules: definition.rules.map((current, i) => (i === index ? { ...rule, patch } : current)),
      })
    )
      restore();
  }
  protected ruleLabel(when: Readonly<Record<string, string>>): string {
    return Object.entries(when)
      .map(([name, value]) => `${name}: ${value}`)
      .join(' · ');
  }
  protected patchLabel(patch: NodePatch): string {
    const labels: Record<string, string> = {
      w: 'Width',
      h: 'Height',
      fill: 'Background color',
      opacity: 'Opacity',
      cornerRadius: 'Corner radius',
    };
    return Object.entries(patch)
      .filter(([name]) => name !== 'fillVisible')
      .map(([name, value]) =>
        name === 'textStyle' && value
          ? Object.entries(value)
              .map(([field, item]) => `${field}: ${item}`)
              .join(', ')
          : typeof value === 'object'
            ? labels[name] || name
            : `${labels[name] || name}: ${value}`,
      )
      .join(', ');
  }
  protected addRule(): void {
    const when = Object.fromEntries(Object.entries(this.cell).filter(([, value]) => value));
    const target = this.ruleTarget;
    if (!target || !Object.keys(when).length) {
      this.error.set(
        'Choose a layer and at least one variant value. Any keeps the other axes independent.',
      );
      return;
    }
    let patch: NodePatch;
    if (['fontSize', 'lineHeight', 'letterSpacing', 'align'].includes(this.ruleField)) {
      const value = this.ruleField === 'align' ? this.ruleValue : Number(this.ruleValue);
      if (
        !this.textRuleTarget() ||
        (typeof value === 'number' &&
          (!this.ruleValue.trim() ||
            !Number.isFinite(value) ||
            value < (this.ruleField === 'letterSpacing' ? 0 : 1) ||
            value >
              (this.ruleField === 'fontSize' ? 512 : this.ruleField === 'lineHeight' ? 4 : 128))) ||
        (this.ruleField === 'align' && !['left', 'center', 'right'].includes(this.ruleValue))
      ) {
        this.error.set(
          'Choose a text layer and supported typography: size 1–512, line height 1–4, spacing 0–128, or left/center/right alignment.',
        );
        return;
      }
      patch = { textStyle: { [this.ruleField]: value } };
    } else if (this.ruleField === 'fill') {
      const color = this.colorFromHex()(this.ruleValue, 1);
      if (!color) {
        this.error.set('Enter a color such as #227744.');
        return;
      }
      patch = { fill: color, fillVisible: true };
    } else {
      const value = Number(this.ruleValue);
      if (
        !Number.isFinite(value) ||
        value < 0 ||
        (this.ruleField === 'opacity' && value > 1) ||
        (target === this.node().id && ['w', 'h'].includes(this.ruleField))
      ) {
        this.error.set(
          'Use a nonnegative number (opacity: 0 to 1). Size rules apply to child layers; instance bounds remain independent.',
        );
        return;
      }
      patch = { [this.ruleField]: value };
    }
    const definition = this.definition();
    // An exact cell/target has one cumulative patch; more-specific cells take precedence.
    const sameCell = (cell: Readonly<Record<string, string>>) =>
      Object.keys(cell).length === Object.keys(when).length &&
      Object.entries(when).every(([k, v]) => cell[k] === v);
    const existing = definition.rules.findIndex((r) => r.target === target && sameCell(r.when));
    const rules = [...definition.rules];
    if (existing < 0) rules.push({ target, when, patch });
    else
      rules[existing] = {
        ...rules[existing],
        patch: {
          ...rules[existing].patch,
          ...patch,
          ...(patch.textStyle
            ? { textStyle: { ...rules[existing].patch.textStyle, ...patch.textStyle } }
            : {}),
        },
      };
    this.store({ ...definition, rules });
  }
  protected textRuleTarget(): boolean {
    return this.targets().some((target) => target.id === this.ruleTarget && target.type === 'text');
  }
}
