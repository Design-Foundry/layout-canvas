import { TestBed } from '@angular/core/testing';
import type { NodeProps, NodePatch } from '@canvas/bridge';
import { appendPrototypeConnection, PrototypeConnections, prototypeConnectionPath } from './prototype-connections';

const node = (value: Partial<NodeProps> = {}) => ({ id:'hotspot', type:'rect', ...value }) as NodeProps;
const targets = [
  { id:'home',name:'Home',bounds:{x:100,y:100,w:200,h:300} },
  { id:'details',name:'Details',bounds:{x:400,y:100,w:200,h:300} },
];
async function setup(value = node(), accept = true) {
  await TestBed.configureTestingModule({imports:[PrototypeConnections]}).compileComponents();
  const fixture = TestBed.createComponent(PrototypeConnections), saved:NodePatch[]=[];
  fixture.componentRef.setInput('node',value);
  fixture.componentRef.setInput('source',{x:20,y:30,w:40,h:20});
  fixture.componentRef.setInput('targets',targets);
  fixture.componentRef.setInput('save',(_id:string,patch:NodePatch)=>{saved.push(patch);return accept;});
  await fixture.whenStable();
  return {fixture,saved};
}
describe('visual prototype connections',()=>{
  it('appends click navigation and preserves ordered trigger options',()=>{
    const hover={trigger:'hover',action:'navigate',target:'home',transition:'dissolve',durationMs:700} as const;
    const before=node({interactions:[hover]});
    expect(appendPrototypeConnection(before,'details')).toEqual({interactions:[hover,{trigger:'click',action:'navigate',target:'details'}]});
    expect(before.interactions).toEqual([hover]);
  });
  it('keeps legacy click behavior and refuses shadowed/full edits rather than replacing them',()=>{
    expect(()=>appendPrototypeConnection(node({interaction:{trigger:'click',action:'back'}}),'home')).toThrow(/already has a click/);
    expect(()=>appendPrototypeConnection(node({interactions:Array.from({length:16},()=>({trigger:'hover',action:'back'}))}),'home')).toThrow(/at most 16/);
    expect(()=>appendPrototypeConnection(node({type:'instance'}),'home')).toThrow(/inside the instance/);
    expect(appendPrototypeConnection(node({interactions:[],interaction:{trigger:'click',action:'back'}}),'home')).toEqual({interactions:[{trigger:'click',action:'navigate',target:'home'}]});
  });
  it('draws every available destination and omits stale references',async()=>{
    const {fixture}=await setup(node({interactions:[
      {trigger:'click',action:'navigate',target:'home'},
      {trigger:'hover',action:'open-overlay',target:'details'},
      {trigger:'press',action:'navigate',target:'missing'},
    ]}));
    expect(fixture.nativeElement.querySelectorAll('path')).toHaveLength(2);
    expect(prototypeConnectionPath({x:20,y:30,w:40,h:20},targets[0].bounds)).toBe('M 60 40 C 100 40, 60 250, 100 250');
  });
  it('provides keyboard-operable destination authoring and reports native refusal',async()=>{
    const {fixture,saved}=await setup(node(),false);
    fixture.nativeElement.querySelector('.handle').click();await fixture.whenStable();
    const select=fixture.nativeElement.querySelector('select') as HTMLSelectElement;
    select.value='details';select.dispatchEvent(new Event('change'));await fixture.whenStable();
    const button=[...fixture.nativeElement.querySelectorAll('button')].find((item:any)=>item.textContent.includes('Add click')) as HTMLButtonElement;
    button.click();await fixture.whenStable();
    expect(saved).toEqual([{interactions:[{trigger:'click',action:'navigate',target:'details'}]}]);
    expect(fixture.nativeElement.querySelector('[role=status]').textContent).toContain('No changes saved');
  });
  it('commits a primary pointer drop only on an available screen and cancels on selection change',async()=>{
    const {fixture,saved}=await setup();
    const handle=fixture.nativeElement.querySelector('.handle') as HTMLButtonElement;
    handle.setPointerCapture=()=>{};
    const pointer=(type:string,x:number,y:number)=>{
      const event=new Event(type,{bubbles:true,cancelable:true});
      Object.defineProperties(event,{pointerId:{value:5},isPrimary:{value:true},button:{value:0},clientX:{value:x},clientY:{value:y}});
      handle.dispatchEvent(event);
    };
    pointer('pointerdown',60,40);pointer('pointermove',450,200);pointer('pointerup',450,200);
    await fixture.whenStable();
    expect(saved).toHaveLength(1);expect(saved[0].interactions?.at(-1)).toMatchObject({target:'details'});
    pointer('pointerdown',60,40);
    fixture.componentRef.setInput('node',node({id:'other'}));await fixture.whenStable();
    pointer('pointerup',450,200);expect(saved).toHaveLength(1);
  });
});
