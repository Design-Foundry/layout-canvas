import { Component, effect, input, model, output } from '@angular/core';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import { RouterLink } from '@angular/router';
import { LucentIconButton, LucentInlineInput, LucentTooltip } from '@design-foundry/lucent';
import { EDITOR_TOOLS, EditorTool } from './editor-model';

@Component({
  selector: 'app-editor-toolbar',
  imports: [ReactiveFormsModule, RouterLink, LucentIconButton, LucentInlineInput, LucentTooltip],
  template: `
    <div class="start">
      <lucent-icon-button
        icon="arrow_back"
        variant="ghost"
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
          [variant]="tool() === option.id ? 'secondary' : 'ghost'"
          [ariaLabel]="option.label"
          [lucentTooltip]="option.label"
          lucentTooltipPosition="bottom"
          (clicked)="tool.set(option.id)"
        />
      }
    </div>
    <div class="end"></div>
  `,
  styleUrl: './editor-toolbar.scss',
})
export class EditorToolbar {
  readonly name = input.required<string>();
  readonly tool = model.required<EditorTool>();
  readonly rename = output<string>();

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
