import { Component, DestroyRef, ElementRef, afterNextRender, inject, input, output, viewChild } from '@angular/core';

/** Presentation navigation stays keyboard reachable independently of native canvas input. */
@Component({
  selector: 'app-prototype-controls',
  template: `
    <nav #navigation tabindex="-1" aria-label="Prototype presentation" [attr.aria-busy]="transitioning()">
      <button type="button" [disabled]="!canBack()" (click)="back.emit()">Back</button>
      <button type="button" (click)="restart.emit()">Restart</button>
      @if (overlay()) {
        <button type="button" (click)="closeOverlay.emit()">Close overlay</button>
      }
      <span role="status" aria-live="polite" aria-atomic="true">{{ name() }}</span>
      <button type="button" (click)="exit.emit()">Exit (Esc)</button>
    </nav>
  `,
  styles: `
    :host { position:absolute; bottom:18px; left:50%; transform:translateX(-50%); max-width:calc(100% - 32px); }
    nav { display:flex; align-items:center; gap:10px; padding:10px 12px; border-radius:8px; background:#242424; color:#eee; box-shadow:0 4px 20px #0008; flex-wrap:wrap; }
    button { color:inherit; background:#383838; border:1px solid #888; border-radius:4px; padding:6px 10px; white-space:nowrap; }
    button:disabled { opacity:.5; }
    button:focus-visible, nav:focus-visible { outline:3px solid #a6c6ff; outline-offset:3px; }
    span { overflow:hidden; text-overflow:ellipsis; white-space:nowrap; max-width:240px; }
  `,
})
export class PrototypeControls {
  readonly name = input('Prototype');
  readonly canBack = input(false);
  readonly overlay = input(false);
  readonly transitioning = input(false);
  readonly back = output<void>();
  readonly restart = output<void>();
  readonly closeOverlay = output<void>();
  readonly exit = output<void>();
  private readonly navigation = viewChild<ElementRef<HTMLElement>>('navigation');

  constructor() {
    const previous = document.activeElement;
    afterNextRender(() => this.navigation()?.nativeElement.focus());
    inject(DestroyRef).onDestroy(() => {
      // The editing toolbar becomes visible in the same render that removes this player.
      queueMicrotask(() => {
        if (previous instanceof HTMLElement && previous.isConnected) previous.focus();
      });
    });
  }
}
