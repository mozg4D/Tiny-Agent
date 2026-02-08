/* 
 * TinyAgent C++ Backend (v19.1 - Extended Click + Hotkey Fix)
 * Compiles with: 
 * g++ tinyAgent.cpp -o tinyagent.exe -lws2_32 -lgdi32 -lgdiplus -luser32 -lkernel32 -lole32 -luuid -lshell32 -mwindows -static
 */

#define NOMINMAX 
#define _WIN32_WINNT 0x0600 
#define WIN32_LEAN_AND_MEAN 

#include <winsock2.h>
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <algorithm> 
#include <cmath> 

using namespace Gdiplus;

#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"kernel32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"shell32.lib")

#define PORT 8080
#define ID_HOTKEY_FULL 101 // Ctrl + F9
#define ID_HOTKEY_ACTIVE 102 // Ctrl + F10

// State
volatile int g_server_running = 1;

// Data Protection
CRITICAL_SECTION g_img_lock; 
char *g_last_screenshot_b64 = NULL;
size_t g_last_screenshot_len = 0;
int g_has_new_screenshot = 0;

DWORD g_main_thread_id = 0;
CLSID g_pngClsid;

// --- HTML RESOURCE ---
const char* g_index_html = R"HTML(<!DOCTYPE html>
<html lang="ru">

<head>
    <meta charset="UTF-8">
    <title>TinyAgent Pro v17</title>
    <style>
        :root {
            --bg-color: #1e1e1e;
            --sidebar-bg: #252526;
            --accent-color: #0e639c;
            --text-color: #cccccc;
            --border-color: #3e3e42;
            --input-bg: #3c3c3c;
        }

        * {
            box-sizing: border-box;
        }

        body,
        html {
            margin: 0;
            padding: 0;
            width: 100%;
            height: 100%;
            background: var(--bg-color);
            color: var(--text-color);
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            font-size: 13px;
            overflow: hidden;
        }

        #app {
            display: flex;
            width: 100vw;
            height: 100vh;
        }

        /* --- SIDEBAR --- */
        #sidebar {
            width: 400px;
            min-width: 350px;
            max-width: 600px;
            background: var(--sidebar-bg);
            border-right: 1px solid var(--border-color);
            display: flex;
            flex-direction: column;
            position: relative;
            z-index: 10;
        }

        .row {
            display: flex;
            gap: 8px;
            padding: 10px;
            align-items: center;
        }

        .col {
            display: flex;
            flex-direction: column;
            gap: 8px;
            padding: 10px;
        }

        .divider {
            height: 1px;
            background: var(--border-color);
            margin: 0;
        }

        input,
        select,
        textarea {
            background: var(--input-bg);
            border: 1px solid transparent;
            color: #fff;
            padding: 6px 8px;
            font-family: 'Segoe UI', sans-serif;
            font-size: 12px !important;
            outline: none;
            border-radius: 2px;
        }

        input:focus,
        select:focus,
        textarea:focus {
            border-color: var(--accent-color);
        }

        #api-key {
            flex-grow: 1;
            min-width: 0;
        }

        #model-select {
            width: auto;
            flex-shrink: 0;
            min-width: 160px;
        }

        #speed-select {
            width: 60px;
            text-align: right;
            cursor: pointer;
        }

        textarea#prompt-input {
            resize: none;
            overflow-y: hidden;
            min-height: 50px;
        }

        button {
            background: #333;
            color: #ccc;
            border: 1px solid transparent;
            padding: 6px 12px;
            cursor: pointer;
            transition: 0.1s;
            font-size: 12px !important;
            display: flex;
            align-items: center;
            justify-content: center;
        }

        button:hover {
            background: #444;
            color: white;
        }

        button.primary {
            background: var(--accent-color);
            color: white;
            font-weight: bold;
        }

        button.icon-btn {
            padding: 6px;
            width: 32px;
            height: 32px;
        }

        button.icon-btn svg {
            stroke: #ccc;
            width: 20px;
            height: 20px;
        }

        button.icon-btn:hover svg {
            stroke: #fff;
        }

        .btn-row {
            display: flex;
            gap: 8px;
        }

        .btn-row button {
            flex: 1;
        }

        .chk-group {
            display: flex;
            gap: 12px;
            color: #aaa;
        }

        .chk-item {
            display: flex;
            align-items: center;
            gap: 6px;
            cursor: pointer;
        }

        #resizer {
            width: 5px;
            height: 100%;
            background: transparent;
            position: absolute;
            right: -2px;
            top: 0;
            cursor: col-resize;
            z-index: 20;
        }

        #resizer:hover {
            background: var(--accent-color);
        }

        .hint-text {
            padding: 0 10px 10px 10px;
            color: #aaa;
            font-size: 12px;
            line-height: 1.4;
            text-align: center;
            white-space: nowrap;
        }

        /* --- CODE EDITOR --- */
        #editor-wrapper {
            flex-grow: 1;
            position: relative;
            background: #1e1e1e;
            overflow: hidden;
            border-top: 1px solid var(--border-color);
        }

        .editor-layer {
            position: absolute;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            padding: 10px;
            margin: 0;
            border: none;
            font-family: 'Consolas', 'Courier New', monospace;
            font-size: 13px !important;
            line-height: 1.5;
            white-space: pre;
            overflow: auto;
            tab-size: 4;
            box-sizing: border-box;
            white-space: pre-wrap;
            word-wrap: break-word;
        }

        pre#code-output {
            z-index: 1;
            pointer-events: none;
            color: #d4d4d4;
        }

        textarea#code-input {
            z-index: 2;
            color: transparent;
            background: transparent;
            caret-color: white;
            resize: none;
            outline: none;
        }

        .c-cmt {
            color: #6a9955;
        }

        .c-func {
            color: #dcdcaa;
        }

        .c-num {
            color: #b5cea8;
        }

        .c-str {
            color: #ce9178;
        }

        /* --- TABS --- */
        .tab-bar {
            display: flex;
            background: #2d2d2d;
            padding-top: 2px;
        }

        .tab-btn {
            background: transparent;
            border: none;
            color: #888;
            padding: 8px 16px;
            cursor: pointer;
            border-top: 2px solid transparent;
            font-weight: 600;
        }

        .tab-btn.active {
            color: white;
            border-top-color: var(--accent-color);
            background: #1e1e1e;
        }

        .tab-content {
            display: none;
            height: 100%;
        }

        .tab-content.active {
            display: block;
        }

        #ref-list {
            padding: 5px 0;
            overflow-y: auto;
            height: 100%;
        }

        .ref-item {
            padding: 4px 12px;
            cursor: pointer;
            font-family: 'Consolas', monospace;
            font-size: 13px;
            color: #d4d4d4;
        }

        .ref-item:hover {
            background: #2a2d2e;
        }

        /* --- VIEWPORT --- */
        #viewport {
            flex-grow: 1;
            background: #111;
            display: flex;
            flex-direction: column;
            overflow: hidden;
            outline: none;
        }

        #canvas-wrap {
            flex-grow: 1;
            position: relative;
            overflow: hidden;
            display: flex;
            align-items: center;
            justify-content: center;
        }

        canvas {
            display: block;
            image-rendering: pixelated;
            image-rendering: crisp-edges;
            cursor: crosshair;
        }

        .color-opt {
            width: 18px;
            height: 18px;
            cursor: pointer;
            border: 1px solid transparent;
            border-radius: 2px;
        }

        .color-opt.selected {
            border-color: white;
            transform: scale(1.1);
        }
    </style>
</head>

<body>

    <div id="app">
        <div id="sidebar">
            <!-- Header -->
            <div class="row">
                <input type="password" id="api-key" placeholder="API Key">
                <select id="model-select">
                    <option value="gemini-3-flash-preview">gemini-3-flash-preview</option>
                    <option value="gemini-3-pro-preview">gemini-3-pro-preview</option>
                </select>
            </div>

            <!-- Prompt & Buttons -->
            <div class="col" style="padding-top: 0;">
                <textarea id="prompt-input" placeholder="Describe task..."></textarea>
                <div class="btn-row">
                    <button class="primary" id="btn-generate">GENERATE PLAN</button>
                    <button id="btn-execute">EXECUTE</button>
                </div>
            </div>

            <!-- Shortcuts Hint -->
            <div class="hint-text">
                Screenshot: F9 (Full) &bull; F10 (Active) &bull; Stop: Mouse Shake
            </div>

            <div class="divider"></div>

            <!-- Tools -->
            <div class="row">
                <div class="color-opt selected" style="background:#ff3333" data-c="#ff3333"></div>
                <div class="color-opt" style="background:#33ff33" data-c="#33ff33"></div>
                <div class="color-opt" style="background:#3388ff" data-c="#3388ff"></div>
                <div style="flex-grow:1"></div>

                <button class="icon-btn" id="btn-undo" title="Undo (Ctrl+Z)">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
                        stroke-linejoin="round">
                        <path d="M3 7v6h6"></path>
                        <path d="M21 17a9 9 0 0 0-9-9 9 9 0 0 0-6 2.3L3 13"></path>
                    </svg>
                </button>
                <button class="icon-btn" id="btn-clear" title="Clear All">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"
                        stroke-linejoin="round">
                        <line x1="18" y1="6" x2="6" y2="18"></line>
                        <line x1="6" y1="6" x2="18" y2="18"></line>
                    </svg>
                </button>
            </div>

            <div class="row chk-group">
                <label class="chk-item"><input type="checkbox" id="chk-ruler" checked> Ruler</label>
                <label class="chk-item"><input type="checkbox" id="chk-grid"> Grid (AI)</label>
                <label class="chk-item"><input type="checkbox" id="chk-traj" checked> Traj</label>
            </div>

            <div class="divider"></div>

            <!-- Tabs -->
            <div class="tab-bar">
                <button class="tab-btn active" onclick="setTab('editor')">CODE</button>
                <button class="tab-btn" onclick="setTab('ref')">API</button>
                <div style="flex-grow:1"></div>
                <div style="display:flex; align-items: center; padding-right:10px; gap:8px;">
                    <span style="color:#888; font-size:12px;">Speed:</span>
                    <select id="speed-select">
                        <option value="0.1">0.1x</option>
                        <option value="0.5">0.5x</option>
                        <option value="1.0" selected>1x</option>
                        <option value="2.0">2x</option>
                        <option value="5.0">5x</option>
                        <option value="10.0">10x</option>
                    </select>
                </div>
            </div>

            <div id="editor-wrapper">
                <div id="tab-editor" class="tab-content active" style="position: relative; height: 100%;">
                    <pre id="code-output" class="editor-layer"></pre>
                    <textarea id="code-input" class="editor-layer" spellcheck="false"></textarea>
                </div>
                <div id="tab-ref" class="tab-content">
                    <div id="ref-list"></div>
                </div>
            </div>

            <div id="resizer"></div>
        </div>

        <!-- MAIN VIEW -->
        <div id="viewport" tabindex="0">
            <div id="canvas-wrap">
                <canvas id="canvas"></canvas>
            </div>
        </div>
    </div>

    <script>
        // --- APP STATE ---
        const app = {
            image: null,
            markers: [],

            scale: 1,
            panX: 0,
            panY: 0,
            rulerSize: 30,

            mode: 'none',
            lastMouse: { x: 0, y: 0 },
            zoomStart: { x: 0, y: 0 },

            markerColor: '#ff3333',
            showRuler: true,
            showGrid: false,
            showTraj: true,

            commands: [],
        };

        const els = {
            canvas: document.getElementById('canvas'),
            ctx: document.getElementById('canvas').getContext('2d'),
            editorIn: document.getElementById('code-input'),
            editorOut: document.getElementById('code-output'),
            prompt: document.getElementById('prompt-input'),
            sidebar: document.getElementById('sidebar'),
            resizer: document.getElementById('resizer'),
            refList: document.getElementById('ref-list'),
            viewport: document.getElementById('viewport'),
            executeBtn: document.getElementById('btn-execute'),
            speedSel: document.getElementById('speed-select')
        };

        // --- INIT ---
        function init() {
            const refs = [
                'click("left", 500, 500)', 'dblclick(500, 500)', 'drag("left", 500, 500, 600, 600)',
                'move(500, 500)', 'type("text")', 'keyDown("Control")', 'keyUp("Control")', 'wait(500)'
            ];

            refs.forEach(cmd => {
                const div = document.createElement('div');
                div.className = 'ref-item';
                div.innerHTML = highlightSyntax(cmd);
                div.onclick = () => addCmd(cmd);
                els.refList.appendChild(div);
            });

            els.editorIn.addEventListener('scroll', () => {
                els.editorOut.scrollTop = els.editorIn.scrollTop;
                els.editorOut.scrollLeft = els.editorIn.scrollLeft;
            });
            els.editorIn.addEventListener('input', () => { updateEditor(true); });

            els.prompt.addEventListener('input', () => {
                els.prompt.style.height = 'auto';
                els.prompt.style.height = els.prompt.scrollHeight + 'px';
            });

            window.addEventListener('resize', resizeCanvas);
            window.addEventListener('keydown', onKeyDown);
            window.addEventListener('paste', onPaste);

            els.canvas.addEventListener('mousedown', onMouseDown);
            window.addEventListener('mousemove', onMouseMove);
            window.addEventListener('mouseup', onMouseUp);
            els.canvas.addEventListener('wheel', onWheel, { passive: false });
            els.canvas.addEventListener('contextmenu', e => e.preventDefault());

            document.querySelectorAll('.color-opt').forEach(el => {
                el.addEventListener('click', e => {
                    document.querySelectorAll('.color-opt').forEach(o => o.classList.remove('selected'));
                    el.classList.add('selected');
                    app.markerColor = el.dataset.c;
                });
            });

            document.getElementById('btn-undo').onclick = manualUndo;
            document.getElementById('btn-clear').onclick = clearMarkers;

            document.getElementById('chk-ruler').onchange = (e) => { app.showRuler = e.target.checked; render(); };
            document.getElementById('chk-grid').onchange = (e) => { app.showGrid = e.target.checked; render(); };
            document.getElementById('chk-traj').onchange = (e) => { app.showTraj = e.target.checked; render(); };
            els.resizer.addEventListener('mousedown', initResize);

            els.executeBtn.onclick = runExecution;

            // Default code
            // els.editorIn.value = `// Commands will appear here...`;
            updateEditor(true);
            resizeCanvas();

            // Умное закрытие
            window.addEventListener('beforeunload', () => {
                navigator.sendBeacon('/shutdown');
            });

            pollEvents();

            // Load API Key
            const savedKey = localStorage.getItem('tinyagent_api_key');
            if (savedKey) document.getElementById('api-key').value = savedKey;

            document.getElementById('api-key').addEventListener('change', (e) => {
                localStorage.setItem('tinyagent_api_key', e.target.value);
            });

            document.getElementById('btn-generate').onclick = generatePlan;
        }

        // --- BACKEND COMMUNICATION ---
        async function pollEvents() {
            try {
                const res = await fetch('/events');
                if (res.ok) {
                    const data = await res.json();
                    if (data.type === 'screenshot' && data.image) {
                        const img = new Image();
                        img.onload = () => {
                            app.image = img;
                            app.markers = [];
                            fitImage();
                        };
                        img.src = data.image;
                    }
                }
            } catch (e) {
                console.log('Polling...', e);
            }
            setTimeout(pollEvents, 1000);
        }

        async function runExecution() {
            const code = els.editorIn.value;
            const speed = els.speedSel.value;

            els.executeBtn.innerText = 'Running...';
            els.executeBtn.disabled = true;

            try {
                const res = await fetch('/run', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ code: code, speed: parseFloat(speed) })
                });
                const txt = await res.text();
                if (txt.includes("ABORTED")) {
                    alert("Execution ABORTED by user (mouse movement detected)");
                }
            } catch (e) {
                alert('Execution failed: ' + e);
            }

            els.executeBtn.innerText = 'EXECUTE';
            els.executeBtn.disabled = false;
        }

        // --- AI GENERATION ---
        async function generatePlan() {
            const key = document.getElementById('api-key').value.trim();
            const prompt = els.prompt.value.trim();
            const model = document.getElementById('model-select').value;

            if (!key) { alert('Please enter Google API Key'); return; }
            if (!prompt) { alert('Please enter a prompt'); return; }

            const btn = document.getElementById('btn-generate');
            const oldText = btn.innerText;
            btn.innerText = 'THINKING...';
            btn.disabled = true;

            try {
                const sysPrompt = `You are an automation assistant.
COMMANDS:
- click("left"|"right"|"middle", x, y)
- dblclick(x, y) 
- drag("left"|"right"|"middle", x1, y1, x2, y2)
- move(x, y)
- scroll(delta)
- type("text")
- wait(ms)
- keyDown("key") / keyUp("key")

COORDINATES:
- 0,0 is Top-Left. 1000,1000 is Bottom-Right.
- ALL coordinates are NORMALIZED (0-1000).

OUTPUT FORMAT:
- You may explain your plan briefly.
- Provide the code in a code block like:
\`\`\`
click("left", 500, 500)
\`\`\`
`;

                const payload = {
                    contents: [{
                        role: "user",
                        parts: [
                            { text: sysPrompt + "\nTASK: " + prompt }
                        ]
                    }]
                };

                // Add image if available
                if (app.image) {
                    // Convert app.image to base64 (it was loaded from src which is data url)
                    // app.image.src is "data:image/png;base64,....."
                    const b64 = app.image.src.split(',')[1];
                    if (b64) {
                        payload.contents[0].parts.push({
                            inline_data: {
                                mime_type: "image/png",
                                data: b64
                            }
                        });
                    }
                }

                const url = `https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent?key=${key}`;

                const res = await fetch(url, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(payload)
                });

                if (!res.ok) throw new Error(res.statusText);

                const data = await res.json();
                const aiText = data.candidates?.[0]?.content?.parts?.[0]?.text || "";

                const extracted = extractCommands(aiText);
                if (extracted) insertAtCursor(extracted);
                else alert("AI returned no valid commands.\nRaw: " + aiText);

            } catch (e) {
                alert('AI Error: ' + e);
            }

            btn.innerText = oldText;
            btn.disabled = false;
        }

        function extractCommands(text) {
            // User wants FULL response, not just commands.
            // We just return the text as is.
            // If there's a code block, maybe we could prioritize it, but the user asked to see the "full response".
            // So we return everything.
            return text;
        }

        function insertAtCursor(text) {
            const field = els.editorIn;
            const startPos = field.selectionStart;
            const endPos = field.selectionEnd;
            const val = field.value;

            // Find insertion point
            let insertPos = endPos;

            // If cursor is not at end, check if we are on a non-empty line
            // We want to insert on the *next* line if current line has content
            const before = val.substring(0, startPos);
            const lastNewLine = before.lastIndexOf('\n');
            const currentLineContent = val.substring(lastNewLine + 1, startPos).trim();

            let prefix = "";
            let suffix = "";

            if (currentLineContent.length > 0) {
                // Content exists before cursor on this line -> Insert on NEW line
                // Move insertPos to end of this line? 
                // Better: Just insert "\n" before
                prefix = "\n";
            } else {
                // We are on an empty line or at start of line
                // No prefix needed
            }

            // Ensure newline at the end if not present in text
            if (!text.endsWith('\n')) suffix = "\n";

            const finalTxt = prefix + text + suffix;

            field.setRangeText(finalTxt, startPos, endPos, "end");

            // Trigger update
            updateEditor(true);
            els.editorOut.scrollTop = els.editorIn.scrollTop;
        }

        // --- EDITOR HIGHLIGHTER ---
        function highlightSyntax(text) {
            const escaped = text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
            const tokenRegex = /("[^"]*"|'[^']*')|(\/\/.*)|(\b\d+\b)|(\b[a-zA-Z_]\w*(?=\())/g;
            return escaped.replace(tokenRegex, (match, str, comment, num, func) => {
                if (str) return `<span class="c-str">${str}</span>`;
                if (comment) return `<span class="c-cmt">${comment}</span>`;
                if (num) return `<span class="c-num">${num}</span>`;
                if (func) return `<span class="c-func">${func}</span>`;
                return match;
            });
        }

        function updateEditor(parse = false) {
            const text = els.editorIn.value;
            els.editorOut.innerHTML = highlightSyntax(text) + (text.endsWith('\n') ? ' ' : '');
            if (parse) parseCommands(text);
            render();
        }

        function parseCommands(text) {
            app.commands = [];
            const lines = text.split('\n');
            const rDrag = /drag\s*\(\s*["'](\w+)["']\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\)/;

            // Supports: click(x,y), click("btn",x,y), dblclick(x,y), move(x,y)
            const rClickFull = /(click|dblclick|move)\s*\(\s*(?:["'](\w+)["']\s*,\s*)?(\d+)\s*,\s*(\d+)\)/;

            const rScroll = /scroll\s*\(\s*(-?\d+)\s*\)/;

            lines.forEach((line, i) => {
                if (line.trim().startsWith('//')) return;

                let m = line.match(rDrag);
                if (m) {
                    app.commands.push({ type: 'drag', btn: m[1], x: +m[2], y: +m[3], tx: +m[4], ty: +m[5], line: i });
                    return;
                }

                m = line.match(rScroll);
                if (m) {
                    app.commands.push({ type: 'scroll', delta: +m[1], line: i });
                    return;
                }

                m = line.match(rClickFull);
                if (m) {
                    // m[1]=type, m[2]=btn(optional), m[3]=x, m[4]=y
                    const type = m[1];
                    const btn = m[2] || "left";
                    const x = +m[3];
                    const y = +m[4];
                    app.commands.push({ type: type, btn: btn, x: x, y: y, line: i });
                }
            });
        }

        function updateCodeFromState() {
            const lines = els.editorIn.value.split('\n');
            app.commands.forEach(cmd => {
                if (cmd.type === 'drag') {
                    lines[cmd.line] = `drag("${cmd.btn}", ${Math.round(cmd.x)}, ${Math.round(cmd.y)}, ${Math.round(cmd.tx)}, ${Math.round(cmd.ty)})`;
                } else if (cmd.type === 'scroll') {
                    lines[cmd.line] = `scroll(${Math.round(cmd.delta)})`;
                } else if (cmd.type === 'click') {
                    // Determine if we need to specify button
                    if (cmd.btn === 'left') {
                        // Could use short form or long form. User asked for "click('left', ...)" extension, 
                        // but didn't say to FORCE long form. Let's stick to short form if left to be cleaner?
                        // Actually user said: "нужно расширить функцию: click("left", 500, 500)"
                        // If I enforce long form: `click("left", x, y)`
                        lines[cmd.line] = `click("${cmd.btn}", ${Math.round(cmd.x)}, ${Math.round(cmd.y)})`;
                    } else {
                        lines[cmd.line] = `click("${cmd.btn}", ${Math.round(cmd.x)}, ${Math.round(cmd.y)})`;
                    }
                } else if (['dblclick', 'move'].includes(cmd.type)) {
                    lines[cmd.line] = `${cmd.type}(${Math.round(cmd.x)}, ${Math.round(cmd.y)})`;
                }
            });
            els.editorIn.value = lines.join('\n');
            updateEditor(false);
        }

        // --- UNDO ---
        function onKeyDown(e) {
            if ((e.ctrlKey || e.metaKey) && e.code === 'KeyZ') {
                const tag = document.activeElement.tagName;
                if (tag === 'INPUT' || tag === 'TEXTAREA') return;
                e.preventDefault();
                manualUndo();
            }
        }

        function manualUndo() {
            if (app.image && app.markers.length > 0) {
                app.markers.pop();
                render();
            }
        }

        // --- RENDER ---
        function render() {
            const ctx = els.ctx;
            const cvs = els.canvas;

            ctx.fillStyle = '#111';
            ctx.fillRect(0, 0, cvs.width, cvs.height);

            if (!app.image) {
                ctx.fillStyle = '#444'; ctx.textAlign = 'center'; ctx.font = '14px Segoe UI';
                ctx.fillText('Waiting for Screenshot ( F9 / F10 ) ...', cvs.width / 2, cvs.height / 2);
                return;
            }

            const img = app.image;
            const pad = app.showRuler ? app.rulerSize : 0;

            const bufW = img.width + (pad * 2);
            const bufH = img.height + (pad * 2);
            const buffer = document.createElement('canvas');
            buffer.width = bufW;
            buffer.height = bufH;
            const bCtx = buffer.getContext('2d');

            bCtx.fillStyle = '#000';
            bCtx.fillRect(0, 0, bufW, bufH);

            bCtx.drawImage(img, pad, pad);

            app.markers.forEach(m => {
                bCtx.beginPath();
                bCtx.strokeStyle = m.color;
                bCtx.lineWidth = 2;
                bCtx.lineCap = 'round';
                bCtx.lineJoin = 'round';

                const toPx = (p) => ({
                    x: pad + (p.x * img.width) / 1000,
                    y: pad + (p.y * img.height) / 1000
                });

                if (m.points.length > 0) {
                    const pts = m.points.map(toPx);
                    bCtx.moveTo(pts[0].x, pts[0].y);
                    for (let i = 1; i < pts.length - 2; i++) {
                        const xc = (pts[i].x + pts[i + 1].x) / 2;
                        const yc = (pts[i].y + pts[i + 1].y) / 2;
                        bCtx.quadraticCurveTo(pts[i].x, pts[i].y, xc, yc);
                    }
                    if (pts.length > 2) {
                        bCtx.quadraticCurveTo(
                            pts[pts.length - 2].x, pts[pts.length - 2].y,
                            pts[pts.length - 1].x, pts[pts.length - 1].y
                        );
                    } else if (pts.length === 2) {
                        bCtx.lineTo(pts[1].x, pts[1].y);
                    }
                }
                bCtx.stroke();
            });

            if (app.showRuler) drawFullFrameRuler(bCtx, img.width, img.height, pad);
            if (app.showGrid) drawBakedGrid(bCtx, img.width, img.height, pad);

            ctx.save();
            ctx.translate(app.panX, app.panY);
            ctx.scale(app.scale, app.scale);
            ctx.drawImage(buffer, 0, 0);

            if (app.showTraj) drawTrajectory(ctx, img.width, img.height, pad);

            ctx.restore();
        }

        function drawFullFrameRuler(ctx, w, h, pad) {
            ctx.fillStyle = '#aaa';
            ctx.font = '10px monospace';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';

            const step = w < 400 ? 500 : (w < 800 ? 250 : 100);
            const stepY = h < 400 ? 500 : (h < 800 ? 250 : 100);

            ctx.fillStyle = '#000000';
            ctx.fillRect(pad, 0, w, pad);
            ctx.fillRect(pad, h + pad, w, pad);
            ctx.fillRect(0, pad, pad, h);
            ctx.fillRect(w + pad, pad, pad, h);

            ctx.fillRect(0, 0, pad, pad);
            ctx.fillRect(w + pad, 0, pad, pad);
            ctx.fillRect(0, h + pad, pad, pad);
            ctx.fillRect(w + pad, h + pad, pad, pad);

            ctx.fillStyle = '#aaa';

            for (let i = 0; i <= 1000; i += step) {
                const px = pad + (i / 1000 * w);
                ctx.fillRect(Math.floor(px), pad - 4, 1, 4);
                ctx.fillText(i, Math.floor(px), pad / 2);
                ctx.fillRect(Math.floor(px), h + pad, 1, 4);
                ctx.fillText(i, Math.floor(px), h + pad + pad / 2);
            }

            for (let i = stepY; i < 1000; i += stepY) {
                const py = pad + (i / 1000 * h);
                ctx.fillRect(pad - 4, Math.floor(py), 4, 1);
                ctx.fillText(i, pad / 2, Math.floor(py));
                ctx.fillRect(w + pad, Math.floor(py), 4, 1);
                ctx.fillText(i, w + pad + pad / 2, Math.floor(py));
            }
        }

        function drawBakedGrid(ctx, w, h, pad) {
            ctx.font = '10px monospace';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';

            let step = 100;
            if (w < 400 || h < 400) step = 500;
            else if (w < 800 || h < 800) step = 250;

            for (let nx = step; nx < 1000; nx += step) {
                for (let ny = step; ny < 1000; ny += step) {
                    const px = pad + (nx * w) / 1000;
                    const py = pad + (ny * h) / 1000;

                    ctx.fillStyle = 'rgba(255, 255, 0, 0.8)';
                    ctx.beginPath();
                    ctx.arc(px, py, 2, 0, Math.PI * 2);
                    ctx.fill();

                    if (nx > 0 && nx < 1000 && ny > 0 && ny < 1000) {
                        const txt = `${nx},${ny}`;
                        const tw = ctx.measureText(txt).width;
                        ctx.fillStyle = 'rgba(0,0,0,0.5)';
                        ctx.fillRect(px - tw / 2 - 2, py + 6, tw + 4, 12);
                        ctx.fillStyle = '#ffff00';
                        ctx.fillText(txt, px, py + 12);
                    }
                }
            }
        }

        function drawTrajectory(ctx, w, h, pad) {
            if (!app.commands.length) return;

            const toPx = (n) => pad + (n * w) / 1000;
            const toPy = (n) => pad + (n * h) / 1000;

            const drawDot = (x, y, c) => {
                ctx.beginPath(); ctx.fillStyle = c;
                ctx.arc(x, y, 4 / app.scale, 0, Math.PI * 2);
                ctx.fill();
            };

            let lastPos = null;

            app.commands.forEach((c, i) => {
                const tx = toPx(c.x), ty = toPy(c.y);
                const startPt = { x: tx, y: ty };
                let endPt = { x: tx, y: ty };

                if (c.type === 'drag') {
                    endPt = { x: toPx(c.tx), y: toPy(c.ty) };
                }

                if (lastPos) {
                    ctx.beginPath();
                    ctx.strokeStyle = '#666';
                    ctx.lineWidth = 1.5 / app.scale;
                    ctx.setLineDash([3 / app.scale, 3 / app.scale]);
                    ctx.moveTo(lastPos.x, lastPos.y);
                    ctx.lineTo(startPt.x, startPt.y);
                    ctx.stroke();
                }

                ctx.setLineDash([]);

                if (c.type === 'drag') {
                    ctx.beginPath();
                    ctx.strokeStyle = '#00ffff';
                    ctx.lineWidth = 2 / app.scale;
                    ctx.moveTo(startPt.x, startPt.y);
                    ctx.lineTo(endPt.x, endPt.y);
                    ctx.stroke();
                    drawDot(startPt.x, startPt.y, '#00ff00');
                    drawDot(endPt.x, endPt.y, '#ff0000');
                } else if (c.type === 'click') {
                    drawDot(startPt.x, startPt.y, '#00ff00');
                } else if (c.type === 'dblclick') {
                    drawDot(startPt.x, startPt.y, '#d400ff');
                } else if (c.type === 'move') {
                    ctx.beginPath(); ctx.fillStyle = '#ffff00';
                    ctx.arc(startPt.x, startPt.y, 3 / app.scale, 0, Math.PI * 2);
                    ctx.fill();
                }

                lastPos = endPt;
            });
        }

        function onMouseDown(e) {
            app.lastMouse = { x: e.clientX, y: e.clientY };
            els.viewport.focus();

            if (e.button === 0) {
                if (app.showTraj) {
                    const h = findDragHandle(e.clientX, e.clientY);
                    if (h) { app.mode = 'dragPoint'; app.activeDragHandle = h; return; }
                }
                if (app.image) {
                    app.mode = 'draw';
                    const p = getAiCoords(e.clientX, e.clientY);
                    app.markers.push({ color: app.markerColor, points: [p] });
                    render();
                }
            } else if (e.button === 1) app.mode = 'pan';
            else if (e.button === 2) { app.mode = 'zoom'; app.zoomStart = { x: e.clientX, y: e.clientY }; }
        }

        function onMouseMove(e) {
            const dx = e.clientX - app.lastMouse.x;
            const dy = e.clientY - app.lastMouse.y;
            app.lastMouse = { x: e.clientX, y: e.clientY };

            // --- Cursor Logic ---
            if (app.mode === 'none') {
                const h = findDragHandle(e.clientX, e.clientY);
                if (h) els.canvas.style.cursor = 'move';
                else if (app.image) els.canvas.style.cursor = 'crosshair';
                else els.canvas.style.cursor = 'default';
            }
            // --------------------

            if (app.mode === 'pan') { app.panX += dx; app.panY += dy; render(); }
            else if (app.mode === 'zoom') zoomAt((dx - dy) * 0.005, app.zoomStart.x, app.zoomStart.y);
            else if (app.mode === 'draw') {
                const m = app.markers;
                const pt = getAiCoords(e.clientX, e.clientY);
                const last = m[m.length - 1].points[m[m.length - 1].points.length - 1];
                if (!last || Math.abs(last.x - pt.x) > 0.5 || Math.abs(last.y - pt.y) > 0.5) {
                    m[m.length - 1].points.push(pt);
                    render();
                }
            } else if (app.mode === 'dragPoint' && app.activeDragHandle) {
                const c = app.activeDragHandle.cmd;
                const pt = getAiCoords(e.clientX, e.clientY, true);
                if (app.activeDragHandle.ptIdx === 0) { c.x = pt.x; c.y = pt.y; }
                else { c.tx = pt.x; c.ty = pt.y; }
                updateCodeFromState();
                render();
            }
        }

        function onMouseUp() { app.mode = 'none'; app.activeDragHandle = null; }
        function onWheel(e) { e.preventDefault(); zoomAt(-e.deltaY * 0.001, e.clientX, e.clientY); }

        function findDragHandle(mx, my) {
            if (!app.image) return null;
            const img = app.image;
            const pad = app.showRuler ? app.rulerSize : 0;
            const rect = els.canvas.getBoundingClientRect();

            const bufX = (mx - rect.left - app.panX) / app.scale;
            const bufY = (my - rect.top - app.panY) / app.scale;

            const toBuf = (n, dim) => pad + (n * dim / 1000);
            const rad = 8 / app.scale;

            for (let c of app.commands) {
                const px = toBuf(c.x, img.width);
                const py = toBuf(c.y, img.height);
                if (Math.hypot(px - bufX, py - bufY) < rad) return { cmd: c, ptIdx: 0 };

                if (c.type === 'drag') {
                    const px2 = toBuf(c.tx, img.width);
                    const py2 = toBuf(c.ty, img.height);
                    if (Math.hypot(px2 - bufX, py2 - bufY) < rad) return { cmd: c, ptIdx: 1 };
                }
            }
            return null;
        }

        function getAiCoords(cx, cy, clamp = false) {
            const rect = els.canvas.getBoundingClientRect();
            const img = app.image;
            const pad = app.showRuler ? app.rulerSize : 0;
            const bufX = (cx - rect.left - app.panX) / app.scale;
            const bufY = (cy - rect.top - app.panY) / app.scale;

            let nx = ((bufX - pad) / img.width) * 1000;
            let ny = ((bufY - pad) / img.height) * 1000;

            if (clamp) { nx = Math.max(0, Math.min(1000, nx)); ny = Math.max(0, Math.min(1000, ny)); }
            return { x: nx, y: ny };
        }

        function zoomAt(amount, cx, cy) {
            const oldScale = app.scale;
            app.scale = Math.max(0.1, Math.min(20, app.scale * (1 + amount)));
            const rect = els.canvas.getBoundingClientRect();
            const mx = cx - rect.left, my = cy - rect.top;
            app.panX -= (mx - app.panX) * (app.scale / oldScale - 1);
            app.panY -= (my - app.panY) * (app.scale / oldScale - 1);
            render();
        }

        function resizeCanvas() {
            const w = document.getElementById('canvas-wrap');
            els.canvas.width = w.clientWidth; els.canvas.height = w.clientHeight;
            render();
        }

        function onPaste(e) {
            const items = e.clipboardData.items;
            for (let item of items) {
                if (item.type.indexOf('image') !== -1) {
                    const blob = item.getAsFile();
                    const img = new Image();
                    img.onload = () => {
                        app.image = img;
                        app.markers = [];
                        fitImage();
                    };
                    img.src = URL.createObjectURL(blob);
                }
            }
        }

        function fitImage() {
            if (!app.image) { render(); return; }
            const img = app.image;
            const w = document.getElementById('canvas-wrap');
            const pad = app.rulerSize;
            const totalW = img.width + (pad * 2);
            const totalH = img.height + (pad * 2);
            const ratio = Math.min(w.clientWidth / totalW, w.clientHeight / totalH);
            app.scale = ratio * 0.95;
            app.panX = (w.clientWidth - totalW * app.scale) / 2;
            app.panY = (w.clientHeight - totalH * app.scale) / 2;
            render();
        }

        function clearMarkers() { app.markers = []; render(); }
        function initResize(e) { window.addEventListener('mousemove', doResize); window.addEventListener('mouseup', stopResize); }
        function doResize(e) { let w = e.clientX; if (w < 300) w = 300; if (w > 600) w = 600; els.sidebar.style.width = w + 'px'; resizeCanvas(); }
        function stopResize() { window.removeEventListener('mousemove', doResize); window.removeEventListener('mouseup', stopResize); }

        window.setTab = (t) => {
            document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.style.display = 'none');
            if (t === 'editor') {
                document.querySelectorAll('.tab-btn')[0].classList.add('active');
                document.getElementById('tab-editor').style.display = 'block';
            } else {
                document.querySelectorAll('.tab-btn')[1].classList.add('active');
                document.getElementById('tab-ref').style.display = 'block';
            }
        };
        window.addCmd = (txt) => {
            insertAtCursor(txt);
        };

        init();
    </script>
</body>

</html>)HTML";

// Context
int g_capture_mode = 0; 
HWND g_target_hwnd = NULL; 
RECT g_last_capture_rect = {0}; 

// --- HELPER: Kill previous instance ---
void KillOldInstance() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(PORT);
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        const char* msg = "POST /shutdown HTTP/1.1\r\nHost: localhost\r\n\r\n";
        send(s, msg, strlen(msg), 0);
        closesocket(s);
        Sleep(1000); 
    } else {
        closesocket(s);
    }
}

// --- HELPER: Network Send ---
int send_full(SOCKET s, const char *buf, size_t len) {
    size_t total = 0;
    size_t bytesleft = len;
    while(total < len) {
        int chunk = (bytesleft > 16384) ? 16384 : (int)bytesleft;
        int n = send(s, buf+total, chunk, 0);
        if (n == -1) { return -1; }
        total += n;
        bytesleft -= n;
    }
    return 0;
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT  num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if(size == 0) return -1;
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if(pImageCodecInfo == NULL) return -1;
    GetImageEncoders(num, size, pImageCodecInfo);
    for(UINT j = 0; j < num; ++j) {
        if(wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

char* base64_encode(const unsigned char *data, size_t input_length, size_t *output_length) {
    static const char encoding_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    *output_length = 4 * ((input_length + 2) / 3);
    char *encoded_data = (char*)malloc(*output_length + 1);
    if (encoded_data == NULL) return NULL;
    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }
    const int mod_table[] = {0, 2, 1};
    for (int i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[*output_length - 1 - i] = '=';
    encoded_data[*output_length] = '\0';
    return encoded_data;
}

void VisualFlash(int x, int y, int w, int h) {
    HDC hDC = GetDC(NULL);
    PatBlt(hDC, x, y, w, h, DSTINVERT);
    Sleep(50);
    PatBlt(hDC, x, y, w, h, DSTINVERT);
    ReleaseDC(NULL, hDC);
}

void CaptureScreen(int type) {
    printf("[Backend] Capture type: %d\n", type);
    
    int x, y, w, h;
    RECT rc = {0};
    g_capture_mode = type;
    // Always capture the foreground window HWND, even in full screen mode,
    // so we can restore focus to it later.
    g_target_hwnd = GetForegroundWindow();
    if(!g_target_hwnd) g_target_hwnd = GetDesktopWindow();

    if (type == 1) { // Full
        x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    } else { // Active Window (Smart Tracking)
        GetWindowRect(g_target_hwnd, &rc);
        x = rc.left; y = rc.top; w = rc.right - rc.left; h = rc.bottom - rc.top;
    }

    if (w <= 0 || h <= 0) return;

    MessageBeep(MB_OK);
    VisualFlash(x, y, w, h);

    g_last_capture_rect.left = x; g_last_capture_rect.top = y; 
    g_last_capture_rect.right = x+w; g_last_capture_rect.bottom = y+h;

    HDC hScreenDC = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, w, h);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    
    BitBlt(hMemoryDC, 0, 0, w, h, hScreenDC, x, y, SRCCOPY);

    IStream *pStream = NULL;
    CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    
    Bitmap *pBitmap = Bitmap::FromHBITMAP(hBitmap, NULL);
    pBitmap->Save(pStream, &g_pngClsid, NULL);
    delete pBitmap;

    HGLOBAL hGlobal = NULL;
    GetHGlobalFromStream(pStream, &hGlobal);
    void *pData = GlobalLock(hGlobal);
    size_t size = GlobalSize(hGlobal);
    
    size_t b64_len;
    char *b64 = base64_encode((unsigned char*)pData, size, &b64_len);
    
    EnterCriticalSection(&g_img_lock);
    if(g_last_screenshot_b64) free(g_last_screenshot_b64);
    g_last_screenshot_b64 = (char*)malloc(b64_len + 64);
    strcpy(g_last_screenshot_b64, "data:image/png;base64,");
    strcat(g_last_screenshot_b64, b64);
    g_last_screenshot_len = strlen(g_last_screenshot_b64);
    g_has_new_screenshot = 1;
    LeaveCriticalSection(&g_img_lock);
    
    printf("[Backend] Snapshot ready. Size: %lu bytes.\n", (unsigned long)g_last_screenshot_len);

    free(b64);
    GlobalUnlock(hGlobal);
    pStream->Release();
    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);
}

// --- LOGIC (SendInput) ---

WORD GetVkFromString(const char* key) {
    if (strcmp(key, "Control") == 0) return VK_CONTROL;
    if (strcmp(key, "Shift") == 0) return VK_SHIFT;
    if (strcmp(key, "Alt") == 0) return VK_MENU;
    if (strcmp(key, "Enter") == 0) return VK_RETURN;
    if (strcmp(key, "Esc") == 0) return VK_ESCAPE;
    if (strcmp(key, "Space") == 0) return VK_SPACE;
    if (strcmp(key, "Backspace") == 0) return VK_BACK;
    if (strcmp(key, "Tab") == 0) return VK_TAB;
    // Single characters
    if (strlen(key) == 1) return VkKeyScan(key[0]) & 0xFF;
    return 0;
}

void PerformKey(const char* keyStr, bool down) {
    WORD vk = GetVkFromString(keyStr);
    if (!vk) return;
    
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    if (!down) input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void SendKeyInput(WORD vk, BOOL up) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    if (up) input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void PerformType(const char *text) {
    if (!text) return;
    printf("[Backend] Typing (Unicode): %s\n", text);

    // Convert UTF-8 to WideChar (UTF-16)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wlen <= 0) return;
    
    wchar_t *wstr = (wchar_t*)malloc(wlen * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wstr, wlen);

    for(int i=0; i < wlen - 1; i++) { // -1 to skip null terminator
        wchar_t ch = wstr[i];
        
        INPUT inputs[2] = {};
        
        // Key Down
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = 0;
        inputs[0].ki.wScan = ch;
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
        
        // Key Up
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = 0;
        inputs[1].ki.wScan = ch;
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        
        SendInput(2, inputs, sizeof(INPUT));
        Sleep(20);
    }
    free(wstr);
}

void PerformScroll(int delta) {
    printf("[Backend] Scrolling: %d\n", delta);
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = (DWORD)delta;
    SendInput(1, &input, sizeof(INPUT));
}

void PerformClick(const char* btn, int x, int y, int dbl) {
    SetCursorPos(x, y);
    
    DWORD downParams = MOUSEEVENTF_LEFTDOWN;
    DWORD upParams = MOUSEEVENTF_LEFTUP;
    
    if (strcmp(btn, "right") == 0) {
        downParams = MOUSEEVENTF_RIGHTDOWN;
        upParams = MOUSEEVENTF_RIGHTUP;
    } else if (strcmp(btn, "middle") == 0) {
        downParams = MOUSEEVENTF_MIDDLEDOWN;
        upParams = MOUSEEVENTF_MIDDLEUP;
    }

    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = downParams;
    SendInput(1, &input, sizeof(INPUT));
    
    input.mi.dwFlags = upParams;
    SendInput(1, &input, sizeof(INPUT));

    if (dbl) { 
        Sleep(100); 
        input.mi.dwFlags = downParams;
        SendInput(1, &input, sizeof(INPUT));
        input.mi.dwFlags = upParams;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void PerformDrag(const char* btn, int x1, int y1, int x2, int y2) {
    SetCursorPos(x1, y1); 
    
    DWORD downParams = MOUSEEVENTF_LEFTDOWN;
    DWORD upParams = MOUSEEVENTF_LEFTUP;
    if (strcmp(btn, "right") == 0) { downParams = MOUSEEVENTF_RIGHTDOWN; upParams = MOUSEEVENTF_RIGHTUP; }
    else if (strcmp(btn, "middle") == 0) { downParams = MOUSEEVENTF_MIDDLEDOWN; upParams = MOUSEEVENTF_MIDDLEUP; }

    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = downParams;
    SendInput(1, &input, sizeof(INPUT));
    
    Sleep(100);
    for(int i=0; i<=10; i++) { 
        SetCursorPos(x1 + (x2-x1)*i/10, y1 + (y2-y1)*i/10); 
        Sleep(10); 
    }
    
    input.mi.dwFlags = upParams;
    SendInput(1, &input, sizeof(INPUT));
}

void GetTargetCoords(int normX, int normY, int *outX, int *outY) {
    int w, h, offsetX, offsetY;
    // Если режим окна и оно валидно - используем его
    if (g_capture_mode == 2 && g_target_hwnd && IsWindow(g_target_hwnd)) {
        RECT rc; GetWindowRect(g_target_hwnd, &rc);
        offsetX = rc.left; offsetY = rc.top;
        w = rc.right - rc.left; h = rc.bottom - rc.top;
    } else {
        // Иначе координаты последнего снимка (полный экран)
        offsetX = g_last_capture_rect.left; offsetY = g_last_capture_rect.top;
        w = g_last_capture_rect.right - g_last_capture_rect.left;
        h = g_last_capture_rect.bottom - g_last_capture_rect.top;
    }
    if (w <= 0) w = GetSystemMetrics(SM_CXSCREEN);
    if (h <= 0) h = GetSystemMetrics(SM_CYSCREEN);
    *outX = offsetX + (normX * w / 1000);
    *outY = offsetY + (normY * h / 1000);
}

int ExecuteScript(const char *code, float speed) {
    printf("[Backend] Executing script...\n");
    
    // Focus Window
    // Focus Window
    // Always try to restore the captured window, regardless of mode
    if (g_target_hwnd && IsWindow(g_target_hwnd)) {
        printf("[Backend] Focusing target window...\n");
        
        // METHOD 0: Disable Foreground Lock Timeout (Session wide setting)
        // This prevents Windows from flashing the button in taskbar instead of focusing.
        DWORD timeout = 0;
        SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, (PVOID)(size_t)timeout, SPIF_SENDWININICHANGE | SPIF_UPDATEINIFILE);

        // Unlock SetForegroundWindow if locked
        LockSetForegroundWindow(2); // LSFW_UNLOCK

        // Try multiple methods to force focus
        if(IsIconic(g_target_hwnd)) ShowWindow(g_target_hwnd, SW_RESTORE);
        
        // Method 0: Allow
        AllowSetForegroundWindow(ASFW_ANY);

        // Method 1: SwitchToThisWindow (often more aggressive)
        SwitchToThisWindow(g_target_hwnd, TRUE);

        // Method 1.5: Simulate Input to trick Windows
        // Press and release Shift
        keybd_event(VK_SHIFT, 0, 0, 0);
        keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);

        // Method 2: AttachThreadInput hack (Enabled for robustness)
        DWORD myId = GetCurrentThreadId();
        DWORD targetId = GetWindowThreadProcessId(g_target_hwnd, NULL);
        if (myId != targetId) {
            AttachThreadInput(myId, targetId, TRUE);
            BringWindowToTop(g_target_hwnd);
            SetForegroundWindow(g_target_hwnd);
            AttachThreadInput(myId, targetId, FALSE);
        }
        
        Sleep(300); 
    }

    POINT last_pos; GetCursorPos(&last_pos);
    char *code_copy = strdup(code);
    char *line = strtok(code_copy, "\n");
    
    while(line) {
        // Kill Switch - Reverted to 100px per user request
        POINT curr_pos; GetCursorPos(&curr_pos);
        double dist = sqrt(pow(curr_pos.x - last_pos.x, 2) + pow(curr_pos.y - last_pos.y, 2));
        if (dist > 100.0) { 
            printf("[Backend] ABORT: Mouse moved dist=%.2f (>100.0). Last(%ld,%ld) Curr(%ld,%ld)\n", 
                dist, last_pos.x, last_pos.y, curr_pos.x, curr_pos.y);
            free(code_copy); return -1; 
        }

        int p1, p2, p3, p4; char s_arg[256]; char s_btn[32];
        int tx, ty, tx2, ty2;

        if (sscanf(line, "drag ( \"%[^\"]\" , %d , %d , %d , %d )", s_btn, &p1, &p2, &p3, &p4) == 5) {
            GetTargetCoords(p1, p2, &tx, &ty);
            GetTargetCoords(p3, p4, &tx2, &ty2);
            PerformDrag(s_btn, tx, ty, tx2, ty2);
            // Wait a bit for OS to update cursor pos, then trust actual pos
            Sleep(50);
            GetCursorPos(&last_pos); 
        }
        else if (sscanf(line, "click ( \"%[^\"]\" , %d , %d )", s_btn, &p1, &p2) == 3) {
            GetTargetCoords(p1, p2, &tx, &ty);
            PerformClick(s_btn, tx, ty, 0);
            GetCursorPos(&last_pos);
        }
        else if (sscanf(line, "click ( %d , %d )", &p1, &p2) == 2) {
            GetTargetCoords(p1, p2, &tx, &ty);
            PerformClick("left", tx, ty, 0);
            GetCursorPos(&last_pos);
        }
        else if (sscanf(line, "dblclick ( %d , %d )", &p1, &p2) == 2) {
            GetTargetCoords(p1, p2, &tx, &ty);
            PerformClick("left", tx, ty, 1);
            GetCursorPos(&last_pos);
        }
        else if (sscanf(line, "move ( %d , %d )", &p1, &p2) == 2) {
            GetTargetCoords(p1, p2, &tx, &ty);
            SetCursorPos(tx, ty);
            GetCursorPos(&last_pos);
        }
        else if (sscanf(line, "wait ( %d )", &p1) == 1) {
            int chunks = p1 / 100; if (chunks == 0) chunks = 1;
            for(int i=0; i<chunks; i++) {
                Sleep((DWORD)(100 / speed));
                GetCursorPos(&curr_pos);
                if (sqrt(pow(curr_pos.x - last_pos.x, 2) + pow(curr_pos.y - last_pos.y, 2)) > 100.0) {
                     printf("[Backend] ABORT (Wait): Mouse moved.\n");
                     free(code_copy); return -1;
                }
            }
        }
        else if (sscanf(line, "type ( \"%[^\"]\" )", s_arg) == 1) {
            PerformType(s_arg);
        }
        else if (sscanf(line, "scroll ( %d )", &p1) == 1) {
            PerformScroll(p1);
        }
        else if (sscanf(line, "keyDown ( \"%[^\"]\" )", s_arg) == 1) {
            PerformKey(s_arg, true);
        }
        else if (sscanf(line, "keyUp ( \"%[^\"]\" )", s_arg) == 1) {
            PerformKey(s_arg, false);
        }
        
        Sleep((DWORD)(50 / speed));
        line = strtok(NULL, "\n");
    }
    free(code_copy);
    printf("[Backend] Script finished.\n");
    return 0;
}

DWORD WINAPI ServerThread(LPVOID lpParam) {
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    address.sin_family = AF_INET; address.sin_addr.s_addr = INADDR_ANY; address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) return 0;
    listen(server_fd, 3);
    
    HWND hExist = FindWindowA(NULL, "TinyAgent UI v19");
    if (hExist) {
        SetForegroundWindow(hExist);
    } else {
        ShellExecuteA(NULL, "open", "http://localhost:8080", NULL, NULL, SW_SHOWNORMAL);
    }

    while(g_server_running) {
        SOCKET new_socket = accept(server_fd, NULL, NULL);
        if(new_socket == INVALID_SOCKET) continue;

        char buffer[8192] = {0}; 
        recv(new_socket, buffer, sizeof(buffer)-1, 0);

        if (strstr(buffer, "GET / ")) {
            char header[512]; 
            sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", strlen(g_index_html));
            send_full(new_socket, header, strlen(header));
            send_full(new_socket, g_index_html, strlen(g_index_html));
        } else if (strstr(buffer, "GET /heartbeat")) {
            const char *resp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK";
            send_full(new_socket, resp, strlen(resp));
        } else if (strstr(buffer, "POST /shutdown")) {
            const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nBye";
            send_full(new_socket, resp, strlen(resp));
            g_server_running = 0; PostThreadMessage(g_main_thread_id, WM_QUIT, 0, 0);
        } else if (strstr(buffer, "GET /events")) {
            for(int i=0; i<30; i++) { if(g_has_new_screenshot || !g_server_running) break; Sleep(100); }
            EnterCriticalSection(&g_img_lock);
            if(g_has_new_screenshot && g_last_screenshot_b64) {
                g_has_new_screenshot = 0;
                const char *prefix = "{\"type\":\"screenshot\",\"image\":\"";
                const char *suffix = "\"}";
                size_t json_len = strlen(prefix) + g_last_screenshot_len + strlen(suffix);
                char header[512];
                sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n", (unsigned long)json_len);
                send_full(new_socket, header, strlen(header));
                send_full(new_socket, prefix, strlen(prefix));
                send_full(new_socket, g_last_screenshot_b64, g_last_screenshot_len);
                send_full(new_socket, suffix, strlen(suffix));
            } else {
                const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
                send_full(new_socket, resp, strlen(resp));
            }
            LeaveCriticalSection(&g_img_lock);
        } else if (strstr(buffer, "POST /run")) {
            char *body = strstr(buffer, "\r\n\r\n");
            if (body) {
                body += 4;
                float speed = 1.0;
                char *speed_start = strstr(body, "\"speed\":"); if(speed_start) sscanf(speed_start + 8, "%f", &speed);
                char *code_start = strstr(body, "\"code\":");
                if(code_start) {
                    code_start += 7; char *qs = strchr(code_start, '"');
                    if (qs) {
                        char script[1024*10] = {0}; int si=0; qs++; 
                        while(*qs) {
                            if(*qs == '"' && *(qs-1) != '\\') break; 
                            if(*qs == '\\' && *(qs+1) == 'n') { script[si++]='\n'; qs+=2; continue; }
                            if(*qs == '\\' && *(qs+1) == '"') { script[si++]='"'; qs+=2; continue; }
                            script[si++] = *qs++;
                        }
                        
                        int res = ExecuteScript(script, speed);

                        if (res == -1) {
                            const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nABORTED";
                            send_full(new_socket, resp, strlen(resp));
                        } else {
                            const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nOK";
                            send_full(new_socket, resp, strlen(resp));
                        }
                    }
                }
            }
        }
        closesocket(new_socket);
    }
    return 0;
}

int main() {
    g_main_thread_id = GetCurrentThreadId();
    InitializeCriticalSection(&g_img_lock);
    KillOldInstance(); 
    SetProcessDPIAware(); 
    GdiplusStartupInput gdiplusStartupInput; ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    GetEncoderClsid(L"image/png", &g_pngClsid);

    RegisterHotKey(NULL, ID_HOTKEY_FULL, 0, VK_F9);
    RegisterHotKey(NULL, ID_HOTKEY_ACTIVE, 0, VK_F10);

    CreateThread(NULL, 0, ServerThread, NULL, 0, NULL);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0) != 0) {
        if (msg.message == WM_HOTKEY) {
            int type = 1; // Full
            if (msg.wParam == ID_HOTKEY_ACTIVE) type = 2; // Active
            CaptureScreen(type);
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    GdiplusShutdown(gdiplusToken);
    DeleteCriticalSection(&g_img_lock);
    return 0;
}
