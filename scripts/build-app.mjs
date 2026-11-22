import { execFileSync } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const output = `${root}dist/app`;
const nativeDir = `${root}visualcore/build/native-release`;

const run = (command, args) => {
  console.log(`\n> ${command} ${args.join(' ')}`);
  execFileSync(command, args, { cwd: root, stdio: 'inherit', shell: process.platform === 'win32' && command === 'pnpm' });
};

run('pnpm', ['native:release']);
run('pnpm', ['wasm:release']);
run('pnpm', ['build']);

rmSync(output, { recursive: true, force: true });
run('dotnet', [
  'publish',
  'server/src/Canvas.Server',
  '-c', 'Release',
  '-o', output,
  '-r', 'win-x64',
  '--self-contained',
  `-p:VisualCoreNativeDir=${nativeDir}`,
]);

const client = `${root}client/dist/client/browser`;
if (!existsSync(client)) throw new Error(`Client build not found at ${client}`);
mkdirSync(`${output}/wwwroot`, { recursive: true });
cpSync(client, `${output}/wwwroot`, { recursive: true });

const archive = `${root}dist/Canvas-win-x64.zip`;
rmSync(archive, { force: true });
run(process.platform === 'win32' ? join(process.env.SystemRoot, 'System32', 'tar.exe') : 'tar', ['-a', '-c', '-f', archive, '-C', output, '.']);

console.log(`\nPackaged ${archive}`);
