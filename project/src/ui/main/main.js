import '@codingame/monaco-vscode-lua-default-extension';
import "@codingame/monaco-vscode-theme-defaults-default-extension";
import '@codingame/monaco-vscode-theme-seti-default-extension';

import * as monaco from '@codingame/monaco-vscode-editor-api';
import * as vscode from 'vscode';
import { initialize } from '@codingame/monaco-vscode-api/services';
import { createModelReference } from '@codingame/monaco-vscode-api/monaco';

// Import essential service overrides
import getEditorServiceOverride from '@codingame/monaco-vscode-editor-service-override';
import getThemeServiceOverride from '@codingame/monaco-vscode-theme-service-override';
import getTextmateServiceOverride from '@codingame/monaco-vscode-textmate-service-override';
import getLanguagesServiceOverride from '@codingame/monaco-vscode-languages-service-override';
import getModelServiceOverride from '@codingame/monaco-vscode-model-service-override';
import getConfigurationServiceOverride from '@codingame/monaco-vscode-configuration-service-override';
import getQuickAccessServiceOverride from '@codingame/monaco-vscode-quickaccess-service-override';
import getStorageServiceOverride from '@codingame/monaco-vscode-storage-service-override';
import getViewsServiceOverride, { renderPart, Parts, attachPart } from '@codingame/monaco-vscode-views-service-override';
import getServiceOverride from '@codingame/monaco-vscode-layout-service-override';
import { createConfiguredEditor } from '@codingame/monaco-vscode-api/monaco';
import getSearchServiceOverride from '@codingame/monaco-vscode-search-service-override';
import getExtensionsServiceOverride from '@codingame/monaco-vscode-extensions-service-override';
import getNotificationsServiceOverride from '@codingame/monaco-vscode-notifications-service-override';
import getDialogsServiceOverride from '@codingame/monaco-vscode-dialogs-service-override';
import getFilesServiceOverride from '@codingame/monaco-vscode-files-service-override';
import getBaseServiceOverride from '@codingame/monaco-vscode-base-service-override';
import { updateUserConfiguration } from '@codingame/monaco-vscode-configuration-service-override';
import { whenReady } from '@codingame/monaco-vscode-lua-default-extension';
import { Part } from '@codingame/monaco-vscode-api/vscode/vs/workbench/browser/part';
import { MonacoLanguageClient } from 'monaco-languageclient';
import { CloseAction, ErrorAction } from 'vscode-languageclient/browser';
// Import custom Lua API completions
import { registerLuaAPI } from './autocomplete';

// Import default VS Code assets (themes, etc.)
import '@codingame/monaco-vscode-theme-defaults-default-extension';

import EditorWorker from 'monaco-editor/esm/vs/editor/editor.worker?worker';
import TextMateWorker from '@codingame/monaco-vscode-textmate-service-override/worker?worker';

let editor;
let currentFilePath = null;
let isRunning = false;
let languageClient = null;
let currentProjectPath = null;
let latestProjectPermissionSettings = null;
let latestGlobalRateSettings = null;
let latestResourceLimits = null;

// Folder support state
let currentWorkspaceFolder = null;
let fileTree = {};
let openFiles = new Map(); // Map of URI string -> { model, file }
let currentFileUri = null;

// File system provider for browser files
class BrowserFileSystemProvider {
    constructor() {
        this.files = new Map(); // path -> File object
        this.folders = new Set();
        this.writeableFiles = new Map(); // path -> content (for new files)
        this._onDidChangeFile = new vscode.EventEmitter();
        this.onDidChangeFile = this._onDidChangeFile.event;
        this.root = '';
    }

    async loadFolder(folderName, fileList) {
        this.files.clear();
        this.folders.clear();
        this.writeableFiles.clear();
    
        // Set root to empty string - we'll use paths like "MyFolder/file.lua"
        this.root = '';
        this.folders.add(folderName);
    
        for (const file of fileList) {
            const relativePath = file.webkitRelativePath
                .split('/')
                .slice(1)
                .join('/');
    
            if (!relativePath) continue;
    
            const fullPath = `${folderName}/${relativePath}`;
            this.files.set(fullPath, file);
    
            const parts = fullPath.split('/');
            for (let i = 0; i < parts.length - 1; i++) {
                this.folders.add(parts.slice(0, i + 1).join('/'));
            }
        }
    
        // Force Explorer refresh by firing change event on root
        this._onDidChangeFile.fire([
            { type: vscode.FileChangeType.Changed, uri: vscode.Uri.parse('browserfs:/') }
        ]);

        return Array.from(this.files.keys()).map(p => p.slice(folderName.length + 1));
    }    

    async readDirectory(uri) {
        const path = uri.path.substring(1); // Remove leading /
        const entries = new Map(); // name -> FileType
        const prefix = path ? path + '/' : '';
    
        // Files (original + writable)
        const allFiles = new Set([
            ...this.files.keys(),
            ...this.writeableFiles.keys()
        ]);
    
        for (const filePath of allFiles) {
            if (!filePath.startsWith(prefix)) continue;
    
            const rest = filePath.slice(prefix.length);
            if (!rest) continue;
    
            const slash = rest.indexOf('/');
            if (slash === -1) {
                entries.set(rest, vscode.FileType.File);
            } else {
                entries.set(rest.slice(0, slash), vscode.FileType.Directory);
            }
        }
    
        // Explicit folders (including empty + root children)
        for (const folderPath of this.folders) {
            if (!folderPath.startsWith(prefix)) continue;
    
            const rest = folderPath.slice(prefix.length);
            if (!rest) continue;
    
            const slash = rest.indexOf('/');
            if (slash === -1) {
                entries.set(rest, vscode.FileType.Directory);
            }
        }
    
        return Array.from(entries.entries());
    }

    async readFile(uri) {
        const path = uri.path.substring(1); // Remove leading /
        
        // Check if it's a writeable file first
        if (this.writeableFiles.has(path)) {
            const content = this.writeableFiles.get(path);
            return new TextEncoder().encode(content);
        }
        
        const file = this.files.get(path);
        if (!file) throw vscode.FileSystemError.FileNotFound(uri);
        
        return new Promise((resolve, reject) => {
            const reader = new FileReader();
            reader.onload = (e) => resolve(new Uint8Array(e.target.result));
            reader.onerror = () => reject(vscode.FileSystemError.Unavailable(uri));
            reader.readAsArrayBuffer(file);
        });
    }

    async writeFile(uri, content, options) {
        const path = uri.path.substring(1);
        
        // Store in writeable files map
        const textContent = new TextDecoder().decode(content);
        this.writeableFiles.set(path, textContent);
        
        // Add to folders if it's in a subdirectory
        const parts = path.split('/');
        for (let i = 0; i < parts.length - 1; i++) {
            const folderPath = parts.slice(0, i + 1).join('/');
            this.folders.add(folderPath);
        }
        
        this._onDidChangeFile.fire([{ type: vscode.FileChangeType.Changed, uri }]);
    }

    async delete(uri, options) {
        const path = uri.path.substring(1);
        this.files.delete(path);
        this.writeableFiles.delete(path);
        this.folders.delete(path);
        this._onDidChangeFile.fire([{ type: vscode.FileChangeType.Deleted, uri }]);
    }

    async rename(oldUri, newUri, options) {
        const oldPath = oldUri.path.substring(1);
        const newPath = newUri.path.substring(1);
        
        if (this.files.has(oldPath)) {
            const file = this.files.get(oldPath);
            this.files.delete(oldPath);
            this.files.set(newPath, file);
        }
        if (this.writeableFiles.has(oldPath)) {
            const content = this.writeableFiles.get(oldPath);
            this.writeableFiles.delete(oldPath);
            this.writeableFiles.set(newPath, content);
        }
        this._onDidChangeFile.fire([
            { type: vscode.FileChangeType.Deleted, uri: oldUri },
            { type: vscode.FileChangeType.Created, uri: newUri }
        ]);
    }

    async createDirectory(uri) {
        const path = uri.path.substring(1);
        this.folders.add(path);
        this._onDidChangeFile.fire([{ type: vscode.FileChangeType.Created, uri }]);
    }

    async stat(uri) {
        const path = uri.path.substring(1);
        
        if (this.writeableFiles.has(path)) {
            const content = this.writeableFiles.get(path);
            return {
                type: vscode.FileType.Directory,
                ctime: Date.now(),
                mtime: Date.now(),
                size: content.length
            };
        }
        
        if (this.files.has(path)) {
            const file = this.files.get(path);
            return {
                type: vscode.FileType.File,
                ctime: file.lastModified,
                mtime: file.lastModified,
                size: file.size
            };
        } else if (this.folders.has(path) || path === '' || path === '/') {
            return {
                type: vscode.FileType.Directory,
                ctime: Date.now(),
                mtime: Date.now(),
                size: 0
            };
        }
        throw vscode.FileSystemError.FileNotFound(uri);
    }

    watch(uri, options) {
        // Simple watch implementation
        return { dispose: () => {} };
    }
}

// Create and register file system provider BEFORE initialization
//const browserFS = new BrowserFileSystemProvider();

console.log_cpp = (msg) => {
    window.callCpp('console.log', msg);
}
console.error_cpp = (msg) => {
    window.callCpp('console.error', msg);
}

async function setupEditor() {
    // Polyfill for getSelection on shadow roots
    /*if (typeof ShadowRoot !== 'undefined' && !ShadowRoot.prototype.getSelection) {
        ShadowRoot.prototype.getSelection = function() {
            return document.getSelection();
        };
    }*/
    
    window.MonacoEnvironment = {
        getWorker(_workerId, label) {
            switch (label) {
                case 'editorWorkerService':
                case 'TextEditorWorker':
                    return new EditorWorker();
                case 'TextMateWorker':
                    return new TextMateWorker();
                default:
                    throw new Error(`Unknown worker ${label}`);
            }
        }
    };

    // 1. Initialize all required services at once
    await initialize({
        ...getBaseServiceOverride(),
        ...getEditorServiceOverride(),
        ...getThemeServiceOverride(),
        ...getTextmateServiceOverride(),
        ...getLanguagesServiceOverride(),
        ...getModelServiceOverride(),
        ...getConfigurationServiceOverride(),
        //...getQuickAccessServiceOverride(),
        ...getStorageServiceOverride(),
        ...getViewsServiceOverride(),
        ...getServiceOverride(),
        //...getSearchServiceOverride(),
        //...getExtensionsServiceOverride(),
        //...getNotificationsServiceOverride(),
        //...getDialogsServiceOverride(),
        ...getFilesServiceOverride(),
        /*workspace: {
            workspaceUri: vscode.Uri.parse('browserfs:/'),
            trusted: true
        }*/
    });

    await whenReady();

    // 2. Render the workbench (required for themes and configurations to work)
    await updateUserConfiguration(`{
        "workbench.colorTheme": "Default Dark Modern",
        "workbench.iconTheme": "vs-seti",
        "editor.hover.enabled": true,
        "editor.hover.delay": 150,
        "editor.hover.sticky": true,
        "explorer.compactFolders": false
    }`);

    // Attach parts - order matters!
    const activityBar = document.getElementById('activity-bar');
    const fileSidebar = document.getElementById('file-sidebar');
    
    // Make sure elements exist and are properly structured
    if (activityBar) {
        attachPart(Parts.ACTIVITYBAR_PART, activityBar);
    }
    
    if (fileSidebar) {
        attachPart(Parts.SIDEBAR_PART, fileSidebar);
    }

    const uri = monaco.Uri.parse('file:///untitled.lua');
    const modelRef = await createModelReference(uri, '--test');

    // 3. Create the editor instance
    editor = await createConfiguredEditor(document.getElementById('editor'), {
        model: modelRef.object.textEditorModel,
        theme: 'Default Dark Modern',
        fontSize: 14,
        tabSize: 4,
        minimap: { enabled: true },
        automaticLayout: true,
        padding: { bottom: 20 },
        scrollBeyondLastLine: false,
        lineNumbers: 'on',
        renderWhitespace: 'selection',
        cursorBlinking: 'smooth',
        smoothScrolling: true,
        fontFamily: "'Monaco', 'Menlo', 'Ubuntu Mono', monospace",
        hover: {
            enabled: true,
            delay: 150,
            sticky: true
        },
        quickSuggestionsDelay: 100,
        suggestOnTriggerCharacters: true,
        acceptSuggestionOnCommitCharacter: true,
        wordBasedSuggestions: 'off',
        'semanticHighlighting.enabled': false
    });

    monaco.editor.setModelLanguage(modelRef.object.textEditorModel, 'lua');

    monaco.editor.tokenize(editor.getValue(), 'lua');
    
    // Add keyboard shortcuts
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, () => {
        saveCurrentFile();
    });
    
    // Wait a bit to ensure Monaco is fully ready, then register custom API
    await whenReady();
    registerLuaAPI(monaco);
    addStatusLine('✓ Custom Lua API completions registered', 'success');
}

// Custom Message Reader for C++ bridge
class CppBridgeReader {
    constructor() {
        this.callback = null;
        this.errorCallback = null;
        this.closeCallback = null;
        this.partialMessageCallback = null;
        this.messageCount = 0;
        this.totalTime = 0;
        
        // Set up receiver from C++
        window.receiveLSPMessage = (messageStr) => {
            const start = performance.now();
            
            if (this.callback) {
                try {
                    const message = JSON.parse(messageStr);
                    
                    this.callback(message);
                    
                    const callbackEnd = performance.now();
                    
                    this.messageCount++;
                    const totalTime = callbackEnd - start;
                    this.totalTime += totalTime;
                    
                    // Log slow messages
                    if (totalTime > 10) {
                        addStatusLine(`SLOW LSP receive (${totalTime.toFixed(2)}ms): ${message.method || 'response'}`, 'warning');
                    }
                    
                    // Log stats every 50 messages
                    if (this.messageCount % 50 === 0) {
                        addStatusLine(`LSP Recv: ${this.messageCount} msgs, avg ${(this.totalTime/this.messageCount).toFixed(2)}ms`, 'info');
                    }
                } catch (e) {
                    addStatusLine('LSP parse error: ' + e.message, 'error');
                    if (this.errorCallback) {
                        this.errorCallback(e);
                    }
                }
            }
            
            const end = performance.now();
            if (end - start > 16) {
                addStatusLine(`Frame drop in receiveLSPMessage: ${(end - start).toFixed(2)}ms`, 'warning');
            }
        };
    }
    
    listen(callback) {
        this.callback = callback;
    }
    
    onError(callback) {
        this.errorCallback = callback;
        return { dispose: () => { this.errorCallback = null; } };
    }
    
    onClose(callback) {
        this.closeCallback = callback;
        return { dispose: () => { this.closeCallback = null; } };
    }
    
    onPartialMessage(callback) {
        this.partialMessageCallback = callback;
        return { dispose: () => { this.partialMessageCallback = null; } };
    }
    
    dispose() {
        this.callback = null;
        this.errorCallback = null;
        this.closeCallback = null;
        this.partialMessageCallback = null;
        window.receiveLSPMessage = null;
    }
}

// Custom Message Writer for C++ bridge
class CppBridgeWriter {
    constructor() {
        this.errorCallback = null;
        this.closeCallback = null;
        this.writeCount = 0;
        this.totalWriteTime = 0;
    }
    
    async write(msg) {
        const start = performance.now();
        
        try {
            const msgStr = JSON.stringify(msg);
            
            // THIS IS WHERE WE TEST IF callCpp IS BLOCKING
            setTimeout(()=>window.callCpp('lsp_message', msgStr), 0);
            
            const end = performance.now();
            const writeTime = end - start;
            
            this.writeCount++;
            this.totalWriteTime += writeTime;
            
            // Check if callCpp is blocking the UI thread
            if (writeTime > 5) {
                addStatusLine(`⚠️ callCpp BLOCKING: ${writeTime.toFixed(2)}ms for ${msg.method || 'message'}`, 'warning');
            }
            
            // Log stats every 50 writes
            if (this.writeCount % 50 === 0) {
                addStatusLine(`LSP Send: ${this.writeCount} msgs, avg ${(this.totalWriteTime/this.writeCount).toFixed(2)}ms`, 'info');
            }
            
            return Promise.resolve();
        } catch (e) {
            addStatusLine('LSP write error: ' + e.message, 'error');
            if (this.errorCallback) {
                this.errorCallback([e, msg, 0]);
            }
            return Promise.reject(e);
        }
    }
    
    onError(callback) {
        this.errorCallback = callback;
        return { dispose: () => { this.errorCallback = null; } };
    }
    
    onClose(callback) {
        this.closeCallback = callback;
        return { dispose: () => { this.closeCallback = null; } };
    }
    
    end() {
        if (this.closeCallback) {
            this.closeCallback();
        }
    }
    
    dispose() {
        this.errorCallback = null;
        this.closeCallback = null;
    }
}

async function initializeLSPOriginal() {
    try {
        addStatusLine('Initializing LSP client...', 'info');
        const reader = new CppBridgeReader();
        const writer = new CppBridgeWriter();
        languageClient = new MonacoLanguageClient({
            name: 'Lua Language Client', clientOptions: {
                documentSelector: [{ language: 'lua' }], synchronize: { fileEvents: [] }, textDocumentSync: {
                    openClose: true, change: 2, // Incremental sync
                    save: { includeText: false }
                }, 
                initializationOptions: {
                    semanticTokens: { enable: false }, 
                    diagnostics: { workspaceDelay: 1000, workspaceRate: 100 } 
                }, 
                errorHandler: { 
                    error: () => ({ action: ErrorAction.Continue }), 
                    closed: () => ({ action: CloseAction.DoNotRestart }) 
                }, 
                middleware: { 
                    didChange: (event, next) => { 
                        clearTimeout(window.lspDidChangeTimeout); 
                        window.lspDidChangeTimeout = setTimeout(() => { next(event); }, 300); 
                    }
                }
            },
            messageTransports: { reader: reader, writer: writer }
        }); 
        await languageClient.start(); 
        addStatusLine('✓ Lua Language Server connected', 'success');
    } catch (err) { 
        addStatusLine('✗ LSP initialization failed: ' + err.message, 'error');
    }
}

function openDocumentForLSP() {
    if (!languageClient) return;

    const model = editor.getModel();
    const uri = model.uri.toString();

    languageClient.sendNotification('textDocument/didOpen', {
        textDocument: {
            uri,
            languageId: 'lua',
            version: 1,
            text: model.getValue()
        }
    });
}

// Initialize LSP client
async function initializeLSP() {
    try {
        addStatusLine('Initializing LSP client...', 'info');
        
        const reader = new CppBridgeReader();
        const writer = new CppBridgeWriter();
        
        languageClient = new MonacoLanguageClient({
            name: 'Lua Language Client',
            clientOptions: {
                documentSelector: [{ language: 'lua' }],
            
                // 🔴 DO NOT let LSP react to editor events automatically
                synchronize: {
                    fileEvents: []
                },
            
                // 🔴 Disable editor-driven features
                hoverProvider: false,
                completionProvider: false,
                signatureHelpProvider: false,
                documentHighlightProvider: false,
                documentSymbolProvider: false,
            
                textDocumentSync: {
                    openClose: true,
                    change: 2 // incremental
                },
            
                initializationOptions: {
                    Lua: {
                        workspace: {
                            checkThirdParty: false,
                            library: []
                        },
                        diagnostics: {
                            enable: false
                        },
                        semantic: {
                            enable: false
                        }
                    }
                },
            
                errorHandler: {
                    error: () => ({ action: ErrorAction.Continue }),
                    closed: () => ({ action: CloseAction.DoNotRestart })
                }
            },
            messageTransports: {
                reader: reader,
                writer: writer
            }
        });
        
        await languageClient.start();
        openDocumentForLSP();

        let pendingChange = false;

        editor.onDidChangeModelContent((e) => {
            if (!languageClient) return;

            pendingChange = true;

            clearTimeout(window.lspChangeTimer);
            window.lspChangeTimer = setTimeout(() => {
                if (!pendingChange) return;
                pendingChange = false;

                const model = editor.getModel();
                languageClient.sendNotification('textDocument/didChange', {
                    textDocument: {
                        uri: model.uri.toString(),
                        version: ++documentVersion
                    },
                    contentChanges: e.changes.map(c => ({
                        range: c.range,
                        rangeLength: c.rangeLength,
                        text: c.text
                    }))
                });
            }, 250);
        });
        addStatusLine('✓ Lua Language Server connected', 'success');
        
    } catch (err) {
        addStatusLine('✗ LSP initialization failed: ' + err.message, 'error');
    }
}

function applyDiagnostics(params) {
    const model = editor.getModel();
    if (!model) return;

    // Only apply to the active document
    if (params.uri !== model.uri.toString()) return;

    const markers = params.diagnostics.map(d => ({
        severity: d.severity === 1
            ? monaco.MarkerSeverity.Error
            : d.severity === 2
            ? monaco.MarkerSeverity.Warning
            : monaco.MarkerSeverity.Info,

        message: d.message,

        startLineNumber: d.range.start.line + 1,
        startColumn: d.range.start.character + 1,
        endLineNumber: d.range.end.line + 1,
        endColumn: d.range.end.character + 1
    }));

    monaco.editor.setModelMarkers(model, 'lua', markers);
}

let lspWorker = null;

function initLSPWorker() {
    addStatusLine('Starting LSP worker...', 'info');


    lspWorker = new Worker(
        new URL("./lsp_worker.js", import.meta.url),
        { type: 'module' }
    );

    // Messages FROM worker → C++ (LSP server)
    lspWorker.onmessage = (e) => {
        addStatusLine('LSP Worker message of type: ' + e.data.type, 'info');
        if (e.data.type === 'lsp-out') {
            window.callCpp(
                'lsp_message',
                JSON.stringify(e.data.payload)
            );
        }
    };

    // Messages FROM C++ → worker
    window.receiveLSPMessage = (msgStr) => {
        addStatusLine('Forwarding LSP message to worker', 'info');
        lspWorker.postMessage({
            type: 'lsp-in',
            payload: JSON.parse(msgStr)
        });
    };

    addStatusLine('✓ LSP worker initialized', 'success');
}

// Setup editor (NO LSP - using custom completions instead!)
const editorSetupID = setInterval(() => {    
    setupEditor().then(() => {
        addStatusLine('Editor setup complete with custom API completions', 'success');
        // LSP initialization removed - using Monaco's built-in completions!
        //initializeLSPOriginal();
    }).catch(err => addStatusLine("Editor setup error: " + err.message + "; " + err.stack, 'error'));
    
    clearInterval(editorSetupID);
}, 50);

// UI Elements
const openBtn = document.getElementById('openBtn');
const projectBtn = document.getElementById("projectBtn");
const saveBtn = document.getElementById('saveBtn');
const runScriptBtn = document.getElementById('runScriptBtn');
const runProjectBtn = document.getElementById('runProjectBtn');
const fileInput = document.getElementById('fileInput');
const fileName = document.getElementById('fileName');
const clearBtn = document.getElementById('clearBtn');
const statusOutput = document.getElementById('statusOutput');
const statusIndicator = document.getElementById('statusIndicator');
const statusText = document.getElementById('statusText');

// Sidebar tabs
const sidebarTabs = document.querySelectorAll('.sidebar-tab');
const statusPanel = document.getElementById('statusPanel');
const settingsPanel = document.getElementById('settingsPanel');

sidebarTabs.forEach(tab => {
    tab.addEventListener('click', () => {
        sidebarTabs.forEach(t => t.classList.remove('active'));
        tab.classList.add('active');
        
        const tabName = tab.dataset.tab;
        statusPanel.classList.toggle('active', tabName === 'status');
        settingsPanel.classList.toggle('active', tabName === 'settings');
    });
});

// Settings
const themeSelect = document.getElementById('themeSelect');
const fontSizeInput = document.getElementById('fontSizeInput');
const tabSizeInput = document.getElementById('tabSizeInput');
const resetOnCodeChangeCheck = document.getElementById('resetOnCodeChangeCheck');
const resetPermissionsBtn = document.getElementById('resetPermissionsBtn');
const projectDirDisplay = document.getElementById('projectDirDisplay');
const projectPermissionList = document.getElementById('projectPermissionList');
const projectSettingsTab = document.getElementById('projectSettingsTab');
const editorSettingsTab = document.getElementById('editorSettingsTab');
const projectSettingsPage = document.getElementById('projectSettingsPage');
const editorSettingsPage = document.getElementById('editorSettingsPage');
const approvedMouseRateInput = document.getElementById('approvedMouseRateInput');
const approvedKeyboardRateInput = document.getElementById('approvedKeyboardRateInput');
const approvedKeyboardTextRateInput = document.getElementById('approvedKeyboardTextRateInput');
const globalMouseRateInput = document.getElementById('globalMouseRateInput');
const globalKeyboardRateInput = document.getElementById('globalKeyboardRateInput');
const globalKeyboardTextRateInput = document.getElementById('globalKeyboardTextRateInput');
const cpuThrottlePercentInput = document.getElementById('cpuThrottlePercentInput');
const cpuKillPercentInput = document.getElementById('cpuKillPercentInput');
const residentRamLimitMbInput = document.getElementById('residentRamLimitMbInput');
const virtualRamLimitEnabledCheck = document.getElementById('virtualRamLimitEnabledCheck');
const virtualRamLimitMbInput = document.getElementById('virtualRamLimitMbInput');
const mouseRateMeta = document.getElementById('mouseRateMeta');
const keyboardRateMeta = document.getElementById('keyboardRateMeta');
const keyboardTextRateMeta = document.getElementById('keyboardTextRateMeta');
const lineNumbersCheck = document.getElementById('lineNumbersCheck');
const miniMapCheck = document.getElementById('miniMapCheck');

resetOnCodeChangeCheck.disabled = true;
resetPermissionsBtn.disabled = true;
approvedMouseRateInput.disabled = true;
approvedKeyboardRateInput.disabled = true;
approvedKeyboardTextRateInput.disabled = true;
renderProjectPermissionSettings(null);

function setSettingsSubTab(page) {
    const projectActive = page === 'project';
    projectSettingsTab.classList.toggle('active', projectActive);
    editorSettingsTab.classList.toggle('active', !projectActive);
    projectSettingsPage.classList.toggle('active', projectActive);
    editorSettingsPage.classList.toggle('active', !projectActive);
}

projectSettingsTab.addEventListener('click', () => setSettingsSubTab('project'));
editorSettingsTab.addEventListener('click', () => setSettingsSubTab('editor'));

themeSelect.addEventListener('change', applySettings);
fontSizeInput.addEventListener('change', applySettings);
tabSizeInput.addEventListener('change', applySettings);
lineNumbersCheck.addEventListener('change', applySettings);
miniMapCheck.addEventListener('change', applySettings);
resetOnCodeChangeCheck.addEventListener('change', () => {
    if (!currentProjectPath) {
        addStatusLine('Load a project to change permission reset policy.', 'warning');
        resetOnCodeChangeCheck.checked = true;
        return;
    }
    window.setProjectResetOnChange(resetOnCodeChangeCheck.checked);
});
resetPermissionsBtn.addEventListener('click', () => {
    if (!currentProjectPath) {
        addStatusLine('Load a project to reset saved permissions.', 'warning');
        return;
    }
    window.callCpp('reset_project_permissions', '');
});

function updateRateMeta(el, requested, effective, unit) {
    const reqText = (typeof requested === 'number') ? `${requested} ${unit}` : `not requested (uses approved max)`;
    const effText = (typeof effective === 'number') ? `${effective} ${unit}` : '--';
    el.textContent = `Requested: ${reqText} | Effective next run: ${effText}`;
}

function getSanitizedRate(value, fallback) {
    const n = Number(value);
    if (!Number.isFinite(n) || n <= 0) return fallback;
    return n;
}

function saveApprovedRatesFromInputs() {
    if (!currentProjectPath || !latestProjectPermissionSettings) {
        addStatusLine('No project loaded. Cannot update rate limits.', 'warning');
        return;
    }
    const approved = latestProjectPermissionSettings.approved_rates || {};
    const payload = {
        approved_rates: {
            mouse_events_per_sec: getSanitizedRate(approvedMouseRateInput.value, approved.mouse_events_per_sec),
            keyboard_events_per_sec: getSanitizedRate(approvedKeyboardRateInput.value, approved.keyboard_events_per_sec),
            keyboard_text_chars_per_sec: getSanitizedRate(approvedKeyboardTextRateInput.value, approved.keyboard_text_chars_per_sec)
        }
    };
    window.callCpp('set_project_permission_settings', JSON.stringify(payload));
}

approvedMouseRateInput.addEventListener('change', saveApprovedRatesFromInputs);
approvedKeyboardRateInput.addEventListener('change', saveApprovedRatesFromInputs);
approvedKeyboardTextRateInput.addEventListener('change', saveApprovedRatesFromInputs);

function saveGlobalRatesFromInputs() {
    const current = (latestGlobalRateSettings && latestGlobalRateSettings.max_rates) ? latestGlobalRateSettings.max_rates : {};
    const payload = {
        max_rates: {
            mouse_events_per_sec: getSanitizedRate(globalMouseRateInput.value, current.mouse_events_per_sec),
            keyboard_events_per_sec: getSanitizedRate(globalKeyboardRateInput.value, current.keyboard_events_per_sec),
            keyboard_text_chars_per_sec: getSanitizedRate(globalKeyboardTextRateInput.value, current.keyboard_text_chars_per_sec)
        }
    };
    window.callCpp('set_global_rate_settings', JSON.stringify(payload));
}

function renderGlobalRateSettings(settings) {
    latestGlobalRateSettings = settings || null;
    const rates = settings && settings.max_rates && typeof settings.max_rates === 'object' ? settings.max_rates : {};
    globalMouseRateInput.value = rates.mouse_events_per_sec ?? '';
    globalKeyboardRateInput.value = rates.keyboard_events_per_sec ?? '';
    globalKeyboardTextRateInput.value = rates.keyboard_text_chars_per_sec ?? '';
}

function saveResourceLimitsFromInputs() {
    const current = latestResourceLimits || {};
    const payload = {
        cpu_throttle_percent: getSanitizedRate(cpuThrottlePercentInput.value, current.cpu_throttle_percent),
        cpu_kill_percent: getSanitizedRate(cpuKillPercentInput.value, current.cpu_kill_percent),
        resident_ram_limit_mb: getSanitizedRate(residentRamLimitMbInput.value, current.resident_ram_limit_mb),
        virtual_ram_limit_enabled: !!virtualRamLimitEnabledCheck.checked,
        virtual_ram_limit_mb: getSanitizedRate(virtualRamLimitMbInput.value, current.virtual_ram_limit_mb)
    };
    window.callCpp('set_resource_limits', JSON.stringify(payload));
}

function renderResourceLimits(settings) {
    latestResourceLimits = settings || null;
    cpuThrottlePercentInput.value = settings?.cpu_throttle_percent ?? '';
    cpuKillPercentInput.value = settings?.cpu_kill_percent ?? '';
    residentRamLimitMbInput.value = settings?.resident_ram_limit_mb ?? '';
    virtualRamLimitMbInput.value = settings?.virtual_ram_limit_mb ?? '';
    virtualRamLimitEnabledCheck.checked = !!settings?.virtual_ram_limit_enabled;
    virtualRamLimitMbInput.disabled = !virtualRamLimitEnabledCheck.checked;
}

globalMouseRateInput.addEventListener('change', saveGlobalRatesFromInputs);
globalKeyboardRateInput.addEventListener('change', saveGlobalRatesFromInputs);
globalKeyboardTextRateInput.addEventListener('change', saveGlobalRatesFromInputs);
cpuThrottlePercentInput.addEventListener('change', saveResourceLimitsFromInputs);
cpuKillPercentInput.addEventListener('change', saveResourceLimitsFromInputs);
residentRamLimitMbInput.addEventListener('change', saveResourceLimitsFromInputs);
virtualRamLimitMbInput.addEventListener('change', saveResourceLimitsFromInputs);
virtualRamLimitEnabledCheck.addEventListener('change', saveResourceLimitsFromInputs);
var lastLine = "";

function renderProjectPermissionSettings(settings) {
    latestProjectPermissionSettings = settings || null;

    const hasProject = !!(settings && settings.project_dir);
    resetOnCodeChangeCheck.disabled = !hasProject;
    resetPermissionsBtn.disabled = !hasProject;
    approvedMouseRateInput.disabled = !hasProject;
    approvedKeyboardRateInput.disabled = !hasProject;
    approvedKeyboardTextRateInput.disabled = !hasProject;
    projectDirDisplay.textContent = hasProject ? settings.project_dir : 'No project loaded';

    if (!hasProject) {
        projectPermissionList.innerHTML = `<div class="settings-note">Load a project to view permissions.</div>`;
        approvedMouseRateInput.value = '';
        approvedKeyboardRateInput.value = '';
        approvedKeyboardTextRateInput.value = '';
        updateRateMeta(mouseRateMeta, null, null, 'events/sec');
        updateRateMeta(keyboardRateMeta, null, null, 'events/sec');
        updateRateMeta(keyboardTextRateMeta, null, null, 'chars/sec');
        return;
    }

    if (typeof settings.reset_on_code_change === 'boolean') {
        resetOnCodeChangeCheck.checked = settings.reset_on_code_change;
    }

    const approvedRates = settings.approved_rates && typeof settings.approved_rates === 'object' ? settings.approved_rates : {};
    const requestedRates = settings.requested_rates && typeof settings.requested_rates === 'object' ? settings.requested_rates : {};
    const effectiveRates = settings.effective_rates && typeof settings.effective_rates === 'object' ? settings.effective_rates : {};
    approvedMouseRateInput.value = approvedRates.mouse_events_per_sec ?? '';
    approvedKeyboardRateInput.value = approvedRates.keyboard_events_per_sec ?? '';
    approvedKeyboardTextRateInput.value = approvedRates.keyboard_text_chars_per_sec ?? '';
    updateRateMeta(mouseRateMeta, requestedRates.mouse_events_per_sec, effectiveRates.mouse_events_per_sec, 'events/sec');
    updateRateMeta(keyboardRateMeta, requestedRates.keyboard_events_per_sec, effectiveRates.keyboard_events_per_sec, 'events/sec');
    updateRateMeta(keyboardTextRateMeta, requestedRates.keyboard_text_chars_per_sec, effectiveRates.keyboard_text_chars_per_sec, 'chars/sec');

    const permissions = Array.isArray(settings.permissions) ? settings.permissions : [];
    const grants = settings.grants && typeof settings.grants === 'object' ? settings.grants : {};

    if (permissions.length === 0) {
        projectPermissionList.innerHTML = `<div class="settings-note">This manifest does not declare permissions.</div>`;
        return;
    }

    projectPermissionList.innerHTML = '';
    permissions.forEach((permission) => {
        const name = typeof permission.name === 'string' ? permission.name : '';
        const displayName = typeof permission.display_name === 'string' ? permission.display_name : name;
        if (!name) return;
        const optional = !!permission.optional;
        const granted = grants[name] === true;

        const row = document.createElement('div');
        row.className = 'permission-row';

        const meta = document.createElement('div');
        meta.className = 'permission-meta';
        meta.innerHTML = `
            <span class="permission-name">${escapeHtml(displayName)}</span>
            <span class="permission-badge ${optional ? 'optional' : 'required'}">${optional ? 'optional' : 'required'}</span>
        `;

        const toggle = document.createElement('input');
        toggle.type = 'checkbox';
        toggle.checked = granted;
        toggle.addEventListener('change', () => {
            if (!currentProjectPath) {
                addStatusLine('No project loaded. Cannot update permissions.', 'warning');
                toggle.checked = granted;
                return;
            }
            window.callCpp('set_project_permission_settings', JSON.stringify({
                grants: {
                    [name]: !!toggle.checked
                }
            }));
        });

        row.appendChild(meta);
        row.appendChild(toggle);
        projectPermissionList.appendChild(row);
    });
}

function applySettings() {
    if (!editor) return;
    
    editor.updateOptions({
        theme: themeSelect.value,
        fontSize: parseInt(fontSizeInput.value),
        tabSize: parseInt(tabSizeInput.value),
        lineNumbers: lineNumbersCheck.checked ? 'on' : 'off',
        minimap: { enabled: miniMapCheck.checked }
    });

    updateUserConfiguration(`{
        "workbench.colorTheme": "${themeSelect.value}"
    }`);
}

// File operations
openBtn.addEventListener('click', () => {
    fileInput.click();
});

fileInput.addEventListener('click', () => {
    fileInput.value = null; 
});

projectBtn.addEventListener('click', () => {
    window.callCpp('load_project', '')
})

fileInput.addEventListener('change', (e) => {
    const file = e.target.files[0];
    if (file) {
        const reader = new FileReader();
        reader.onload = (event) => {
            editor.setValue(event.target.result);
            currentFilePath = file.name;
            fileName.textContent = file.name;
            addStatusLine(`Loaded: ${file.name}`, 'info');
        };
        reader.readAsText(file);
    }
});

// Folder operations
//const openFolderBtn = document.getElementById('openFolderBtn');
//const newFileBtn = document.getElementById('newFileBtn');
//const refreshFilesBtn = document.getElementById('refreshFilesBtn');
const fileSidebar = document.getElementById('file-sidebar');

let fsProviderRegistered = true;

/*openFolderBtn.addEventListener('click', async () => {
    // Register file system provider on first use
    if (!fsProviderRegistered) {
        try {
            vscode.workspace.registerFileSystemProvider('browserfs', browserFS, {
                isCaseSensitive: true,
                isReadonly: false
            });
            fsProviderRegistered = true;
            console.log('File system provider registered');
        } catch (error) {
            console.error('Failed to register file system provider:', error);
            addStatusLine(`Error: ${error.message}`, 'error');
            return;
        }
    }
    
    const input = document.createElement('input');
    input.type = 'file';
    input.webkitdirectory = true;
    input.directory = true;
    input.multiple = true;
    
    input.addEventListener('change', async (e) => {
        const files = Array.from(e.target.files);
        if (files.length === 0) return;
        
        const firstFile = files[0];
        const folderName = firstFile.webkitRelativePath.split('/')[0];
        
        try {
            // Load files into provider
            const filePaths = await browserFS.loadFolder(folderName, files);
            await vscode.workspace.updateWorkspaceFolders(
                0,
                null,
                {
                  uri: vscode.Uri.parse(`browserfs:/${folderName}`),
                  name: folderName
                }
            );
            
            currentWorkspaceFolder = folderName;
            fileSidebar.classList.remove('collapsed');
            
            addStatusLine(`✓ Opened folder: ${folderName} (${filePaths.length} files)`, 'success');
            
            // Open first .lua file if exists
            const firstLuaFile = filePaths.find(p => p.endsWith('.lua'));
            if (firstLuaFile) {
                const fileUri = vscode.Uri.parse(`browserfs:/${folderName}/${firstLuaFile}`);
                const doc = await vscode.workspace.openTextDocument(fileUri);
                await vscode.window.showTextDocument(doc);
                
                // Update current file path
                currentFilePath = firstLuaFile;
                fileName.textContent = firstLuaFile;
            }
        } catch (error) {
            addStatusLine(`Error opening folder: ${error.message}`, 'error');
            console.error('Folder open error:', error);
        }
    });
    
    input.click();
});*/

/*newFileBtn.addEventListener('click', async () => {
    if (!currentWorkspaceFolder) {
        addStatusLine('Please open a folder first', 'warning');
        return;
    }
    
    const newFileName = await vscode.window.showInputBox({
        prompt: 'Enter file name',
        value: 'untitled.lua'
    });
    
    if (!newFileName) return;
    
    const uri = vscode.Uri.parse(`browserfs:/${currentWorkspaceFolder}/${newFileName}`);
    
    try {
        // Create and open the document
        const edit = new vscode.WorkspaceEdit();
        edit.createFile(uri, { ignoreIfExists: true });
        await vscode.workspace.applyEdit(edit);
        
        const doc = await vscode.workspace.openTextDocument(uri);
        await vscode.window.showTextDocument(doc);
        
        currentFilePath = newFileName;
        fileName.textContent = newFileName;
        
        addStatusLine(`Created: ${newFileName}`, 'success');
    } catch (error) {
        addStatusLine(`Failed to create file: ${error.message}`, 'error');
    }
});*/

/*refreshFilesBtn.addEventListener('click', () => {
    if (!currentWorkspaceFolder) {
        addStatusLine('No folder opened', 'warning');
        return;
    }
    
    addStatusLine('File tree refreshed', 'info');
});*/

saveBtn.addEventListener('click', saveCurrentFile);

async function saveCurrentFile() {
    const activeEditor = vscode.window.activeTextEditor;
    if (!activeEditor) {
        addStatusLine('No file to save', 'warning');
        return;
    }
    
    const document = activeEditor.document;
    const content = document.getText();
    const filePath = document.uri.path;
    
    // Extract relative path
    const pathParts = filePath.split('/').filter(p => p);
    const relativePath = pathParts.slice(1).join('/'); // Remove workspace folder name
    
    window.callCpp('save_file', JSON.stringify({
        path: filePath.substring(1), // Remove leading slash
        content: content
    }));
}

function saveFile() {
    saveCurrentFile();
}

// Script execution
runScriptBtn.addEventListener('click', runScript);
runProjectBtn.addEventListener('click', runProjectScript);

function runScript() {
    console.log("here");
    if (isRunning) return;
    
    const code = editor.getValue();
    console.log(code);
    if (!code.trim()) {
        addStatusLine('Editor is empty', 'warning');
        return;
    }

    setRunning(true);
    addStatusLine('Starting script execution...', 'info');

    /*editor.updateOptions({
        minimap: { enabled: false },
        lineNumbers: 'off',
        renderWhitespace: 'none',
        readOnly: true,
        hover: { enabled: false },
        quickSuggestions: false,
        wordBasedSuggestions: 'off'
    });*/
    
    const payload = {
        filename: (fileName.textContent && fileName.textContent !== 'No file loaded') ? fileName.textContent : 'quick_script.lua',
        code: code,
        project_dir: currentProjectPath || undefined
    };
    window.callCpp('run_quick_script', JSON.stringify(payload));
}

function setRunning(running) {
    isRunning = running;
    runScriptBtn.disabled = running;
    runProjectBtn.disabled = running;
    
    if (running) {
        statusIndicator.className = 'status-indicator running';
        statusText.textContent = 'Running...';
    } else {
        statusIndicator.className = 'status-indicator ready';
        statusText.textContent = 'Ready';
    }
}

// Console output
clearBtn.addEventListener('click', () => {
    statusOutput.innerHTML = '<div class="status-line info"><span class="timestamp">[Ready]</span><span>Console cleared</span></div>';
});

function addStatusLine(message, type = 'info') {
    const timestamp = new Date().toLocaleTimeString();
    const line = document.createElement('div');
    line.className = `status-line ${type}`;
    line.innerHTML = `<span>${escapeHtml(message)}</span>`;
    statusOutput.appendChild(line);
    statusOutput.scrollTop = statusOutput.scrollHeight;
}

window.addStatusLine = addStatusLine;

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

window.addConsoleLines = (lines) => {
    lines = lines.split('\n');
    lines[0] = lastLine + lines[0];
    lastLine = lines.pop();

    lines.forEach(line => {
        addStatusLine(line, 'info');
    });
}

// C++ response handler
window.cpp_response = function(name, result) {
    if (name === 'run_script' || name === 'run_quick_script') {
        setRunning(false);
        if (result === 'success') {
            addStatusLine('✓ Script started successfully', 'success');
        } else {
            addStatusLine(`✗ Error: ${result}`, 'error');
        }
        // Re-sync Settings permission toggles after a project run (e.g. re-prompt after code change wrote new grants).
        if (name === 'run_script' && currentProjectPath) {
            window.callCpp('get_project_permission_settings', '');
        }
    } else if (name === 'script_output') {
        addStatusLine(result, 'info');
    } else if (name === 'save_file') {
        if (result === 'success') {
            addStatusLine(`✓ Saved: ${currentFilePath}`, 'success');
        } else {
            addStatusLine(`✗ Save failed: ${result}`, 'error');
        }
    } else if (name == "load_project"){
        const pts = result.split(";");
        if (pts[0] == "success"){
            currentProjectPath = pts[1];
            addStatusLine(`✓ Project at '${pts[1]}' loaded successfully!`);
            window.callCpp('get_project_permission_settings', '');
        } else {
            currentProjectPath = null;
            resetOnCodeChangeCheck.disabled = true;
            resetPermissionsBtn.disabled = true;
            renderProjectPermissionSettings(null);
            addStatusLine(`✗ Loading project failed: ${pts[1]}`, 'error');
        }
    } else if (name === "get_project_permission_settings") {
        try {
            const settings = JSON.parse(result);
            renderProjectPermissionSettings(settings);
        } catch (e) {
            addStatusLine(`Failed to parse project permission settings: ${result}`, 'warning');
        }
    } else if (name === "set_project_permission_settings") {
        if (result === 'success') {
            addStatusLine('✓ Project security settings saved. Will apply next run.', 'success');
            window.callCpp('get_project_permission_settings', '');
        } else {
            addStatusLine(`✗ Failed to update project permission settings: ${result}`, 'error');
        }
    } else if (name === "get_global_rate_settings") {
        try {
            const settings = JSON.parse(result);
            renderGlobalRateSettings(settings);
            if (currentProjectPath) window.callCpp('get_project_permission_settings', '');
        } catch (e) {
            addStatusLine(`Failed to parse global rate settings: ${result}`, 'warning');
        }
    } else if (name === "set_global_rate_settings") {
        if (result === 'success') {
            addStatusLine('✓ Global rate limits saved.', 'success');
            window.callCpp('get_global_rate_settings', '');
        } else {
            addStatusLine(`✗ Failed to update global rate limits: ${result}`, 'error');
        }
    } else if (name === "get_resource_limits") {
        try {
            const settings = JSON.parse(result);
            renderResourceLimits(settings);
        } catch (e) {
            addStatusLine(`Failed to parse resource limits: ${result}`, 'warning');
        }
    } else if (name === "set_resource_limits") {
        if (String(result).startsWith('error:')) {
            addStatusLine(`✗ Failed to update resource limits: ${result}`, 'error');
        } else {
            addStatusLine('✓ Resource limits saved.', 'success');
            window.callCpp('get_resource_limits', '');
        }
    } else if (name === "reset_project_permissions") {
        if (result === 'success') {
            addStatusLine('✓ Saved project permissions reset. Will apply next run.', 'success');
            window.callCpp('get_project_permission_settings', '');
        } else {
            addStatusLine(`✗ Failed to reset saved project permissions: ${result}`, 'error');
        }
    }
}; 

window.setProjectResetOnChange = function(enabled) {
    if (!currentProjectPath) {
        addStatusLine('No project loaded. Cannot update reset policy.', 'warning');
        return;
    }
    window.callCpp('set_project_permission_settings', JSON.stringify({
        reset_on_code_change: !!enabled
    }));
};

function runProjectScript() {
    if (isRunning) return;
    setRunning(true);
    addStatusLine('Starting project manifest execution...', 'info');
    window.callCpp('run_script', '');
}

window.runProjectScript = runProjectScript;

let loadSettingsIntervalID = null;
document.addEventListener('DOMContentLoaded', () => {
    loadSettingsIntervalID = setInterval(()=>{
        window.callCpp('get_global_rate_settings', '');
        window.callCpp('get_resource_limits', '');
        clearInterval(loadSettingsIntervalID);
    }, 100);
});

// Console Sidebar resize functionality
const consoleSidebar = document.getElementById('sidebar');
const consoleResizeHandle = document.getElementById('resizeHandle');
let consoleIsResizing = false;

consoleResizeHandle.addEventListener('mousedown', (e) => {
    e.preventDefault();
    consoleIsResizing = true;
    consoleResizeHandle.classList.add('resizing');
    document.body.style.cursor = 'ew-resize';
    document.body.style.userSelect = 'none';
});

document.addEventListener('mousemove', (e) => {
    if (!consoleIsResizing) return;
    
    const newWidth = window.innerWidth - e.clientX;
    if (newWidth >= 200 && newWidth <= 600) {
        consoleSidebar.style.width = newWidth + 'px';
    }
});

document.addEventListener('mouseup', () => {
    if (consoleIsResizing) {
        consoleIsResizing = false;
        consoleResizeHandle.classList.remove('resizing');
        document.body.style.cursor = '';
        document.body.style.userSelect = '';
    }
});

console.log('Macro Script Editor loaded');

// File Sidebar Toggle
const fileResizeHandle = document.getElementById('fileResizeHandle');

fileResizeHandle.addEventListener('click', () => {
    fileSidebar.classList.toggle('collapsed');
});

// File Sidebar Resize functionality
let fileIsResizing = false;

fileResizeHandle.addEventListener('mousedown', (e) => {
    // Only resize if not clicking to toggle
    if (e.detail === 1 && !fileSidebar.classList.contains('collapsed')) {
        e.preventDefault();
        e.stopPropagation();
        fileIsResizing = true;
        fileResizeHandle.classList.add('resizing');
        document.body.style.cursor = 'ew-resize';
        document.body.style.userSelect = 'none';
    }
});

document.addEventListener('mousemove', (e) => {
    if (!fileIsResizing) return;
    
    const newWidth = e.clientX - fileSidebar.offsetLeft;
    if (newWidth >= 100 && newWidth <= 500) {
        fileSidebar.style.width = newWidth + 'px';
    }
});

document.addEventListener('mouseup', () => {
    if (fileIsResizing) {
        fileIsResizing = false;
        fileResizeHandle.classList.remove('resizing');
        document.body.style.cursor = '';
        document.body.style.userSelect = '';
    }
});