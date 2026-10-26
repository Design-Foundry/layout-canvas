import { Component, effect, input, model, output } from '@angular/core';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import { RouterLink } from '@angular/router';
import { LucentIconButton, LucentInlineInput, LucentTooltip } from '@designfoundry/lucent-angular';
import type { PrototypeScreen } from '@canvas/bridge';
import { EDITOR_TOOLS, EditorTool } from './editor-model';

@Component({
  selector: 'app-editor-toolbar',
  imports: [ReactiveFormsModule, RouterLink, LucentIconButton, LucentInlineInput, LucentTooltip],
  template: `
    <div class="start">
      <lucent-icon-button
        icon="arrow_back"
        variant="text"
        size="sm"
        ariaLabel="Documents"
        lucentTooltip="Documents"
        lucentTooltipPosition="bottom"
        routerLink="/"
      />
      <lucent-inline-input
        class="name"
        typography="typography-label-md"
        [formControl]="nameControl"
        (focusout)="commitName()"
        (keydown.enter)="blurTarget($event)"
        (keydown.escape)="cancelName($event)"
      />
    </div>
    <div class="tools" role="toolbar" aria-label="Tools">
      @for (option of tools; track option.id) {
        <lucent-icon-button
          [icon]="option.icon"
          size="sm"
          [variant]="tool() === option.id ? 'secondary' : 'text'"
          [ariaLabel]="option.label"
          [lucentTooltip]="option.label"
          lucentTooltipPosition="bottom"
          (clicked)="tool.set(option.id)"
        />
      }
    </div>
    <div class="end">
      @if (prototypeScreens().length) {
        <select
          aria-label="Starting screen"
          [value]="prototypeStart()"
          (change)="chooseStart($event)"
        >
          @for (screen of prototypeScreens(); track screen.id) {
            <option [value]="screen.id">{{ (screen.start && screen.flowName) || screen.name || 'Untitled frame' }}</option>
          }
        </select>
      } @else {
        <span class="prototype-hint">Frame (F) to play</span>
      }
      <lucent-icon-button
        icon="play_arrow"
        variant="text"
        size="sm"
        ariaLabel="Play prototype"
        [disabled]="!prototypeScreens().length"
        [lucentTooltip]="
          prototypeScreens().length
            ? 'Play prototype'
            : 'Create a root frame with F to play a prototype'
        "
        (clicked)="play.emit()"
      />
      <lucent-icon-button
        icon="widgets"
        variant="text"
        size="sm"
        ariaLabel="Create component"
        lucentTooltip="Create component"
        lucentTooltipPosition="bottom"
        [disabled]="!canCreateComponent()"
        (clicked)="createComponent.emit()"
      />
    </div>
  `,
  styleUrl: './editor-toolbar.scss',
})
export class EditorToolbar {
  readonly name = input.required<string>();
  readonly tool = model.required<EditorTool>();
  readonly canCreateComponent = input(false);
  readonly rename = output<string>();
  readonly createComponent = output<void>();
  readonly prototypeScreens = input<readonly PrototypeScreen[]>([]);
  readonly prototypeStart = input('');
  readonly choosePrototypeStart = output<string>();
  readonly play = output<void>();
  protected chooseStart(event: Event): void {
    this.choosePrototypeStart.emit((event.target as HTMLSelectElement).value);
  }

  protected readonly tools = EDITOR_TOOLS;
  protected readonly nameControl = new FormControl('', { nonNullable: true });

  constructor() {
    effect(() => this.nameControl.setValue(this.name()));
  }

  protected commitName(): void {
    const name = this.nameControl.value.trim();
    if (!name) {
      this.nameControl.setValue(this.name());
    } else if (name !== this.name()) {
      this.rename.emit(name);
    }
  }

  protected cancelName(event: Event): void {
    this.nameControl.setValue(this.name());
    this.blurTarget(event);
  }

  protected blurTarget(event: Event): void {
    (event.target as HTMLElement).blur();
  }
}
