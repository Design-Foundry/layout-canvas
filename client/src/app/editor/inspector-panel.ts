import { CdkConnectedOverlay, ConnectedPosition } from '@angular/cdk/overlay';
import { Component, ElementRef, computed, effect, input, output, signal } from '@angular/core';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import {
  ComponentDefinition,
  ComponentValues,
  AlignItems,
  ColorValue,
  Four,
  Justify,
  Length,
  NodePatch,
  NodeProps,
  OklchColor,
  ShadowProps,
  Sizing,
  StrokeAlign,
  StrokeProps,
  StrokeStyle,
  TokenRow,
} from '@canvas/bridge';
import {
  LucentButton,
  LucentCheckbox,
  LucentIconButton,
  LucentLabel,
  LucentReferenceTag,
  LucentSegmentedControl,
  LucentSelect,
} from '@design-foundry/lucent';
import { ComponentCommand } from './editor-model';
import { BindableFamily, ColorField, PickRequest, ValueField, isToken } from './inspector-fields';
import { NumberField } from './number-field';
import { TokenPicker } from './token-picker';
import { ComponentPropertiesPanel } from './component-properties-panel';
import { TextPanel } from './text-panel';
import { LinePanel } from './line-panel';
import { PathPanel } from './path-panel';
import { ImagePanel } from './image-panel';

/**
 * Identifies one bindable property so a single picker can serve them all: corner0..3 (top-start,
 * top-end, bottom-end, bottom-start), pad0..3 and strokeSide0..3 (top, end, bottom, start),
 * shadowColor<n> for layer n.
 */
type FieldKey = string;

const PICKER_POSITIONS: ConnectedPosition[] = [
  { originX: 'end', originY: 'bottom', overlayX: 'end', overlayY: 'top', offsetY: 4 },
  { originX: 'end', originY: 'top', overlayX: 'end', overlayY: 'bottom', offsetY: -4 },
];

const NO_STROKE: StrokeProps = {
  width: 1,
  style: 'solid',
  color: { l: 0, c: 0, h: 0, a: 1 },
  align: 'inside',
};
const NEW_SHADOW: ShadowProps = {
  x: 0,
  y: 4,
  blur: 8,
  spread: 0,
  color: { l: 0, c: 0, h: 0, a: 0.25 },
  inset: false,
  visible: true,
};

const SIDE_LABELS = ['T', 'R', 'B', 'L'] as const;
const CORNER_LABELS = ['TL', 'TR', 'BR', 'BL'] as const;
const JUSTIFY_OPTIONS: { label: string; value: Justify }[] = [
  { label: 'Start', value: 'start' },
  { label: 'Center', value: 'center' },
  { label: 'End', value: 'end' },
  { label: 'Space between', value: 'spaceBetween' },
  { label: 'Space around', value: 'spaceAround' },
  { label: 'Space evenly', value: 'spaceEvenly' },
];
const ALIGN_OPTIONS: { label: string; value: AlignItems }[] = [
  { label: 'Start', value: 'start' },
  { label: 'Center', value: 'center' },
  { label: 'End', value: 'end' },
  { label: 'Stretch', value: 'stretch' },
];
const STYLE_OPTIONS: { label: string; value: StrokeStyle }[] = [
  { label: 'Solid', value: 'solid' },
  { label: 'Dashed', value: 'dashed' },
  { label: 'Dotted', value: 'dotted' },
  { label: 'Double', value: 'double' },
];
const STROKE_ALIGN_OPTIONS: { label: string; value: StrokeAlign }[] = [
  { label: 'Inside', value: 'inside' },
  { label: 'Center', value: 'center' },
  { label: 'Outside', value: 'outside' },
];
const DIRECTION_OPTIONS = [
  { label: 'Row', value: 'row' },
  { label: 'Column', value: 'column' },
];
const SHADOW_KIND_OPTIONS = [
  { label: 'Drop', value: 'drop' },
  { label: 'Inner', value: 'inner' },
];

function replaceAt<T>(items: Four<T>, index: number, value: T): Four<T> {
  const copy = [...items] as [T, T, T, T];
  copy[index] = value;
  return copy;
}

@Component({
  selector: 'app-inspector-panel',
  imports: [
    ReactiveFormsModule,
    TextPanel,
    LinePanel,
    PathPanel,
    ImagePanel,
    CdkConnectedOverlay,
    LucentButton,
    LucentCheckbox,
    LucentIconButton,
    LucentLabel,
    LucentReferenceTag,
    LucentSegmentedControl,
    LucentSelect,
    NumberField,
    ValueField,
    ColorField,
    TokenPicker,
    ComponentPropertiesPanel,
  ],
  templateUrl: './inspector-panel.html',
  styleUrls: ['./panel.scss', './inspector-panel.scss'],
})
export class InspectorPanel {
  readonly saveLine = input<(id: string, patch: NodePatch) => boolean>(() => false);
  readonly saveStyleBinding = input<(id: string, patch: NodePatch) => boolean>(() => false);
  readonly saveWrap = input<(id: string, wrap: boolean) => boolean>(() => false);
  readonly saveText = input<(id: string, text: import('@canvas/bridge').TextRun) => boolean>(
    () => false,
  );
  readonly snapshot = input<() => unknown>(() => null);
  readonly saveDefinition = input<(id: string, definition: ComponentDefinition) => boolean>(
    () => false,
  );
  readonly saveValues = input<(id: string, values: ComponentValues) => boolean>(() => false);
  readonly node = input<NodeProps | null>(null);
  protected readonly hasComponentValues = computed(
    () => Object.keys(this.node()?.componentValues ?? {}).length > 0,
  );
  readonly components = input<readonly { label: string; value: string }[]>([]);
  protected readonly componentChoices = computed(() => [...this.components()]);
  /** The document's tokens, resolved in the active mode. */
  readonly tokens = input<readonly TokenRow[]>([]);
  readonly colorFromHex = input.required<(hex: string, alpha: number) => OklchColor | null>();
  readonly colorToHex = input.required<(color: OklchColor) => string>();
  readonly edit = output<NodePatch>();
  /** A live change from a continuous control. The page shows it without recording it. */
  readonly previewEdit = output<NodePatch>();
  /** The continuous control was released: the last preview becomes an edit. */
  readonly commitPreview = output<void>();
  /** The continuous control was abandoned: the last preview is dropped. */
  readonly cancelPreview = output<void>();
  readonly componentCommand = output<ComponentCommand>();

  protected readonly positions = PICKER_POSITIONS;
  protected readonly sideLabels = SIDE_LABELS;
  protected readonly cornerLabels = CORNER_LABELS;
  protected readonly directionOptions = DIRECTION_OPTIONS;
  protected readonly alignOptions = ALIGN_OPTIONS;
  protected readonly strokeAlignOptions = STROKE_ALIGN_OPTIONS;
  protected readonly shadowKindOptions = SHADOW_KIND_OPTIONS;
  protected readonly justifyOptions = JUSTIFY_OPTIONS;
  protected readonly styleOptions = STYLE_OPTIONS;

  protected readonly showLimits = signal(false);
  protected readonly showCorners = signal(false);
  protected readonly showSides = signal(false);

  protected readonly picker = signal<{ key: FieldKey; origin: ElementRef<HTMLElement> } | null>(
    null,
  );
  private previewing = false;

  protected readonly stroke = computed<StrokeProps | null>(() => {
    const stroke = this.node()?.stroke;
    if (!stroke || isToken(stroke)) return null;
    return stroke.style !== 'none' &&
      (stroke.width !== 0 || stroke.sides?.some((s) => s !== null && s !== 0))
      ? stroke
      : null;
  });
  protected readonly borderToken = computed(() => this.compositeToken(this.node()?.stroke));
  protected readonly shadows = computed<readonly ShadowProps[]>(() => {
    const shadows = this.node()?.shadows;
    return shadows && !isToken(shadows) ? shadows : [];
  });
  protected readonly shadowToken = computed(() => this.compositeToken(this.node()?.shadows));
  protected readonly hasLimits = computed(() => {
    const limits = this.node()?.limits ?? {};
    return Object.values(limits).some((v) => v !== undefined);
  });
  protected readonly hasCorners = computed(
    () => this.node()?.cornerRadii.some((c) => c !== null) ?? false,
  );
  protected readonly hasSides = computed(
    () => this.stroke()?.sides?.some((s) => s !== null) ?? false,
  );
  protected readonly pickerFamily = computed(() => {
    const picker = this.picker();
    return picker ? this.familyOf(picker.key) : 'color';
  });
  protected readonly pickerTokens = computed(() =>
    this.tokens().filter((t) => t.type === this.pickerFamily()),
  );

  protected readonly justifyControl = new FormControl<Justify>('start', { nonNullable: true });
  protected readonly styleControl = new FormControl<StrokeStyle>('solid', { nonNullable: true });
  protected readonly clipControl = new FormControl(false, { nonNullable: true });
  protected readonly absoluteControl = new FormControl(false, { nonNullable: true });
  protected readonly componentControl = new FormControl('', { nonNullable: true });
  protected readonly isContainer = computed(() =>
    ['frame', 'component', 'instance'].includes(this.node()?.type ?? ''),
  );

  constructor() {
    effect(() => {
      const node = this.node();
      if (!node) return;
      this.justifyControl.setValue(node.layout.justify, { emitEvent: false });
      this.styleControl.setValue(this.stroke()?.style ?? 'solid', { emitEvent: false });
      this.clipControl.setValue(node.clipContent, { emitEvent: false });
      this.absoluteControl.setValue(node.absolute, { emitEvent: false });
      this.componentControl.setValue(node.component ?? '', { emitEvent: false });
      if (
        node.overrides ||
        node.inInstance ||
        node.swapBound ||
        Object.keys(node.componentValues ?? {}).length
      )
        this.componentControl.disable({ emitEvent: false });
      else this.componentControl.enable({ emitEvent: false });
    });
    this.justifyControl.valueChanges
      .pipe(takeUntilDestroyed())
      .subscribe((justify) => this.setLayout({ justify }));
    this.styleControl.valueChanges
      .pipe(takeUntilDestroyed())
      .subscribe((style) => this.setStroke({ style }));
    this.clipControl.valueChanges
      .pipe(takeUntilDestroyed())
      .subscribe((clipContent) => this.edit.emit({ clipContent }));
    this.absoluteControl.valueChanges
      .pipe(takeUntilDestroyed())
      .subscribe((absolute) => this.edit.emit(this.absolutePatch(absolute)));
    this.componentControl.valueChanges.pipe(takeUntilDestroyed()).subscribe((component) => {
      const node = this.node();
      if (
        node?.type === 'instance' &&
        !node.overrides &&
        !Object.keys(node.componentValues ?? {}).length &&
        !node.inInstance &&
        !node.swapBound &&
        component !== node.component &&
        this.components().some((option) => option.value === component)
      ) {
        this.edit.emit({ component });
      }
    });
  }

  // ----- Field keys -----

  protected familyOf(key: FieldKey): BindableFamily {
    if (key === 'opacity') return 'opacity';
    if (key === 'fill' || key === 'strokeColor' || key.startsWith('shadowColor')) return 'color';
    if (key === 'stroke') return 'border';
    if (key === 'shadows') return 'shadow';
    return 'dimension';
  }

  /** The patch that sets the property behind `key`, or null to clear an optional one. */
  protected patchFor(key: FieldKey, value: Length | ColorValue | null): NodePatch | null {
    const node = this.node();
    if (!node) return null;
    const indexed = (prefix: string) => Number(key.slice(prefix.length));
    if (key === 'opacity') return value === null ? null : { opacity: value as Length };
    if (key === 'cornerRadius') return value === null ? null : { cornerRadius: value as Length };
    if (key.startsWith('corner')) {
      return {
        cornerRadii: replaceAt(node.cornerRadii, indexed('corner'), value as Length | null),
      };
    }
    if (key === 'fill') return value === null ? null : { fill: value as ColorValue };
    if (key === 'gap')
      return value === null ? null : { layout: { ...node.layout, gap: value as Length } };
    if (key.startsWith('pad')) {
      return value === null
        ? null
        : {
            layout: {
              ...node.layout,
              padding: replaceAt(node.layout.padding, indexed('pad'), value as Length),
            },
          };
    }
    if (['minWidth', 'maxWidth', 'minHeight', 'maxHeight'].includes(key)) {
      const limits = { ...node.limits } as Record<string, Length | undefined>;
      if (value === null) delete limits[key];
      else limits[key] = value as Length;
      return { limits };
    }
    if (key === 'stroke') return value === null ? null : { stroke: value as never };
    if (key === 'shadows') return value === null ? null : { shadows: value as never };
    const stroke = this.stroke() ?? NO_STROKE;
    if (key === 'strokeColor')
      return value === null ? null : { stroke: { ...stroke, color: value as ColorValue } };
    if (key === 'strokeWidth')
      return value === null ? null : { stroke: { ...stroke, width: value as Length } };
    if (key.startsWith('strokeSide')) {
      const sides = stroke.sides ?? [null, null, null, null];
      return {
        stroke: {
          ...stroke,
          sides: replaceAt(sides, indexed('strokeSide'), value as Length | null),
        },
      };
    }
    if (key.startsWith('shadowColor')) {
      if (value === null) return null;
      const index = indexed('shadowColor');
      return {
        shadows: this.shadows().map((s, i) =>
          i === index ? { ...s, color: value as ColorValue } : s,
        ),
      };
    }
    return null;
  }

  protected set(key: FieldKey, value: Length | ColorValue | null): void {
    const patch = this.patchFor(key, value);
    if (patch) this.edit.emit(patch);
  }

  protected preview(key: FieldKey, value: ColorValue): void {
    const patch = this.patchFor(key, value);
    if (patch) this.previewEdit.emit(patch);
  }

  // ----- Geometry -----

  protected setX(x: number | null): void {
    if (x !== null) this.edit.emit({ x });
  }

  protected setY(y: number | null): void {
    if (y !== null) this.edit.emit({ y });
  }

  protected setWidth(w: number | null): void {
    if (w !== null) this.edit.emit({ w, widthSizing: 'fixed' });
  }

  protected setHeight(h: number | null): void {
    if (h !== null) this.edit.emit({ h, heightSizing: 'fixed' });
  }

  protected sizingOptions(node: NodeProps): { label: string; value: Sizing; disabled?: boolean }[] {
    const canHug =
      node.type === 'text' ||
      (['frame', 'component', 'instance'].includes(node.type) && node.layout.type !== 'none');
    return [
      { label: 'Fixed', value: 'fixed' },
      { label: 'Hug', value: 'hug', disabled: !canHug },
      { label: 'Fill', value: 'fill', disabled: !node.inFlow },
    ];
  }

  protected setSizing(axis: 'widthSizing' | 'heightSizing', value: unknown): void {
    const node = this.node();
    if (!node) return;
    const sizing = value as Sizing;
    const patch: NodePatch =
      axis === 'widthSizing'
        ? { widthSizing: sizing, ...(sizing === 'fixed' ? { w: node.box.w } : {}) }
        : { heightSizing: sizing, ...(sizing === 'fixed' ? { h: node.box.h } : {}) };
    this.edit.emit(patch);
  }

  private absolutePatch(absolute: boolean): NodePatch {
    const node = this.node();
    return node && absolute ? { absolute, x: node.box.x, y: node.box.y } : { absolute };
  }

  protected toggleLimits(): void {
    if (this.hasLimits()) {
      this.edit.emit({ limits: {} });
      this.showLimits.set(false);
    } else {
      this.showLimits.update((v) => !v);
    }
  }

  // ----- Auto layout -----

  protected addLayout(): void {
    const node = this.node();
    if (node)
      this.edit.emit({
        layout: { ...node.layout, type: 'row' },
        widthSizing: 'hug',
        heightSizing: 'hug',
      });
  }

  protected removeLayout(): void {
    const node = this.node();
    if (!node) return;
    this.edit.emit({
      layout: { ...node.layout, type: 'none' },
      widthSizing: 'fixed',
      heightSizing: 'fixed',
      w: node.box.w,
      h: node.box.h,
    });
  }

  protected setLayout(changes: Partial<NodeProps['layout']>): void {
    const node = this.node();
    if (node) this.edit.emit({ layout: { ...node.layout, ...changes } });
  }

  // ----- Appearance -----

  protected toggleCorners(): void {
    if (this.hasCorners()) {
      this.edit.emit({ cornerRadii: [null, null, null, null] });
      this.showCorners.set(false);
    } else {
      this.showCorners.update((v) => !v);
    }
  }

  protected toggleFill(): void {
    const node = this.node();
    if (node) this.edit.emit({ fillVisible: !node.fillVisible });
  }

  // ----- Stroke -----

  protected addStroke(): void {
    this.edit.emit({ stroke: NO_STROKE });
  }

  protected removeStroke(): void {
    this.edit.emit({ stroke: { ...NO_STROKE, width: 0 } });
    this.showSides.set(false);
  }

  protected setStroke(changes: Partial<StrokeProps>): void {
    const stroke = this.stroke();
    if (stroke) this.edit.emit({ stroke: { ...stroke, ...changes } });
  }

  protected toggleSides(): void {
    if (this.hasSides()) {
      this.setStroke({ sides: [null, null, null, null] });
      this.showSides.set(false);
    } else {
      this.showSides.update((v) => !v);
    }
  }

  protected detachBorder(): void {
    const resolved = this.borderToken()?.border;
    if (!resolved) return;
    const { l, c, h, a } = resolved.color;
    this.edit.emit({
      stroke: {
        width: resolved.width,
        style: resolved.style,
        color: { l, c, h, a },
        align: 'inside',
      },
    });
  }

  // ----- Effects -----

  protected addShadow(): void {
    this.edit.emit({ shadows: [...this.shadows(), NEW_SHADOW] });
  }

  protected removeShadow(index: number): void {
    this.edit.emit({ shadows: this.shadows().filter((_, i) => i !== index) });
  }

  protected setShadow(index: number, changes: Partial<ShadowProps>): void {
    this.edit.emit({
      shadows: this.shadows().map((s, i) => (i === index ? { ...s, ...changes } : s)),
    });
  }

  protected setShadowNumber(
    index: number,
    field: 'x' | 'y' | 'blur' | 'spread',
    value: number | null,
  ): void {
    if (value !== null) this.setShadow(index, { [field]: value });
  }

  protected detachShadows(): void {
    const resolved = this.shadowToken()?.shadow;
    if (!resolved) return;
    this.edit.emit({
      shadows: resolved.layers.map((layer) => ({
        x: layer.x,
        y: layer.y,
        blur: layer.blur,
        spread: layer.spread,
        inset: layer.inset,
        visible: true,
        color: { l: layer.color.l, c: layer.color.c, h: layer.color.h, a: layer.color.a },
      })),
    });
  }

  private compositeToken(value: unknown) {
    if (!isToken(value)) return null;
    const row = this.tokens().find((t) => t.id === value.token);
    const resolved = row?.resolved;
    const border = resolved && 'style' in resolved ? resolved : null;
    const shadow = resolved && 'layers' in resolved ? resolved : null;
    const text = border
      ? `${border.width}px ${border.style}`
      : shadow
        ? `${shadow.layers.length} ${shadow.layers.length === 1 ? 'layer' : 'layers'}`
        : '';
    return {
      identifier: row?.identifier ?? 'missing token',
      text,
      swatch: border?.color.hex ?? '',
      border,
      shadow,
    };
  }

  // ----- Token picker -----

  protected openPicker(key: FieldKey, origin: PickRequest): void {
    const current = this.picker();
    this.closePicker();
    if (current?.key !== key) this.picker.set({ key, origin });
  }

  protected boundId(): string | null {
    const picker = this.picker();
    const node = this.node();
    if (!picker || !node) return null;
    const lookup: Record<string, unknown> = {
      opacity: node.opacity,
      cornerRadius: node.cornerRadius,
      fill: node.fill,
      gap: node.layout.gap,
      stroke: node.stroke,
      shadows: node.shadows,
      strokeColor: this.stroke()?.color,
      strokeWidth: this.stroke()?.width,
    };
    const value = lookup[picker.key];
    return isToken(value) ? value.token : null;
  }

  protected previewToken(id: string | null): void {
    const picker = this.picker();
    if (!picker) return;
    const patch = id ? this.patchFor(picker.key, { token: id }) : null;
    if (patch) {
      this.previewing = true;
      this.previewEdit.emit(patch);
    } else if (this.previewing) {
      this.previewing = false;
      this.cancelPreview.emit();
    }
  }

  protected pickToken(id: string): void {
    const picker = this.picker();
    const patch = picker ? this.patchFor(picker.key, { token: id }) : null;
    if (patch) {
      this.previewEdit.emit(patch);
      this.previewing = false;
      this.commitPreview.emit();
    }
    this.picker.set(null);
  }

  protected closePicker(): void {
    if (this.previewing) {
      this.previewing = false;
      this.cancelPreview.emit();
    }
    this.picker.set(null);
  }
}
