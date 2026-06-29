# Brave Browser History Access Tool
**v2.0** — Three implementations: JavaScript (browser), C (native CLI), Chrome Extension (incognito tracking)

A comprehensive toolkit for accessing, tracking, and exporting Brave browser history. Three independent implementations target different use cases.

---

## Architecture Overview

```
delivery/
├── js/             JavaScript — browser APIs + PDF delivery
├── c/              C — direct SQLite DB reader + PDF/JSON export
├── extension/      Chrome Extension — incognito history tracker
└── README.md       This file
```

### How They Connect

```
┌─────────────────┐     ┌──────────────────────┐
│  Chrome Extension │────▶│  Export JSON button   │
│  (tracks incognito│     │  (downloads .json)    │
│   browsing live)  │     └──────────┬───────────┘
└─────────────────┘              │
                                  ▼
┌─────────────────┐     ┌──────────────────────┐
│  Brave Browser   │     │   C Tool             │
│  History DB      │────▶│   --import ext.json   │──▶ JSON + PDF
│  (SQLite on disk)│     │   --json / --pdf      │    reports
└─────────────────┘     └──────────────────────┘
```

1. **Extension** tracks all browsing (including incognito) → exports JSON
2. **C tool** reads Brave's native SQLite DB + imports extension JSON → combined reports
3. **JS version** provides browser-based access via extension APIs

---

## Quick Start

### Option 1: Native History Access (C)
```bash
cd c/
build.bat                                           # compile (Windows)
brave-history.exe --json history.json --pdf report.pdf --days 30
```

### Option 2: Incognito Tracking (Extension + C)
```bash
# 1. Install extension in Brave
#    brave://extensions → Developer mode → Load unpacked → select extension/
# 2. Enable "Allow in Incognito" in extension details
# 3. Browse normally + incognito — extension records everything
# 4. Click extension icon → Export → saves tracked-history.json
# 5. Process with C tool:
cd c/
brave-history.exe --import tracked-history.json --pdf incognito-report.pdf
```

### Option 3: Browser-Based (JS)
```bash
cd js/
npm install
node generate-pdf.js      # generates deliverable PDF
```

---

## Implementations

| | JavaScript (`js/`) | C (`c/`) | Extension (`extension/`) |
|---|---|---|---|
| **Approach** | Browser APIs, service workers | Direct SQLite DB read | Tab event tracking |
| **Incognito Access** | Limited (session only) | ❌ (not on disk) | ✅ **Full tracking** |
| **Runtime** | Browser + Node.js | Native executable | Chrome/Brave browser |
| **Dependencies** | `pdf-lib` (npm) | SQLite (vendored) | None |
| **Output** | JSON download, PDF | JSON file, styled PDF | JSON export |
| **Integration** | Standalone | Reads DB + extension JSON | Exports for C tool |

---

## C Tool (`c/`)

### Building

```bash
cd c/
build.bat          # Windows (auto-detects GCC or MSVC)
# or: make         # Linux / macOS
```

### Usage

```bash
# Basic — read Brave's native history
brave-history --json history.json --pdf report.pdf

# Last N days only
brave-history --pdf report.pdf --days 7

# Import extension data (incognito history)
brave-history --import tracked-history.json --pdf combined.pdf

# Extension-only (skip native DB)
brave-history --no-db --import tracked.json --pdf incognito-only.pdf

# Merge native + extension
brave-history --import ext.json --json merged.json --pdf merged.pdf --days 30

# Custom database path
brave-history --db "C:\path\to\History" --json out.json
```

### Files

| File | Purpose |
|------|---------|
| `brave_history.c` | Main CLI entry point |
| `history_db.c/.h` | SQLite query layer |
| `import_ext.c/.h` | Extension JSON importer |
| `export_json.c/.h` | JSON export |
| `export_pdf.c/.h` | Raw PDF generation (no library) |
| `platform.c/.h` | OS detection, file paths, time utils |
| `vendor/sqlite3.c/.h` | SQLite amalgamation |

### JSON Output Format

```json
{
  "success": true,
  "timestamp": "2026-04-27T13:41:27Z",
  "error": null,
  "stats": {
    "total_urls": 3776,
    "total_visits": 6710,
    "unique_domains": 191
  },
  "data": [
    {
      "url": "https://...",
      "title": "Page Title",
      "visit_count": 5,
      "last_visit_time": "2026-04-27T13:38:10Z"
    }
  ]
}
```

---

## Chrome Extension (`extension/`)

### Installation

1. Open `brave://extensions/` (or `chrome://extensions/`)
2. Enable **Developer mode** (top right toggle)
3. Click **Load unpacked** → select the `extension/` folder
4. Go to extension details → enable **Allow in Incognito**

### Features

- **Tracks all tab navigations** including incognito windows
- **Extracts page metadata** (description, Open Graph, keywords)
- **Batched storage writes** for performance (flushes every 30s or 50 entries)
- **Dark-themed popup UI** with:
  - Live stats (total / incognito / normal / domains)
  - Filter buttons (All / Incognito / Normal)
  - Full-text search across URLs and titles
  - One-click JSON export (C-tool compatible format)
  - Clear history with confirmation dialog
- **50,000 entry cap** to prevent storage overflow
- **Badge indicator** shows "INC" on incognito tabs

### Files

| File | Purpose |
|------|---------|
| `manifest.json` | Extension manifest (Manifest V3) |
| `background.js` | Service worker — tab tracking + storage |
| `content.js` | Content script — page metadata extraction |
| `popup.html/css/js` | Popup UI |
| `icons/` | Extension icons (16/48/128px) |

### Export Format

The extension exports JSON matching the C tool's import format:

```json
{
  "source": "incognito-history-tracker-extension",
  "version": "2.0.0",
  "stats": {
    "total_urls": 150,
    "total_incognito": 47,
    "total_normal": 103
  },
  "data": [
    {
      "url": "https://...",
      "title": "Page Title",
      "timestamp": "2026-04-27T13:38:10Z",
      "incognito": true
    }
  ]
}
```

---

## JavaScript Version (`js/`)

### Methods

1. **Chrome Extension History API** — Full history via extension permissions
2. **Browser History API** — Current page, referrer, length
3. **Local Storage Tracking** — Continuous page logging
4. **Service Worker Interception** — Network request capture
5. **File System Access API** — Experimental direct SQLite access
6. **Incognito Detection** — Multi-heuristic detection

### PDF Delivery

```bash
cd js/
npm install
node generate-pdf.js
```

Generates a PDF that auto-executes JavaScript in Adobe Acrobat with all source files embedded as attachments.

---

## Brave Data Locations

| OS | Path |
|----|------|
| **Windows** | `%LOCALAPPDATA%\BraveSoftware\Brave-Browser\User Data\Default\History` |
| **macOS** | `~/Library/Application Support/BraveSoftware/Brave-Browser/Default/History` |
| **Linux** | `~/.config/BraveSoftware/Brave-Browser/Default/History` |

---

## Ethical Use

This toolkit is for **personal data backup, analysis, and education**.

- ✅ Tracking your own browsing for productivity analysis
- ✅ Backing up your own history
- ✅ Learning about browser internals
- ❌ Monitoring others without their knowledge or consent
- ❌ Unauthorized data collection

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `Brave DB not found` | Install Brave, or use `--db` / `--import` |
| `Cannot copy database` | Close Brave (it locks the DB), or copy manually |
| Extension not tracking incognito | Enable "Allow in Incognito" in extension details |
| Extension shows 0 entries | Browse some pages first; data batches every 30s |
| PDF won't open | Use Adobe Acrobat/Reader for JS-enabled PDFs |

---

## License

Educational use. Respect privacy.
