@echo off
REM ────────────────────────────────────────────────────────
REM  build.bat — Build ViewBrowserHistory (C Edition) v3.0
REM
REM  Works with either MinGW (gcc) or MSVC (cl).
REM  Tries gcc first, falls back to cl.
REM ────────────────────────────────────────────────────────

echo.
echo  ======================================================
echo   Building ViewBrowserHistory - C Edition v3.0
echo  ======================================================
echo.

set SOURCES=brave_history.c ^
    history_db.c ^
    platform.c ^
    logging.c ^
    export_json.c ^
    export_pdf.c ^
    export_csv.c ^
    export_html_report.c ^
    export_markdown.c ^
    import_ext.c ^
    categorize.c ^
    url_parser.c ^
    csv_import.c ^
    html_bookmark_import.c ^
    session_tracker.c ^
    domain_trie.c ^
    config_parser.c ^
    url_categorize.c ^
    search_engine.c ^
    browser_detect.c ^
    vendor\sqlite3.c

set OUTPUT=viewbrowserhistory.exe

REM Try GCC first
where gcc >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo  Compiler: GCC ^(MinGW^)
    echo  Compiling 21 source files...
    echo.
    gcc -O2 -Wall -Wextra -Wno-unused-parameter -std=c11 ^
        -D_GNU_SOURCE ^
        -o %OUTPUT% ^
        %SOURCES% ^
        -lshell32 -lole32
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo  SUCCESS: %OUTPUT% built successfully!
        echo.
        echo  Usage:
        echo    %OUTPUT% --help
        echo    %OUTPUT% --all-browsers --html report.html
        echo    %OUTPUT% --browser firefox --days 30 --csv history.csv
        echo    %OUTPUT% --search "domain:github.com" --json results.json
        echo    %OUTPUT% --categorize --markdown analysis.md
        echo.
    ) else (
        echo  ERROR: Compilation failed.
    )
    goto :end
)

REM Try MSVC
where cl >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo  Compiler: MSVC ^(cl.exe^)
    echo  Compiling 21 source files...
    echo.
    cl /O2 /W3 /D_CRT_SECURE_NO_WARNINGS ^
        /Fe:%OUTPUT% ^
        %SOURCES% ^
        shell32.lib ole32.lib
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo  SUCCESS: %OUTPUT% built successfully!
        echo.
    ) else (
        echo  ERROR: Compilation failed.
    )
    goto :end
)

echo  ERROR: No C compiler found!
echo  Please install one of:
echo    - MinGW-w64: https://www.mingw-w64.org/
echo    - Visual Studio Build Tools: https://visualstudio.microsoft.com/
echo.

:end
