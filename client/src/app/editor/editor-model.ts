export type EditorTool = 'select' | 'frame' | 'rectangle' | 'ellipse' | 'hand';

export interface EditorToolOption {
  readonly id: EditorTool;
  readonly label: string;
  readonly icon: string;
}

export const EDITOR_TOOLS: readonly EditorToolOption[] = [
  { id: 'select', label: 'Move', icon: 'near_me' },
  { id: 'frame', label: 'Frame', icon: 'tag' },
  { id: 'rectangle', label: 'Rectangle', icon: 'crop_square' },
  { id: 'ellipse', label: 'Ellipse', icon: 'radio_button_unchecked' },
  { id: 'hand', label: 'Hand', icon: 'pan_tool' },
];

export type LayerKind = 'frame' | 'rectangle' | 'ellipse' | 'text' | 'image' | 'instance';

/** One visible row of the layers panel, flattened from the document tree. */
export interface LayerRow {
  readonly id: string;
  readonly name: string;
  readonly kind: LayerKind;
  /** Nesting level, 0 for top-level layers. */
  readonly depth: number;
}

export const LAYER_ICONS: Readonly<Record<LayerKind, string>> = {
  frame: 'tag',
  rectangle: 'crop_square',
  ellipse: 'radio_button_unchecked',
  text: 'title',
  image: 'image',
  instance: 'widgets',
};
