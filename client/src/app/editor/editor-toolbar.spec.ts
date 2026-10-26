import { ComponentFixture, TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import { EditorToolbar } from './editor-toolbar';

describe('EditorToolbar', () => {
  let fixture: ComponentFixture<EditorToolbar>;
  let renamed: string[];

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [EditorToolbar],
      providers: [provideRouter([])],
    }).compileComponents();

    fixture = TestBed.createComponent(EditorToolbar);
    fixture.componentRef.setInput('name', 'Landing page');
    fixture.componentRef.setInput('tool', 'select');
    renamed = [];
    fixture.componentInstance.rename.subscribe((name) => renamed.push(name));
    fixture.detectChanges();
  });

  function nameField(): HTMLInputElement {
    return (fixture.nativeElement as HTMLElement).querySelector('lucent-inline-input input')!;
  }

  async function type(value: string): Promise<void> {
    const field = nameField();
    field.value = value;
    field.dispatchEvent(new Event('input'));
    await fixture.whenStable();
    field.dispatchEvent(new FocusEvent('focusout', { bubbles: true }));
    await fixture.whenStable();
  }

  it('selects a tool', () => {
    const buttons = (fixture.nativeElement as HTMLElement).querySelectorAll(
      '[role="toolbar"] button',
    );
    (buttons[2] as HTMLButtonElement).click();

    expect(fixture.componentInstance.tool()).toBe('rectangle');
  });

  it('emits a trimmed new name when the field loses focus', async () => {
    await type('  Checkout  ');

    expect(renamed).toEqual(['Checkout']);
  });

  it('restores the name instead of emitting a blank one', async () => {
    await type('   ');

    expect(renamed).toEqual([]);
    expect(nameField().value).toBe('Landing page');
  });

  it('does not emit when the name is unchanged', async () => {
    await type('Landing page');

    expect(renamed).toEqual([]);
  });
  it('requires an eligible root frame before playing and emits the selected start choice', async () => {
    const play = (fixture.nativeElement as HTMLElement).querySelector<HTMLButtonElement>(
      'button[aria-label="Play prototype"]',
    )!;
    expect(play.disabled).toBe(true);
    fixture.componentRef.setInput('prototypeScreens', [
      { id: 'screen', name: 'Home', start: true, w: 300, h: 500 },
    ]);
    fixture.componentRef.setInput('prototypeStart', 'screen');
    await fixture.whenStable();
    let started = false;
    fixture.componentInstance.play.subscribe(() => (started = true));
    play.click();
    expect(started).toBe(true);
    let selected = '';
    fixture.componentInstance.choosePrototypeStart.subscribe((value) => (selected = value));
    const choice = (fixture.nativeElement as HTMLElement).querySelector('select')!;
    choice.value = 'screen';
    choice.dispatchEvent(new Event('change'));
    expect(selected).toBe('screen');
  });
  it('labels marked starting screens with flow names and keeps ordinary frame names',async()=>{
    fixture.componentRef.setInput('prototypeScreens',[
      {id:'a',name:'Home',start:true,flowName:'Checkout',w:300,h:500},
      {id:'b',name:'Details',start:false,flowName:'Dormant',w:300,h:500},
      {id:'c',name:'Other',start:true,flowName:'',w:300,h:500},
    ]);await fixture.whenStable();
    const options=fixture.nativeElement.querySelectorAll('select option') as NodeListOf<HTMLOptionElement>;
    expect([...options].map(option=>option.textContent?.trim())).toEqual(['Checkout','Details','Other']);
    expect([...options].map(option=>option.value)).toEqual(['a','b','c']);
  });

});
