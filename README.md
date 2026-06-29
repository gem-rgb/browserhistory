# ViewBrowserHistory
**v3.0** — Multi-browser history analysis toolkit in C with fuzzing infrastructure

A comprehensive, native C toolkit that reads browser history databases directly from disk, supporting 7 browsers across Windows, macOS, and Linux. Features URL categorization, full-text search, session analysis, domain analytics, and 5 export formats.

---

## Architecture Overview

```
delivery/
├── c/                  C — multi-browser history reader + analysis engine
│   ├── brave_history.c         Main CLI entry point
│   ├── browser_detect.c/.h     Multi-browser detection & profile enumeration
│   ├── history_db.c/.h         SQLite query layer
│   ├── url_parser.c/.h         RFC 3986 URL parser
│   ├── url_categorize.c/.h     URL categorization engine (120+ domain rules)
│   ├── search_engine.c/.h      Full-text search with query language
│   ├── session_tracker.c/.h    Browsing session detection & analysis
│   ├── domain_trie.c/.h        Domain-based trie for aggregation
│   ├── config_parser.c/.h      INI/TOML config file parser
│   ├── csv_import.c/.h         CSV/TSV importer with auto-detection
│   ├── html_bookmark_import.c/.h  HTML bookmark importer (Netscape format)
│   ├── import_ext.c/.h         Extension JSON importer
│   ├── categorize.c/.h         AI-style browsing pattern analysis
│   ├── logging.c/.h            Structured color-coded logging
│   ├── platform.c/.h           OS detection, file paths, time utils
│   ├── export_json.c/.h        JSON export
│   ├── export_pdf.c/.h         Raw PDF generation (no library)
│   ├── export_csv.c/.h         RFC 4180 CSV export
│   ├── export_html_report.c/.h Interactive HTML report with dark theme
│   ├── export_markdown.c/.h    GFM Markdown export
│   └── vendor/sqlite3.c/.h     SQLite3 amalgamation
├── fuzz/               Fuzzing harnesses + seed corpus
│   ├── url_parser_fuzzer.c     URL parsing fuzzer
│   ├── session_fuzzer.c        Session tracker fuzzer
│   ├── import_json_fuzzer.c    JSON import fuzzer
│   ├── bookmark_import_fuzzer.c HTML bookmark fuzzer
│   ├── csv_import_fuzzer.c     CSV import fuzzer
│   ├── config_fuzzer.c         Config parser + categorization rules fuzzer
│   ├── search_query_fuzzer.c   Search query parser fuzzer
│   ├── dictionary.txt          Shared fuzzing dictionary
│   └── corpus/                 Seed corpus per harness
├── .clusterfuzzlite/   ClusterFuzzLite CI integration
├── extension/          Chrome Extension — incognito history tracker
├── js/                 JavaScript — browser APIs + PDF delivery
└── README.md           This file
```

---

## Supported Browsers

| Browser   | Engine   | DB Schema        | Windows | macOS | Linux |
|-----------|----------|------------------|:-------:|:-----:|:-----:|
| Brave     | Chromium | `urls` + `visits`  | ✅ | ✅ | ✅ |
| Chrome    | Chromium | `urls` + `visits`  | ✅ | ✅ | ✅ |
| Chromium  | Chromium | `urls` + `visits`  | ✅ | ✅ | ✅ |
| Edge      | Chromium | `urls` + `visits`  | ✅ | ✅ | ✅ |
| Vivaldi   | Chromium | `urls` + `visits`  | ✅ | ✅ | ✅ |
| Opera     | Chromium | `urls` + `visits`  | ✅ | ✅ | ✅ |
| Firefox   | Gecko    | `moz_places` + `moz_historyvisits` | ✅ | ✅ | ✅ |

**Timestamp handling:** Chromium-based browsers use microseconds since 1601-01-01; Firefox uses microseconds since Unix epoch. Both are normalized to ISO 8601.

---

## Quick Start

### Build

```bash
# Linux / macOS
cd c/
make

# Windows
cd c\
build.bat
```

### Basic Usage

```bash
# Auto-detect Brave and show summary
viewbrowserhistory

# Query all installed browsers
viewbrowserhistory --all-browsers

# Query a specific browser
viewbrowserhistory --browser firefox --days 30

# Export to various formats
viewbrowserhistory --all-browsers --json history.json
viewbrowserhistory --all-browsers --csv history.csv
viewbrowserhistory --all-browsers --html report.html
viewbrowserhistory --all-browsers --markdown analysis.md
viewbrowserhistory --all-browsers --pdf report.pdf
```

### Analysis Features

```bash
# Categorize URLs into browsing categories
viewbrowserhistory --all-browsers --categorize

# Use custom categorization rules
viewbrowserhistory --categorize --rules my-rules.conf

# Full-text search with query language
viewbrowserhistory --search "domain:github.com"
viewbrowserhistory --search "regex:\"pull/[0-9]+\" after:2024-01-01 visits:5"
viewbrowserhistory --search "\"javascript\" AND domain:stackoverflow.com"

# Session and domain analytics
viewbrowserhistory --all-browsers --sessions --domains

# Combined analysis + export
viewbrowserhistory --all-browsers --categorize --sessions --domains --html report.html
```

### Import & Merge

```bash
# Import extension JSON
viewbrowserhistory --import tracked-history.json --pdf combined.pdf

# Import HTML bookmarks
viewbrowserhistory --import bookmarks.html --csv exported.csv

# Import CSV data
viewbrowserhistory --import history.csv --json merged.json

# Skip native DB, use only imports
viewbrowserhistory --no-db --import data.json --html report.html

# Multiple imports
viewbrowserhistory --import ext1.json --import ext2.json --import bookmarks.html
```

---

## Search Query Language

The `--search` flag supports a mini query language:

| Syntax | Description | Example |
|--------|-------------|---------|
| `keyword` | Substring match in URL or title | `javascript` |
| `"exact phrase"` | Quoted exact phrase | `"stack overflow"` |
| `regex:"pattern"` | POSIX regex match | `regex:"pull/[0-9]+"` |
| `domain:host` | Filter by domain | `domain:github.com` |
| `tld:ext` | Filter by TLD | `tld:org` |
| `after:date` | Entries after date | `after:2024-01-01` |
| `before:date` | Entries before date | `before:2024-12-31` |
| `visits:N` | Minimum visit count | `visits:5` |
| `AND` | Boolean AND | `term1 AND term2` |
| `OR` | Boolean OR | `term1 OR term2` |
| `-term` | Exclude (NOT) | `-"excluded"` |

Results are ranked by relevance (URL/title match quality), visit count, and recency.

---

## URL Categorization

Built-in categories with 120+ domain rules:

| Category | Example Domains |
|----------|-----------------|
| Social Media | facebook.com, twitter.com, reddit.com, discord.com |
| News | cnn.com, bbc.co.uk, nytimes.com, techcrunch.com |
| Shopping | amazon.*, ebay.*, walmart.com, etsy.com |
| Development | github.com, stackoverflow.com, docs.python.org |
| Streaming | youtube.com, netflix.com, spotify.com, twitch.tv |
| Search | google.com, bing.com, duckduckgo.com |
| Email | mail.google.com, outlook.live.com, proton.me |

### Custom Rules File

```ini
[My Category]
domains = example.com, *.example.org, subdomain.*
keywords = keyword1, keyword2
color = #FF5733
paths[example.com] = /specific/path/*
```

---

## Configuration File

```ini
[general]
default_days = 30
output_dir = ~/browser-history-exports

[categories]
config_file = ~/.config/viewbrowserhistory/rules.conf
enabled = true

[search]
case_sensitive = false
max_results = 500

[export]
default_format = json
include_stats = true
```

Load with `--config settings.conf`.

---

## Export Formats

| Format | Flag | Features |
|--------|------|----------|
| **JSON** | `--json` | Machine-readable, includes stats |
| **CSV** | `--csv` | RFC 4180, Excel-compatible (UTF-8 BOM) |
| **PDF** | `--pdf` | Styled report with AI insights |
| **HTML** | `--html` | Interactive dark-mode report with sortable tables and category charts |
| **Markdown** | `--markdown` | GFM tables, clickable links, ToC, category breakdown |

---

## Fuzzing Infrastructure

7 fuzzing harnesses target all major input-processing subsystems:

| Harness | Target Module | Attack Surface |
|---------|---------------|----------------|
| `url_parser_fuzzer` | URL parser | Malformed URLs, Unicode, edge cases |
| `session_fuzzer` | Session tracker | Stateful event-stream parsing |
| `import_json_fuzzer` | JSON importer | Malformed JSON, deep nesting |
| `bookmark_import_fuzzer` | HTML bookmark importer | Malformed HTML, nested tags |
| `csv_import_fuzzer` | CSV importer | Encoding, delimiters, quoting |
| `config_fuzzer` | Config parser + categorization rules | INI parsing, glob matching |
| `search_query_fuzzer` | Search query parser | Regex compilation, boolean logic |

### Running with ClusterFuzzLite

```yaml
# .clusterfuzzlite/project.yaml
language: c
```

Seed corpus and a shared dictionary are included per harness.

---

## Building from Source

### Dependencies

- **C11 compiler** (GCC, Clang, or MSVC)
- **SQLite3** (vendored in `vendor/sqlite3.c`)
- No external libraries required

### Linux / macOS

```bash
cd c/
make                    # build
make install            # install to /usr/local/bin
make loc                # count lines of code
```

### Windows

```batch
cd c\
build.bat               :: auto-detects GCC or MSVC
```

### Optional: System SQLite

```bash
make USE_SYSTEM_SQLITE=1
```

---

## Chrome Extension (`extension/`)

Tracks all browsing (including incognito) and exports JSON compatible with the C tool's `--import` flag.

1. Open `brave://extensions/` → Enable **Developer mode**
2. Click **Load unpacked** → select `extension/`
3. Enable **Allow in Incognito** in extension details
4. Browse normally → Click extension icon → **Export**
5. Process: `viewbrowserhistory --import tracked-history.json --html report.html`

---

## Browser Data Locations

### Chromium-based (Brave, Chrome, Edge, Vivaldi, Opera)

| OS | Base Path |
|----|-----------|
| **Windows** | `%LOCALAPPDATA%\<vendor>\User Data\<Profile>\History` |
| **macOS** | `~/Library/Application Support/<vendor>/<Profile>/History` |
| **Linux** | `~/.config/<vendor>/<Profile>/History` |

### Firefox

| OS | Base Path |
|----|-----------|
| **Windows** | `%APPDATA%\Mozilla\Firefox\Profiles\<random>.default\places.sqlite` |
| **macOS** | `~/Library/Application Support/Firefox/Profiles/<random>.default/places.sqlite` |
| **Linux** | `~/.mozilla/firefox/<random>.default/places.sqlite` |

---

## Ethical Use

This toolkit is for **personal data backup, analysis, and education**.

- ✅ Tracking your own browsing for productivity analysis
- ✅ Backing up your own history
- ✅ Learning about browser internals and database schemas
- ❌ Monitoring others without their knowledge or consent
- ❌ Unauthorized data collection

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| No browsers detected | Use `--db <path>` to specify a History file directly |
| Cannot copy database | Close the browser (it locks the DB), or copy manually |
| Firefox not detected | Ensure `~/.mozilla/firefox/` exists with a profile |
| Extension not tracking incognito | Enable "Allow in Incognito" in extension details |
| CSV garbled in Excel | File uses UTF-8 BOM — open with "UTF-8" encoding in Excel |
| Search regex error | Use quotes: `--search "regex:\"pattern\""` |

---

## License

Educational use. Respect privacy.
