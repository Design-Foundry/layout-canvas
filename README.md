# canvas

Design canvas in the spirit of Figma, with native editable components, typography,
vectors, drawing tools and local raster images. See [editor capabilities and limits](docs/editor-features.md).

| Folder        | What                                              | Stack                          |
|---------------|---------------------------------------------------|--------------------------------|
| `visualcore/` | document, ops, undo, camera, hit-testing, render  | C++20 → WASM (Emscripten, WebGPU) |
| `bridge/`     | typed wrapper over the WASM module, owns lifetimes | TypeScript                     |
| `client/`     | editor chrome: toolbar, layers, inspector, tokens | Angular 22                     |
| `server/`     | documents, op log, RF cache                       | ASP.NET, .NET 10 minimal APIs  |

## Getting started

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

## Components and portable files

Open **Assets** to search main components, create linked instances, export a
portable Canvas component or import one into a separate document. Imported files
include required main components and tokens and are validated natively before
storage. The editor supports visibility/text/swap properties, variant axes and
independent instance overrides. Importing a component never overwrites its source
document.

Static PNG/JPEG files can also be imported from Assets. Source dimensions and size
are validated before decoding; pixels are explicitly reduced to at most 512 px per
side and stored in the document. See the feature inventory for resource limits.

Additional checks (Node.js 24 or newer):

    npm run test:integration
    npm run test:server-integration  # a local Canvas server on :5180 is required

## MCP

The server exposes an MCP endpoint at http://localhost:5180/mcp (Streamable HTTP) that answers
only requests from this machine. Agents can list, create and import documents, read tokens, layers
and laid-out boxes, and apply ops through the same log the editor syncs with. `get_format_guide`
describes the document format. With the server running, add it to Claude Code:

    claude mcp add --transport http canvas http://localhost:5180/mcp

## Authors and contributors

Human authors are listed in [AUTHORS](AUTHORS). Copyright remains with those
individuals; the project name is not a legal entity or an assignment of rights.
[CONTRIBUTORS](CONTRIBUTORS) is generated from Git history with `.mailmap` applied:

    npm run contributors
    npm run contributors:check

Attribution files do not grant a license or transfer copyright. Bundled fonts and
third-party source retain their respective license notices.
