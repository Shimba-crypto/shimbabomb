# ShimbaBomb VS Code extension

Syntax highlighting + live diagnostics + completions for `.sb` files via the built-in LSP (`sb lsp`).

## Install (dev)

```bash
cd ~/shimbabomb/editors/vscode
npm install
npx vsce package          # produces shimbabomb-vscode-1.9.0.vsix
code --install-extension shimbabomb-vscode-1.9.0.vsix
```

Requires the `sb` binary on PATH (installer puts it in `~/.local/bin`).
