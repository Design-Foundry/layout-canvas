# canvas

Design canvas in the spirit of Figma, consuming Design Foundry releases (RF) read-only.

| Folder        | What                                              | Stack                          |
|---------------|---------------------------------------------------|--------------------------------|
| `visualcore/` | document, ops, undo, camera, hit-testing, render  | C++20 → WASM (Emscripten, WebGPU) |
| `bridge/`     | typed wrapper over the WASM module, owns lifetimes | TypeScript                     |
| `client/`     | editor chrome: toolbar, layers, inspector, tokens | Angular 22                     |
| `server/`     | documents, op log, RF cache                       | ASP.NET, .NET 10 minimal APIs  |

## Getting started

`@design-foundry/*` packages (such as Lucent) come from GitHub Packages. One-time setup on each machine,
in your user-level `~/.npmrc` (pnpm ignores credentials in the repo's `.npmrc`):

    //npm.pkg.github.com/:_authToken=${NODE_AUTH_TOKEN}

and set `NODE_AUTH_TOKEN` to a GitHub token with `read:packages` (`gh auth token` works).

Then:

    pnpm install
    pnpm wasm          # build visualcore and hand it to bridge/wasm
    pnpm build         # bridge + client

In VS Code, **Run Task → dev: all** (or `Ctrl+Shift+B`) builds the WASM, watches the bridge,
and serves the client on http://localhost:4200 with `/api` proxied to the server on :5180.
**F5 → Full stack** does the same under the debugger.

## Tests

    ctest --preset native-debug      # in visualcore/, from a Dev PowerShell terminal
    pnpm --filter @canvas/client test
    dotnet test server/Canvas.Server.slnx
