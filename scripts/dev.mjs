import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));

const processes = [
  spawn('dotnet', ['run', '--project', 'server/src/Canvas.Server', '--launch-profile', 'http'], {
    cwd: root,
    stdio: 'inherit',
  }),
  spawn('pnpm', ['start'], { cwd: root, stdio: 'inherit', shell: true }),
];

const stop = () => {
  for (const child of processes) {
    if (child.exitCode === null) {
      spawn('taskkill', ['/pid', String(child.pid), '/t', '/f'], { stdio: 'ignore' });
    }
  }
};

for (const child of processes) {
  child.on('exit', () => {
    stop();
    process.exit();
  });
}

process.on('SIGINT', stop);
process.on('exit', stop);
