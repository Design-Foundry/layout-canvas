import assert from 'node:assert/strict';
import { test } from 'node:test';
import { exportComponent } from '../../client/src/app/editor/component-package.ts';

const origin = new URL(process.env.CANVAS_TEST_URL || 'http://127.0.0.1:5180');

assert.ok(
  ['127.0.0.1', 'localhost', '[::1]'].includes(origin.hostname),
  'Live tests require a local Canvas test server.',
);

const request = (path, body, method = 'POST') =>
  fetch(new URL(path, origin), {
    method,
    headers: { 'Content-Type': 'application/json' },
    ...(body ? { body: JSON.stringify(body) } : {}),
  });

const id = (n) => `00000000-0000-0000-0000-${String(n).padStart(12, '0')}`;

test('native lines round-trip HTTP with zero dimensions, instance overrides and downgrade protection', async () => {
  const snapshot = {
    version: 12,
    modes: [{ id: id(1), name: 'Default', order: 'a0' }],
    tokens: [],
    nodes: [
      { id: id(10), type: 'component', order: 'a0', w: 200, h: 120 },
      {
        id: id(11),
        type: 'line',
        parent: id(10),
        order: 'a0',
        x: 10,
        y: 20,
        w: 160,
        h: 0,
        lineWidth: 4,
        lineReverse: false,
      },
      { id: id(12), type: 'instance', component: id(10), order: 'a1', w: 200, h: 120 },
    ],
  };
  let documentId;
  try {
    const response = await request('/api/documents/import', { name: 'Native line QA', snapshot });
    const body = await response.json();
    assert.equal(response.status, 201, JSON.stringify(body));
    documentId = body.id;
    const loaded = await (await fetch(new URL(`/api/documents/${documentId}`, origin))).json();
    const line = loaded.snapshot.data.nodes.find((node) => node.type === 'line');
    assert.equal(line.h, 0);
    assert.equal(line.lineWidth, 4);
    assert.equal(line.lineReverse, false);
    const old = structuredClone(snapshot);
    old.version = 11;
    assert.equal(
      (await request('/api/documents/import', { name: 'Invalid line downgrade', snapshot: old }))
        .status,
      400,
    );
    const append = async (kind, data) => {
      const response = await request(`/api/documents/${documentId}/ops`, {
        ops: [{ id: crypto.randomUUID(), kind, data }],
      });
      assert.equal(response.status, 200);
      return (await response.json()).acks[0];
    };
    assert.ok(
      (await append('SetOverride', { id: id(12), node: id(11), patch: { lineWidth: 8 } })).seq,
    );
    assert.ok((await append('SetProps', { id: id(11), patch: { lineReverse: true, h: 80 } })).seq);
    assert.equal((await append('SetProps', { id: id(11), patch: { cornerRadius: 4 } })).seq, null);
    assert.equal((await append('SetProps', { id: id(10), patch: { lineWidth: 4 } })).seq, null);
    const ops = await (
      await fetch(new URL(`/api/documents/${documentId}/ops?after=1`, origin))
    ).json();
    assert.equal(ops.ops.length, 2);
  } finally {
    if (documentId)
      assert.equal((await request(`/api/documents/${documentId}`, null, 'DELETE')).status, 204);
  }
});

test('shared typography survives native HTTP storage and refuses deletion while referenced', async () => {
  const typography = {
    id: id(90),
    type: 'typography',
    identifier: 'type.body',
    order: 'a0',
    value: {
      font: 'inter-regular-v1',
      fontSize: 24,
      lineHeight: 1.4,
      letterSpacing: 0,
      align: 'left',
    },
  };
  const snapshot = {
    version: 11,
    modes: [{ id: id(1), name: 'Default', order: 'a0' }],
    tokens: [typography],
    nodes: [
      { id: id(10), type: 'component', order: 'a0', w: 200, h: 80 },
      {
        id: id(11),
        type: 'text',
        parent: id(10),
        order: 'a0',
        typography: id(90),
        text: {
          content: 'Texto local',
          font: 'inter-regular-v1',
          fontSize: 16,
          lineHeight: 1.4,
          letterSpacing: 0,
          align: 'left',
        },
        widthSizing: 'hug',
        heightSizing: 'hug',
      },
      { id: id(12), type: 'instance', component: id(10), order: 'a1', w: 200, h: 80 },
    ],
  };
  let documentId;
  try {
    const response = await request('/api/documents/import', {
      name: 'Shared typography QA',
      snapshot,
    });
    const imported = await response.json();
    assert.equal(response.status, 201, JSON.stringify(imported));
    documentId = imported.id;
    const loaded = await (await fetch(new URL(`/api/documents/${documentId}`, origin))).json();
    assert.equal(loaded.snapshot.data.version, 11);
    assert.deepEqual(loaded.snapshot.data.tokens[0].value, typography.value);
    assert.equal(loaded.snapshot.data.nodes.find((node) => node.id === id(11)).typography, id(90));
    const old = structuredClone(snapshot);
    old.version = 10;
    assert.equal(
      (await request('/api/documents/import', { name: 'Invalid shared downgrade', snapshot: old }))
        .status,
      400,
    );
    const append = async (kind, data) => {
      const response = await request(`/api/documents/${documentId}/ops`, {
        ops: [{ id: crypto.randomUUID(), kind, data }],
      });
      assert.equal(response.status, 200);
      return (await response.json()).acks[0];
    };
    assert.equal((await append('DeleteToken', { id: id(90) })).seq, null);
    const updated = structuredClone(typography);
    updated.value.fontSize = 36;
    updated.value.font = 'inter-oblique-v1';
    assert.ok((await append('ReplaceToken', { token: updated })).seq);
    assert.ok(
      (
        await append('SetOverride', {
          id: id(12),
          node: id(11),
          patch: { textStyle: { font: 'inter-bold-oblique-v1' } },
        })
      ).seq,
    );
    assert.equal(
      (await append('SetOverride', { id: id(12), node: id(11), patch: { typography: id(90) } }))
        .seq,
      null,
    );
    assert.equal(
      (await append('SetProps', { id: id(10), patch: { typography: id(90) } })).seq,
      null,
    );
    const ops = await (
      await fetch(new URL(`/api/documents/${documentId}/ops?after=1`, origin))
    ).json();
    assert.equal(ops.ops.length, 2);
    assert.equal(ops.ops[0].data.token.value.fontSize, 36);
  } finally {
    if (documentId)
      assert.equal((await request(`/api/documents/${documentId}`, null, 'DELETE')).status, 204);
  }
});

function packageFixture() {
  return exportComponent(
    {
      version: 2,
      modes: [{ id: id(1), name: 'Default', order: 'a0' }],
      tokens: [
        {
          id: id(100),
          type: 'color',
          identifier: 'primary',
          order: 'a0',
          value: { l: 0.6, c: 0.2, h: 200 },
        },
        {
          id: id(101),
          type: 'color',
          identifier: 'action',
          order: 'a1',
          source: { [id(1)]: id(100) },
        },
      ],
      nodes: [
        {
          id: id(10),
          type: 'component',
          name: 'Native QA',
          order: 'a0',
          w: 160,
          h: 48,
          fill: { token: id(101) },
        },
        {
          id: id(11),
          type: 'rect',
          parent: id(10),
          order: 'a0',
          x: 8,
          y: 8,
          w: 24,
          h: 24,
          fill: { l: 1, c: 0, h: 0, a: 1 },
        },
      ],
    },
    id(10),
  );
}

for (const [version, font] of [
  [9, 'inter-bold-v1'],
  [10, 'inter-oblique-v1'],
  [10, 'inter-bold-oblique-v1'],
]) {
  test(`snapshot ${version} preserves ${font} and independent regular overrides through native HTTP`, async () => {
    const snapshot = {
      version,
      modes: [{ id: id(1), name: 'Default', order: 'a0' }],
      tokens: [],
      nodes: [
        {
          id: id(10),
          type: 'component',
          order: 'a0',
          w: 160,
          h: 70,
          definition: {
            properties: [],
            axes: [],
            rules: [],
            texts: [{ name: 'label', target: id(11), default: 'Continuar' }],
          },
        },
        {
          id: id(11),
          parent: id(10),
          type: 'text',
          order: 'a0',
          w: 140,
          h: 40,
          text: {
            content: 'Continuar',
            font,
            fontSize: 24,
            lineHeight: 1.4,
            letterSpacing: 0,
            align: 'left',
          },
        },
        { id: id(12), type: 'instance', component: id(10), order: 'a1', w: 160, h: 70 },
      ],
    };
    let documentId;
    try {
      const created = await request('/api/documents/import', {
        name: 'Trusted font selection QA',
        snapshot,
      });
      const body = await created.json();
      assert.equal(created.status, 201, JSON.stringify(body));
      documentId = body.id;
      const loaded = await (await fetch(new URL(`/api/documents/${documentId}`, origin))).json();
      assert.equal(loaded.snapshot.data.nodes.find((node) => node.type === 'text').text.font, font);
      const old = structuredClone(snapshot);
      old.version = version - 1;
      assert.equal(
        (
          await request('/api/documents/import', {
            name: 'Invalid font downgrade QA',
            snapshot: old,
          })
        ).status,
        400,
      );
      const append = async (font) => {
        const response = await request(`/api/documents/${documentId}/ops`, {
          ops: [
            {
              id: crypto.randomUUID(),
              kind: 'SetOverride',
              data: { id: id(12), node: id(11), patch: { textStyle: { font } } },
            },
          ],
        });
        return { status: response.status, body: await response.json() };
      };
      const valid = await append('inter-regular-v1');
      assert.equal(valid.status, 200);
      assert.ok(valid.body.acks[0].seq > 1);
      assert.equal((await append('unknown-face')).status, 400);
      const ops = await (
        await fetch(new URL(`/api/documents/${documentId}/ops?after=1`, origin))
      ).json();
      assert.equal(ops.ops.length, 1);
      assert.equal(ops.ops[0].data.patch.textStyle.font, 'inter-regular-v1');
    } finally {
      if (documentId)
        assert.equal((await request(`/api/documents/${documentId}`, null, 'DELETE')).status, 204);
    }
  });
}

test('snapshot 8 wrapping survives native HTTP import, downgrade refusal and an explicit off edit', async () => {
  const snapshot = {
    version: 8,
    modes: [{ id: id(1), name: 'Default', order: 'a0' }],
    tokens: [],
    nodes: [
      { id: id(10), type: 'component', order: 'a0', w: 160, h: 120 },
      {
        id: id(11),
        parent: id(10),
        type: 'text',
        order: 'a0',
        w: 80,
        h: 20,
        heightSizing: 'hug',
        textWrap: true,
        text: {
          content: 'Uno dos tres cuatro cinco',
          font: 'inter-regular-v1',
          fontSize: 16,
          lineHeight: 1.4,
          letterSpacing: 0,
          align: 'left',
        },
      },
    ],
  };
  let documentId;
  try {
    const created = await request('/api/documents/import', {
      name: 'Wrapped native text QA',
      snapshot,
    });
    const body = await created.json();
    assert.equal(created.status, 201, JSON.stringify(body));
    documentId = body.id;
    const opened = await (await fetch(new URL(`/api/documents/${documentId}`, origin))).json();
    assert.equal(opened.snapshot.data.version, 8);
    assert.equal(opened.snapshot.data.nodes.find((n) => n.type === 'text').textWrap, true);
    const old = structuredClone(snapshot);
    old.version = 7;
    assert.equal(
      (await request('/api/documents/import', { name: 'Invalid downgrade QA', snapshot: old }))
        .status,
      400,
    );
    const edit = await request(`/api/documents/${documentId}/ops`, {
      ops: [
        {
          id: crypto.randomUUID(),
          kind: 'SetProps',
          data: { id: id(11), patch: { textWrap: false } },
        },
      ],
    });
    assert.equal(edit.status, 200);
    assert.ok((await edit.json()).acks[0].seq > 1);
    const ops = await (
      await fetch(new URL(`/api/documents/${documentId}/ops?after=1`, origin))
    ).json();
    assert.equal(ops.ops.at(-1).data.patch.textWrap, false);
  } finally {
    if (documentId)
      assert.equal((await request(`/api/documents/${documentId}`, null, 'DELETE')).status, 204);
  }
});

test('portable Canvas import persists aliases, reloads and accepts subsequent edits', async () => {
  const imported = packageFixture();
  let documentId;
  try {
    const response = await request('/api/documents/import', {
      name: imported.name,
      snapshot: imported.snapshot,
    });
    const body = await response.json();
    assert.equal(response.status, 201, JSON.stringify(body));
    documentId = body.id;
    const opened = await (await fetch(new URL(`/api/documents/${documentId}`, origin))).json();
    assert.equal(opened.snapshot.seq, 1);
    assert.equal(opened.snapshot.data.nodes.length, 2);
    assert.equal(opened.snapshot.data.tokens.length, 2);
    const color = opened.snapshot.data.tokens.find((token) => token.identifier === 'primary');
    const alias = opened.snapshot.data.tokens.find((token) => token.identifier === 'action');
    assert.equal(alias.source[id(1)], color.id);
    const node = opened.snapshot.data.nodes.find((node) => node.id === imported.componentId);
    assert.equal(node.fill.token, alias.id);
    const edited = await request(`/api/documents/${documentId}/ops`, {
      ops: [
        {
          id: crypto.randomUUID(),
          kind: 'SetProps',
          data: { id: imported.componentId, patch: { name: 'Edited after import' } },
        },
      ],
    });
    const ack = await edited.json();
    assert.equal(edited.status, 200);
    assert.equal(ack.acks[0].seq, 2);
    const ops = await (
      await fetch(new URL(`/api/documents/${documentId}/ops?after=1`, origin))
    ).json();
    assert.equal(ops.ops[0].data.patch.name, 'Edited after import');
  } finally {
    if (documentId) {
      assert.equal((await request(`/api/documents/${documentId}`, null, 'DELETE')).status, 204);
      assert.equal((await fetch(new URL(`/api/documents/${documentId}`, origin))).status, 404);
    }
  }
});

test('native rejection leaves no partial document behind', async () => {
  const before = await (await fetch(new URL('/api/documents', origin))).json();
  const bundle = packageFixture();
  bundle.snapshot.nodes[0].fill = { token: id(999) };
  const response = await request('/api/documents/import', {
    name: 'Rejected QA',
    snapshot: bundle.snapshot,
  });
  assert.equal(response.status, 400);
  const after = await (await fetch(new URL('/api/documents', origin))).json();
  assert.deepEqual(
    after.map((document) => document.id).sort(),
    before.map((document) => document.id).sort(),
  );
});

test('partial text variants survive native snapshot 7 import and reload', async () => {
  const source = packageFixture().snapshot;
  source.version = 7;
  const text = {
    content: 'Continuar',
    font: 'inter-regular-v1',
    fontSize: 20,
    lineHeight: 1.4,
    letterSpacing: 0,
    align: 'left',
  };
  source.nodes.push({
    id: id(940),
    type: 'text',
    parent: id(10),
    order: 'a1',
    text,
    widthSizing: 'hug',
    heightSizing: 'hug',
  });
  source.nodes[0].definition = {
    properties: [],
    texts: [{ name: 'label', target: id(940), default: 'Continuar' }],
    axes: [
      { name: 'size', values: ['compact', 'large'], default: 'compact' },
      { name: 'tone', values: ['normal', 'wide'], default: 'normal' },
    ],
    rules: [
      { target: id(940), when: { size: 'large' }, patch: { textStyle: { fontSize: 32 } } },
      {
        target: id(940),
        when: { tone: 'wide' },
        patch: { textStyle: { lineHeight: 2, letterSpacing: 1, align: 'center' } },
      },
    ],
  };
  const imported = exportComponent(source, id(10));
  let documentId;
  try {
    const old = structuredClone(imported.snapshot);
    old.version = 6;
    assert.equal(
      (
        await request('/api/documents/import', {
          name: 'Rejected partial style downgrade',
          snapshot: old,
        })
      ).status,
      400,
    );
    const response = await request('/api/documents/import', {
      name: 'Text variants QA',
      snapshot: imported.snapshot,
    });
    const body = await response.json();
    assert.equal(response.status, 201, JSON.stringify(body));
    documentId = body.id;
    const opened = await (await fetch(new URL(`/api/documents/${documentId}`, origin))).json();
    assert.equal(opened.snapshot.data.version, 7);
    const main = opened.snapshot.data.nodes.find((n) => n.definition?.texts?.length);
    assert.deepEqual(
      main.definition.rules.map((r) => r.patch),
      source.nodes[0].definition.rules.map((r) => r.patch),
    );
    const edited = await request(`/api/documents/${documentId}/ops`, {
      ops: [
        {
          id: crypto.randomUUID(),
          kind: 'SetProps',
          data: { id: main.definition.texts[0].target, patch: { textStyle: { letterSpacing: 2 } } },
        },
      ],
    });
    assert.equal(edited.status, 200);
    assert.equal((await edited.json()).acks[0].seq, 2);
  } finally {
    if (documentId)
      assert.equal((await request(`/api/documents/${documentId}`, null, 'DELETE')).status, 204);
  }
});

test('declared text and composed strings survive native import, reload and subsequent edits', async () => {
  const source = packageFixture().snapshot;
  source.version = 6;
  source.nodes[0].definition = {
    properties: [],
    axes: [],
    rules: [],
    texts: [{ name: 'label', target: id(920), default: 'Continuar' }],
  };
  source.nodes.push({
    id: id(920),
    parent: id(10),
    type: 'text',
    order: 'a1',
    widthSizing: 'hug',
    heightSizing: 'hug',
    text: {
      content: 'Continuar',
      font: 'inter-regular-v1',
      fontSize: 28,
      lineHeight: 1.6,
      letterSpacing: 2,
      align: 'center',
    },
  });
  source.nodes.push({
    id: id(921),
    type: 'component',
    name: 'Text host',
    order: 'a1',
    w: 250,
    h: 70,
  });
  source.nodes.push({
    id: id(922),
    parent: id(921),
    component: id(10),
    type: 'instance',
    order: 'a0',
    w: 220,
    h: 60,
    componentValues: { label: 'Guardar diseño' },
  });
  const imported = exportComponent(source, id(921));
  let documentId;
  try {
    const response = await request('/api/documents/import', {
      name: 'Text interchange QA',
      snapshot: imported.snapshot,
    });
    const body = await response.json();
    assert.equal(response.status, 201, JSON.stringify(body));
    documentId = body.id;
    const opened = await (await fetch(new URL(`/api/documents/${documentId}`, origin))).json();
    const snapshot = opened.snapshot.data;
    assert.equal(snapshot.version, 6);
    assert.deepEqual(
      snapshot.nodes.find((n) => n.type === 'text').text,
      source.nodes.find((n) => n.type === 'text').text,
    );
    const instance = snapshot.nodes.find((n) => n.type === 'instance');
    assert.equal(instance.componentValues.label, 'Guardar diseño');
    const edited = await request(`/api/documents/${documentId}/ops`, {
      ops: [
        {
          id: crypto.randomUUID(),
          kind: 'SetProps',
          data: { id: instance.id, patch: { componentValues: { label: '' } } },
        },
      ],
    });
    assert.equal(edited.status, 200);
    assert.equal((await edited.json()).acks[0].seq, 2);
    const operations = await (
      await fetch(new URL(`/api/documents/${documentId}/ops?after=1`, origin))
    ).json();
    assert.equal(operations.ops[0].data.patch.componentValues.label, '');
  } finally {
    if (documentId)
      assert.equal((await request(`/api/documents/${documentId}`, null, 'DELETE')).status, 204);
  }
});

test('snapshot 6 declared text defaults and long explicit instance strings survive the native HTTP boundary', async () => {
  const source = packageFixture().snapshot;
  source.version = 6;
  const main = source.nodes.find((node) => node.type === 'component');
  main.definition = {
    properties: [],
    axes: [],
    rules: [],
    texts: [{ name: 'label', target: id(903), default: 'Continuar' }],
  };
  source.nodes.push({
    id: id(903),
    parent: main.id,
    type: 'text',
    order: 'z0',
    name: 'Label',
    text: {
      content: 'base',
      font: 'inter-regular-v1',
      fontSize: 24,
      lineHeight: 1.4,
      letterSpacing: 0,
      align: 'left',
    },
  });
  source.nodes.push({
    id: id(904),
    type: 'instance',
    order: 'z1',
    component: main.id,
    componentValues: { label: 'A'.repeat(200) },
    w: 240,
    h: 40,
  });
  const response = await request('/api/documents/import', {
    name: 'Declared text QA',
    snapshot: source,
  });
  assert.equal(response.status, 201, await response.clone().text());
  const imported = await response.json();
  try {
    const opened = await (await fetch(new URL(`/api/documents/${imported.id}`, origin))).json();
    assert.equal(opened.snapshot.data.version, 6);
    assert.equal(
      opened.snapshot.data.nodes.find((node) => node.id === id(904)).componentValues.label,
      'A'.repeat(200),
    );
    const typography = { fontSize: 32, lineHeight: 1.4, letterSpacing: 0, align: 'center' };
    const styleEdit = await request(`/api/documents/${imported.id}/ops`, {
      ops: [
        {
          id: crypto.randomUUID(),
          kind: 'SetOverride',
          data: { id: id(904), node: id(903), patch: { textStyle: typography } },
        },
      ],
    });
    assert.equal(styleEdit.status, 200, await styleEdit.clone().text());
    assert.ok((await styleEdit.json()).acks[0].seq > 1);
    const edit = await request(`/api/documents/${imported.id}/ops`, {
      ops: [
        {
          id: crypto.randomUUID(),
          kind: 'SetProps',
          data: { id: id(904), patch: { componentValues: { label: '' } } },
        },
      ],
    });
    assert.equal(edit.status, 200);
    assert.ok((await edit.json()).acks[0].seq > 1);
    const operations = await (
      await fetch(new URL(`/api/documents/${imported.id}/ops?after=1`, origin))
    ).json();
    assert.deepEqual(operations.ops[0].data.patch.textStyle, typography);
    const wrong = structuredClone(source);
    wrong.version = 5;
    assert.equal(
      (
        await request('/api/documents/import', {
          name: 'Rejected text property downgrade',
          snapshot: wrong,
        })
      ).status,
      400,
    );
  } finally {
    await request(`/api/documents/${imported.id}`, undefined, 'DELETE');
  }
});

test('native text survives snapshot 5 import and reload; unsupported glyph edits and downgrades are rejected', async () => {
  const source = packageFixture().snapshot;
  source.version = 5;
  const root = source.nodes.find((node) => node.type === 'component');
  const text = {
    content: 'Diseño: José\nContinuar',
    font: 'inter-regular-v1',
    fontSize: 24,
    lineHeight: 1.4,
    letterSpacing: 0,
    align: 'left',
  };
  source.nodes.push({
    id: id(902),
    parent: root.id,
    type: 'text',
    order: 'z0',
    name: 'Label',
    w: 120,
    h: 60,
    text,
    widthSizing: 'hug',
    heightSizing: 'hug',
  });
  const response = await request('/api/documents/import', {
    name: 'Native text QA',
    snapshot: source,
  });
  assert.equal(response.status, 201, await response.clone().text());
  const imported = await response.json();
  try {
    const opened = await (await fetch(new URL(`/api/documents/${imported.id}`, origin))).json();
    assert.equal(opened.snapshot.data.version, 5);
    assert.deepEqual(opened.snapshot.data.nodes.find((node) => node.id === id(902)).text, text);
    const append = async (content, expectedStatus = 200) => {
      const response = await request(`/api/documents/${imported.id}/ops`, {
        ops: [
          {
            id: crypto.randomUUID(),
            kind: 'SetProps',
            data: { id: id(902), patch: { text: { ...text, content } } },
          },
        ],
      });
      assert.equal(response.status, expectedStatus);
      if (expectedStatus !== 200) return null;
      return (await response.json()).acks[0];
    };
    assert.equal(await append('😀', 400), null);
    assert.ok((await append('Guardar diseño')).seq > 1);
    const storedOps = await (
      await fetch(new URL(`/api/documents/${imported.id}/ops?after=1`, origin))
    ).json();
    assert.equal(storedOps.ops.length, 1);
    assert.equal(storedOps.ops[0].data.patch.text.content, 'Guardar diseño');
    const invalid = structuredClone(source);
    invalid.nodes.find((node) => node.id === id(902)).text.content = '😀';
    const rejected = await request('/api/documents/import', {
      name: 'Rejected text QA',
      snapshot: invalid,
    });
    assert.equal(rejected.status, 400);
    const downgraded = structuredClone(source);
    downgraded.version = 4;
    assert.equal(
      (
        await request('/api/documents/import', {
          name: 'Rejected text downgrade',
          snapshot: downgraded,
        })
      ).status,
      400,
    );
  } finally {
    await request(`/api/documents/${imported.id}`, undefined, 'DELETE');
  }
});

test('snapshot 4 swap definitions and explicit choices survive import and protect selected dependencies', async () => {
  const source = packageFixture().snapshot;
  source.version = 4;
  source.nodes.push(
    { id: id(20), type: 'component', name: 'Icon A', order: 'a1', w: 24, h: 24 },
    { id: id(30), type: 'component', name: 'Icon B', order: 'a2', w: 24, h: 24 },
    { id: id(12), type: 'instance', parent: id(10), component: id(20), order: 'a1', w: 24, h: 24 },
    {
      id: id(40),
      type: 'instance',
      component: id(10),
      order: 'a3',
      w: 160,
      h: 48,
      componentValues: { icon: id(30) },
    },
  );
  source.nodes[0].definition = {
    properties: [],
    axes: [],
    rules: [],
    swaps: [{ name: 'icon', target: id(12), default: id(20), choices: [id(20), id(30)] }],
  };
  let documentId;
  try {
    const response = await request('/api/documents/import', { name: 'Swap QA', snapshot: source });
    const body = await response.json();
    assert.equal(response.status, 201, JSON.stringify(body));
    documentId = body.id;
    const opened = await (await fetch(new URL(`/api/documents/${documentId}`, origin))).json();
    assert.equal(opened.snapshot.data.version, 4);
    assert.deepEqual(
      opened.snapshot.data.nodes.find((n) => n.id === id(10)).definition,
      source.nodes[0].definition,
    );
    assert.deepEqual(opened.snapshot.data.nodes.find((n) => n.id === id(40)).componentValues, {
      icon: id(30),
    });
    const append = async (kind, data) => {
      const r = await request(`/api/documents/${documentId}/ops`, {
        ops: [{ id: crypto.randomUUID(), kind, data }],
      });
      assert.equal(r.status, 200);
      return (await r.json()).acks[0];
    };
    const narrowed = structuredClone(source.nodes[0].definition);
    narrowed.swaps[0].choices = [id(20)];
    assert.equal(
      (await append('SetComponentDefinition', { id: id(10), definition: narrowed })).seq,
      null,
    );
    assert.equal(
      (await append('SetProps', { id: id(40), patch: { componentValues: { icon: id(999) } } })).seq,
      null,
    );
    assert.ok(
      (await append('SetProps', { id: id(40), patch: { componentValues: { icon: id(20) } } })).seq >
        1,
    );
    assert.ok(
      (await append('SetComponentDefinition', { id: id(10), definition: narrowed })).seq > 1,
    );
    const downgraded = structuredClone(source);
    downgraded.version = 3;
    assert.equal(
      (
        await request('/api/documents/import', {
          name: 'Refused swap downgrade',
          snapshot: downgraded,
        })
      ).status,
      400,
    );
  } finally {
    if (documentId)
      assert.equal((await request(`/api/documents/${documentId}`, null, 'DELETE')).status, 204);
  }
});

test('version 3 definitions, variant choices and schema protection survive the native HTTP boundary', async () => {
  const source = packageFixture();
  source.snapshot.version = 3;
  source.snapshot.nodes[0].definition = {
    properties: [{ name: 'show-icon', target: id(11), default: false }],
    axes: [
      { name: 'size', values: ['compact', 'large'], default: 'compact' },
      { name: 'tone', values: ['neutral', 'danger'], default: 'neutral' },
    ],
    rules: [
      { when: { size: 'large' }, target: id(11), patch: { w: 32 } },
      { when: { size: 'large', tone: 'danger' }, target: id(11), patch: { cornerRadius: 8 } },
    ],
  };
  const imported = source;
  const instanceId = crypto.randomUUID();
  imported.snapshot.nodes.push({
    id: instanceId,
    type: 'instance',
    component: imported.componentId,
    name: 'QA variant instance',
    order: 'a1',
    w: 160,
    h: 48,
    componentValues: { 'show-icon': true, size: 'large', tone: 'danger' },
  });
  let documentId;
  try {
    const response = await request('/api/documents/import', {
      name: 'Version 3 QA',
      snapshot: imported.snapshot,
    });
    const body = await response.json();
    assert.equal(response.status, 201, JSON.stringify(body));
    documentId = body.id;
    const opened = await (await fetch(new URL(`/api/documents/${documentId}`, origin))).json();
    assert.equal(opened.snapshot.data.version, 3);
    const main = opened.snapshot.data.nodes.find((n) => n.id === imported.componentId);
    assert.deepEqual(
      main.definition,
      imported.snapshot.nodes.find((n) => n.id === imported.componentId).definition,
    );
    assert.deepEqual(opened.snapshot.data.nodes.find((n) => n.id === instanceId).componentValues, {
      'show-icon': true,
      size: 'large',
      tone: 'danger',
    });
    const append = async (kind, data) => {
      const response = await request(`/api/documents/${documentId}/ops`, {
        ops: [{ id: crypto.randomUUID(), kind, data }],
      });
      assert.equal(response.status, 200);
      return (await response.json()).acks[0];
    };
    const invalidDefinition = structuredClone(main.definition);
    invalidDefinition.axes[0].values = ['compact'];
    assert.equal(
      (await append('SetComponentDefinition', { id: main.id, definition: invalidDefinition })).seq,
      null,
    );
    const updated = structuredClone(main.definition);
    updated.axes[0].default = 'large';
    assert.ok(
      (await append('SetComponentDefinition', { id: main.id, definition: updated })).seq > 1,
    );
    assert.ok(
      (
        await append('SetProps', {
          id: instanceId,
          patch: { componentValues: { size: 'compact' } },
        })
      ).seq > 1,
    );
    assert.equal(
      (
        await append('SetProps', {
          id: instanceId,
          patch: { componentValues: { size: 'missing' } },
        })
      ).seq,
      null,
    );
    const ops = await (
      await fetch(new URL(`/api/documents/${documentId}/ops?after=1`, origin))
    ).json();
    assert.deepEqual(
      ops.ops.map((op) => op.kind),
      ['SetComponentDefinition', 'SetProps'],
    );
  } finally {
    if (documentId)
      assert.equal((await request(`/api/documents/${documentId}`, null, 'DELETE')).status, 204);
  }
});
