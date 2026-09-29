export type EditorTool = 'select' | 'frame' | 'rectangle' | 'hand';

export interface EditorToolOption {
  readonly id: EditorTool;
  readonly label: string;
  readonly icon: string;
}

export const EDITOR_TOOLS: readonly EditorToolOption[] = [
  { id: 'select', label: 'Move', icon: 'near_me' },
  { id: 'frame', label: 'Frame', icon: 'tag' },
  { id: 'rectangle', label: 'Rectangle', icon: 'crop_square' },
  { id: 'hand', label: 'Hand', icon: 'pan_tool' },
];

export type LayerKind = 'frame' | 'rectangle' | 'text' | 'image' | 'instance';

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
  text: 'title',
  image: 'image',
  instance: 'widgets',
};
