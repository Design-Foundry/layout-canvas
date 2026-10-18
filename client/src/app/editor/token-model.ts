import { TokenMagnitude, TokenType, TokenUnit } from '@canvas/bridge';

export interface TokenFamily {
  readonly type: TokenType;
  readonly label: string;
  /** Units the family accepts. Empty for unitless families. */
  readonly units: readonly TokenUnit[];
}

export const TOKEN_FAMILIES: readonly TokenFamily[] = [
  { type: 'color', label: 'Color', units: [] },
  { type: 'dimension', label: 'Dimension', units: ['px', 'rem', 'em', 'pt'] },
  { type: 'duration', label: 'Duration', units: ['ms', 's'] },
  { type: 'ratio', label: 'Ratio', units: [] },
  { type: 'opacity', label: 'Opacity', units: [] },
];

export function formatMagnitude({ value, unit }: TokenMagnitude): string {
  return `${Math.round(value * 1000) / 1000}${unit ?? ''}`;
}
