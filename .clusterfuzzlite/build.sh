#!/bin/bash -eu
# ClusterFuzzLite build script for ViewBrowserHistory
#
# Environment variables set by ClusterFuzzLite:
#   $CC       - C compiler (clang with sanitizer flags)
#   $CXX      - C++ compiler
#   $CFLAGS   - C compiler flags (includes -fsanitize=...)
#   $CXXFLAGS - C++ compiler flags
#   $LIB_FUZZING_ENGINE - Path to the fuzzing engine library
#   $SRC      - Source directory
#   $OUT      - Output directory for fuzzer binaries

cd $SRC

# ── Compile all project source files ──────────────────────────────

SRCS=(
    c/import_ext.c
    c/platform.c
    c/history_db.c
    c/export_json.c
    c/export_pdf.c
    c/export_csv.c
    c/export_html_report.c
    c/export_markdown.c
    c/categorize.c
    c/logging.c
    c/url_parser.c
    c/csv_import.c
    c/html_bookmark_import.c
    c/session_tracker.c
    c/domain_trie.c
    c/config_parser.c
    c/url_categorize.c
    c/search_engine.c
    c/browser_detect.c
)

OBJS=()
for src in "${SRCS[@]}"; do
    obj=$(basename "$src" .c).o
    $CC $CFLAGS -I c/ -I c/vendor -c "$src" -o "$obj"
    OBJS+=("$obj")
done

# Compile vendored SQLite separately (may need relaxed warnings)
$CC $CFLAGS -w -I c/vendor -c c/vendor/sqlite3.c -o sqlite3.o
OBJS+=("sqlite3.o")

# ── Build each fuzzer ─────────────────────────────────────────────

for fuzzer_src in fuzz/*_fuzzer.c; do
    fuzzer_name=$(basename "$fuzzer_src" .c)
    $CC $CFLAGS -I c/ -I c/vendor -c "$fuzzer_src" -o "${fuzzer_name}.o"
    $CXX $CXXFLAGS $LIB_FUZZING_ENGINE "${fuzzer_name}.o" "${OBJS[@]}" \
        -lpthread -ldl -lm -o "$OUT/${fuzzer_name}"

    # Copy seed corpus if it exists
    if [ -d "fuzz/corpus/${fuzzer_name}" ]; then
        zip -j "$OUT/${fuzzer_name}_seed_corpus.zip" fuzz/corpus/${fuzzer_name}/*
    fi

    # Copy per-fuzzer dictionary if it exists, otherwise use shared dictionary
    if [ -f "fuzz/${fuzzer_name}.dict" ]; then
        cp "fuzz/${fuzzer_name}.dict" "$OUT/${fuzzer_name}.dict"
    elif [ -f "fuzz/dictionary.txt" ]; then
        cp "fuzz/dictionary.txt" "$OUT/${fuzzer_name}.dict"
    fi
done

echo "[build.sh] Built $(ls $OUT/*_fuzzer 2>/dev/null | wc -l) fuzzers successfully"
