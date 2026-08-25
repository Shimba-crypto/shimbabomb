const { LanguageClient, TransportKind } = require('vscode-languageclient');
const path = require('path');
const { spawnSync } = require('child_process');

let client;

function findSb() {
    for (const cand of [path.join(process.env.HOME || '', '.local/bin/sb'), '/usr/local/bin/sb', 'sb']) {
        const r = spawnSync(cand, ['--help'], { encoding: 'utf8' });
        if (!r.error) return cand;
    }
    return null;
}

function activate(context) {
    const sb = findSb();
    if (!sb) {
        vscode.window.showWarningMessage('ShimbaBomb: `sb` binary not found — install shimbaomb first.');
        return;
    }
    const serverOptions = {
        command: sb,
        args: ['lsp'],
        transport: TransportKind.stdio,
    };
    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'shimbabomb' }],
    };
    client = new LanguageClient('shimbabombLSP', 'ShimbaBomb LSP', serverOptions, clientOptions);
    client.start();
}

function deactivate() {
    if (client) return client.stop();
    return undefined;
}

module.exports = { activate, deactivate };
