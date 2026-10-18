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

const PX_PER_REM = 16;
const PX_PER_PT = 4 / 3;

/** A dimension in canvas pixels, converted the way visualcore converts it. */
export function lengthInPx({ value, unit }: TokenMagnitude): number {
  if (unit === 'rem' || unit === 'em') return value * PX_PER_REM;
  if (unit === 'pt') return value * PX_PER_PT;
  return value;
}
