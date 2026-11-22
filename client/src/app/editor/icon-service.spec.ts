import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { TestBed } from '@angular/core/testing';
import type { UsedIcon } from '@canvas/bridge';
import { IconService, glyphParams } from './icon-service';

const SET = '00000000-0000-0000-0000-000000000200';
const back: UsedIcon = {
  key: 'material-symbols/arrow_back?style=outlined&FILL=0&wght=400&opsz=24',
  icon: { set: SET, glyph: 'arrow_back' },
};
const home: UsedIcon = {
  key: 'material-symbols/home?style=rounded&FILL=1&wght=500&opsz=24',
  icon: { set: SET, glyph: 'home', axes: { style: 'rounded', FILL: 1, wght: 500 } },
};
const url = (glyph: string) => `/api/icons/material-symbols/glyphs/${glyph}`;

function setup(used: UsedIcon[] = [back, home]) {
  TestBed.configureTestingModule({ providers: [provideHttpClient(), provideHttpClientTesting()] });
  const core = {
    usedIcons: vi.fn(() => used),
    registerIcon: vi.fn((): string | null => null),
  };
  return { core, service: TestBed.inject(IconService), http: TestBed.inject(HttpTestingController) };
}
const flush = () => new Promise((resolve) => setTimeout(resolve));

describe('icon service loading', () => {
  it('fetches the SVG of each used icon with only its non-default axes and registers it under its key', async () => {
    const { core, service, http } = setup();
    service.attach(core);

    const plain = http.expectOne((r) => r.url === url('arrow_back'));
    expect(plain.request.params.keys()).toEqual([]);
    plain.flush('<svg id="back"/>');
    const filled = http.expectOne((r) => r.url === url('home'));
    expect(filled.request.params.get('style')).toBe('rounded');
    expect(filled.request.params.get('FILL')).toBe('1');
    expect(filled.request.params.get('wght')).toBe('500');
    filled.flush('<svg id="home"/>');
    await flush();

    expect(core.registerIcon).toHaveBeenCalledWith(back.key, '<svg id="back"/>');
    expect(core.registerIcon).toHaveBeenCalledWith(home.key, '<svg id="home"/>');
    expect(service.loadedIcons()).toBe(2);

    service.refresh();
    http.expectNone(() => true);
  });

  it('does not ask twice while a request is pending, and loads icons added later', async () => {
    const used: UsedIcon[] = [back];
    const { core, service, http } = setup(used);
    service.attach(core);
    service.refresh();
    const first = http.match((r) => r.url === url('arrow_back'));
    expect(first).toHaveLength(1);
    first[0].flush('<svg/>');
    await flush();

    used.push(home);
    service.refresh();
    http.expectNone((r) => r.url === url('arrow_back'));
    http.expectOne((r) => r.url === url('home')).flush('<svg/>');
  });

  it('leaves a glyph the source lacks as a missing glyph and never asks again', async () => {
    const { core, service, http } = setup([back]);
    service.attach(core);
    http.expectOne((r) => r.url === url('arrow_back')).flush('', { status: 404, statusText: 'Not Found' });
    await flush();

    expect(core.registerIcon).not.toHaveBeenCalled();
    service.refresh();
    http.expectNone(() => true);
  });

  it('retries a failed load after a minute and not before', async () => {
    const { core, service, http } = setup([back]);
    vi.spyOn(console, 'warn').mockImplementation(() => {});
    const now = vi.spyOn(Date, 'now');
    now.mockReturnValue(1_000);
    service.attach(core);
    http.expectOne((r) => r.url === url('arrow_back')).flush('', { status: 502, statusText: 'Bad Gateway' });
    await flush();

    now.mockReturnValue(30_000);
    service.refresh();
    http.expectNone(() => true);
    now.mockReturnValue(62_000);
    service.refresh();
    http.expectOne((r) => r.url === url('arrow_back')).flush('<svg/>');
    await flush();
    expect(core.registerIcon).toHaveBeenCalledWith(back.key, '<svg/>');
    now.mockRestore();
  });

  it('reports why the renderer refused an SVG and tries again later', async () => {
    const { core, service, http } = setup([back]);
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    core.registerIcon.mockReturnValue('the SVG uses an element outside path');
    service.attach(core);
    http.expectOne((r) => r.url === url('arrow_back')).flush('<svg><script/></svg>');
    await flush();

    expect(warn).toHaveBeenCalled();
    expect(service.loadedIcons()).toBe(0);
  });

  it('starts over when attached to another document core', async () => {
    const { core, service, http } = setup([back]);
    service.attach(core);
    http.expectOne((r) => r.url === url('arrow_back')).flush('<svg/>');
    await flush();

    const other = { usedIcons: vi.fn(() => [back]), registerIcon: vi.fn((): string | null => null) };
    service.attach(other);
    http.expectOne((r) => r.url === url('arrow_back')).flush('<svg/>');
    await flush();
    expect(other.registerIcon).toHaveBeenCalledOnce();
  });
});

describe('icon service search', () => {
  it('searches the glyph names endpoint', async () => {
    const { service, http } = setup();
    const result = service.search('arrow');
    const request = http.expectOne((r) => r.url === '/api/icons/material-symbols/glyphs');
    expect(request.request.params.get('query')).toBe('arrow');
    expect(request.request.params.get('limit')).toBe('30');
    request.flush({ version: 'abc', total: 41, offset: 0, items: [{ glyph: 'arrow_back' }, { glyph: 'arrow_forward' }] });

    expect(await result).toEqual({ glyphs: ['arrow_back', 'arrow_forward'], total: 41 });
  });
});

describe('glyph params', () => {
  it('lists only the axes an icon sets', () => {
    expect(glyphParams({ set: SET, glyph: 'home' })).toEqual({});
    expect(glyphParams(home.icon)).toEqual({ style: 'rounded', FILL: 1, wght: 500 });
  });
});
