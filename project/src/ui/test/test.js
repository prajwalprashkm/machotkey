import '@codingame/monaco-vscode-lua-default-extension';
import "@codingame/monaco-vscode-theme-defaults-default-extension";

import * as monaco from '@codingame/monaco-vscode-editor-api';
import { initialize } from '@codingame/monaco-vscode-api/services';

// Import essential service overrides
import getEditorServiceOverride from '@codingame/monaco-vscode-editor-service-override';
import getThemeServiceOverride from '@codingame/monaco-vscode-theme-service-override';
import getTextmateServiceOverride from '@codingame/monaco-vscode-textmate-service-override';
import getLanguagesServiceOverride from '@codingame/monaco-vscode-languages-service-override';
import getModelServiceOverride from '@codingame/monaco-vscode-model-service-override';
import getConfigurationServiceOverride from '@codingame/monaco-vscode-configuration-service-override';
import getQuickAccessServiceOverride from '@codingame/monaco-vscode-quickaccess-service-override';
import getStorageServiceOverride from '@codingame/monaco-vscode-storage-service-override';
import getViewsServiceOverride from '@codingame/monaco-vscode-views-service-override';
import getExtensionsServiceOverride from '@codingame/monaco-vscode-extensions-service-override';
import getNotificationsServiceOverride from '@codingame/monaco-vscode-notifications-service-override';
import getDialogsServiceOverride from '@codingame/monaco-vscode-dialogs-service-override';
import getFilesServiceOverride from '@codingame/monaco-vscode-files-service-override';
import getBaseServiceOverride from '@codingame/monaco-vscode-base-service-override';
import { updateUserConfiguration } from '@codingame/monaco-vscode-configuration-service-override';


// Import default VS Code assets (themes, etc.)
import '@codingame/monaco-vscode-theme-defaults-default-extension';

async function setupEditor() {
    const workerLoaders = {
        TextEditorWorker: () => new Worker(new URL('monaco-editor/esm/vs/editor/editor.worker.js', import.meta.url), { type: 'module' }),
        editorWorkerService: () => new Worker(new URL('monaco-editor/esm/vs/editor/editor.worker.js', import.meta.url), { type: 'module' }),
        TextMateWorker: () => new Worker(new URL('@codingame/monaco-vscode-textmate-service-override/worker', import.meta.url), { type: 'module' })
    };
    
    window.MonacoEnvironment = {
        getWorker: function (_workerId, label) {
            console.log("worker requested: ", label);
            const workerFactory = workerLoaders[label]
            if (workerFactory != null) {
                return workerFactory()
            }
            throw new Error(`Worker ${label} not found`)
        }
    }

    // 1. Initialize all required services at once
    await initialize({
        ...getBaseServiceOverride(),
        ...getEditorServiceOverride(),
        ...getThemeServiceOverride(),
        ...getTextmateServiceOverride(),
        ...getLanguagesServiceOverride(),
        ...getModelServiceOverride(),
        ...getConfigurationServiceOverride(),
        ...getQuickAccessServiceOverride(),
        ...getStorageServiceOverride(),
        ...getViewsServiceOverride(),
        ...getExtensionsServiceOverride(),
        ...getNotificationsServiceOverride(),
        ...getDialogsServiceOverride(),
        ...getFilesServiceOverride(),
    });
    
    // After initialize()
    await updateUserConfiguration(`{
        "workbench.colorTheme": "Default Dark Modern"
    }`);

    // 3. Create the editor instance
    const editor = monaco.editor.create(document.getElementById('editor'), {
        value: '-- Write your Lua macro script here\n-- Press Cmd+Enter or click Run to execute\n\nprint("Hello from Lua!")\n',
        language: 'lua',
        theme: 'Default Dark Modern',
        fontSize: 14,
        tabSize: 4,
        minimap: { enabled: true },
        automaticLayout: true,
        scrollBeyondLastLine: false,
        lineNumbers: 'on',
        renderWhitespace: 'selection',
        cursorBlinking: 'smooth',
        smoothScrolling: true,
        fontFamily: "'Monaco', 'Menlo', 'Ubuntu Mono', monospace"
    });
}

setupEditor().catch(console.error);
