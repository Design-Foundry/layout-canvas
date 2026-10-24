import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const target = new URL('../CONTRIBUTORS', import.meta.url);
const check = process.argv.includes('--check');
const history = execFileSync('git', ['log', 'HEAD', '--format=%aN <%aE>'], {
  cwd: root,
  encoding: 'utf8',
});

// .mailmap joins multiple public identities belonging to the same contributor.
const identities = [...new Set(history.trim().split(/\r?\n/).filter(Boolean))].sort();
// Historical tool credits are retained separately from human attribution.
const tools = identities.filter((identity) => /<noreply@anthropic\.com>$/i.test(identity));
const people = identities.filter((identity) => !tools.includes(identity));
const content = [
  'Canvas contributors',
  '===================',
  '',
  'Generated from the reachable Git history of HEAD with .mailmap applied.',
  'Regenerate with: npm run contributors',
  '',
  'Git contributor identities',
  '--------------------------',
  ...people.map((identity) => `- ${identity}`),
  '',
  ...(tools.length
    ? [
        'Historical tool credits',
        '-----------------------',
        ...tools.map((identity) => `- ${identity}`),
        '',
      ]
    : []),
  'Git identities are contribution credits, not a determination of copyright',
  'ownership. Tool credits do not designate human authors or rights holders.',
  'See AUTHORS for the human author attribution maintained by the project.',
  '',
].join('\n');

if (check) {
  if (readFileSync(target, 'utf8').replaceAll('\r\n', '\n') !== content) {
    console.error('CONTRIBUTORS is out of date. Run npm run contributors.');
    process.exitCode = 1;
  }
} else {
  writeFileSync(target, content);
}
