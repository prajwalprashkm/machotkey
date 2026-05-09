import { MonacoLanguageClient } from 'monaco-languageclient';
import { CloseAction, ErrorAction } from 'vscode-languageclient/browser';

console.log('LSP Worker loaded 1');
let onServerMessage = null;

const reader = {
    listen(cb) {
        onServerMessage = cb;
    }
};

const writer = {
    write(msg) {
        self.postMessage({ type: 'lsp-out', payload: msg });
        return Promise.resolve();
    }
};

const client = new MonacoLanguageClient({
    name: 'Lua Language Client (Worker)',
    clientOptions: {
        documentSelector: [{ language: 'lua' }],
        errorHandler: {
            error: () => ({ action: ErrorAction.Continue }),
            closed: () => ({ action: CloseAction.DoNotRestart })
        }
    },
    messageTransports: { reader, writer }
});

client.start();

self.onmessage = (e) => {
    if (e.data.type === 'lsp-in') {
        onServerMessage?.(e.data.payload);
    }
};

console.log('LSP Worker loaded 2');