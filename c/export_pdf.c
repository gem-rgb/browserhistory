/**
 * export_pdf.c — Raw PDF generation implementation
 *
 * Generates a styled PDF file by writing the PDF file format directly.
 * No external PDF library needed — PDF is a well-documented text format.
 *
 * Layout matches the JS version: dark header bar, orange title accent,
 * cover page with stats, and a multi-page history listing.
 *
 * Architecture: Objects are written sequentially. The Pages object
 * (which needs the full list of page Kids) is written LAST so we
 * don't need to seek back and risk corrupting earlier objects.
 * The Catalog references a reserved object ID for Pages.
 *
 * @version 2.0.0
 */

#include "export_pdf.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── PDF Constants ───────────────────────────────────────────────── */

#define PAGE_WIDTH    612.0f     /* US Letter */
#define PAGE_HEIGHT   792.0f
#define MARGIN_LEFT    50.0f
#define MARGIN_RIGHT   50.0f
#define MARGIN_TOP     50.0f
#define MARGIN_BOTTOM  50.0f
#define LINE_HEIGHT    14.0f
#define HEADER_HEIGHT 100.0f
#define FOOTER_HEIGHT  30.0f

#define MAX_OBJECTS    4096
#define MAX_PAGES       512

/* ── Internal State ──────────────────────────────────────────────── */

typedef struct {
    FILE   *f;
    long    offsets[MAX_OBJECTS];    /* byte offset of each object */
    int     obj_count;              /* highest object ID allocated */
    int     page_obj_ids[MAX_PAGES];
    int     page_count;
    int     pages_obj_id;           /* reserved ID for /Pages object */
    int     catalog_obj_id;
    int     font_helv_id;
    int     font_helv_bold_id;
    int     font_courier_id;
    int     resources_id;
} PdfWriter;

/* ── Low-level PDF writing ───────────────────────────────────────── */

/** Reserve an object ID without writing anything yet. */
static int pdf_reserve_obj(PdfWriter *w) {
    return ++w->obj_count;
}

/** Begin writing an object with a specific pre-reserved ID. */
static void pdf_begin_obj_id(PdfWriter *w, int id) {
    w->offsets[id] = ftell(w->f);
    fprintf(w->f, "%d 0 obj\n", id);
}

/** Begin writing the next auto-numbered object. Returns the ID. */
static int pdf_begin_obj(PdfWriter *w) {
    int id = ++w->obj_count;
    w->offsets[id] = ftell(w->f);
    fprintf(w->f, "%d 0 obj\n", id);
    return id;
}

static void pdf_end_obj(PdfWriter *w) {
    fprintf(w->f, "endobj\n\n");
}

/* ── String Helpers ──────────────────────────────────────────────── */

/**
 * Escape a string for PDF text display.
 * PDF strings use ( ) delimiters with \ escaping.
 */
static void pdf_escape_string(char *dst, size_t dstsize, const char *src) {
    size_t j = 0;
    dst[0] = '\0';
    if (!src) return;

    for (size_t i = 0; src[i] && j < dstsize - 2; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
            case '(':  if (j + 2 < dstsize) { dst[j++] = '\\'; dst[j++] = '('; } break;
            case ')':  if (j + 2 < dstsize) { dst[j++] = '\\'; dst[j++] = ')'; } break;
            case '\\': if (j + 2 < dstsize) { dst[j++] = '\\'; dst[j++] = '\\'; } break;
            case '\t': if (j + 1 < dstsize) { dst[j++] = ' '; } break;
            default:
                if (c >= 32 && c < 127) {
                    dst[j++] = (char)c;
                }
                /* Skip non-ASCII and control chars for PDF safety */
                break;
        }
    }
    dst[j] = '\0';
}

/**
 * Truncate and escape a string to fit within maxchars.
 */
static void truncate_str(char *dst, size_t dstsize, const char *src, int maxchars) {
    if (!src || !*src) {
        snprintf(dst, dstsize, "(untitled)");
        return;
    }
    if ((int)strlen(src) <= maxchars) {
        pdf_escape_string(dst, dstsize, src);
    } else {
        char tmp[2048];
        int copylen = maxchars - 3;
        if (copylen < 0) copylen = 0;
        if ((size_t)copylen >= sizeof(tmp)) copylen = (int)sizeof(tmp) - 4;
        memcpy(tmp, src, (size_t)copylen);
        tmp[copylen] = '\0';
        strcat(tmp, "...");
        pdf_escape_string(dst, dstsize, tmp);
    }
}

/* ── Page Content Streams ────────────────────────────────────────── */

/**
 * Build the cover page content stream into buf.
 */
static void build_cover_page(char *buf, size_t bufsize,
                              const HistoryStats *stats, const char *timestamp) {
    char most_visited_esc[512];
    pdf_escape_string(most_visited_esc, sizeof(most_visited_esc), stats->most_visited_url);

    char most_title_esc[512];
    pdf_escape_string(most_title_esc, sizeof(most_title_esc), stats->most_visited_title);

    snprintf(buf, bufsize,
        /* Dark header background */
        "q\n"
        "0.13 0.13 0.17 rg\n"
        "0 692 612 100 re f\n"
        "Q\n"

        /* Title (orange) */
        "BT\n"
        "/F2 26 Tf\n"
        "1 0.6 0.2 rg\n"
        "50 740 Td\n"
        "(BRAVE HISTORY ACCESS) Tj\n"
        "ET\n"

        /* Subtitle */
        "BT\n"
        "/F1 11 Tf\n"
        "0.8 0.8 0.85 rg\n"
        "50 718 Td\n"
        "(Browser History Access Tool \\227 C Edition v2.0) Tj\n"
        "ET\n"

        /* Timestamp */
        "BT\n"
        "/F3 9 Tf\n"
        "0.5 0.5 0.55 rg\n"
        "50 700 Td\n"
        "(Generated: %s) Tj\n"
        "ET\n"

        /* About section */
        "BT\n"
        "/F2 15 Tf\n"
        "0.15 0.15 0.15 rg\n"
        "50 650 Td\n"
        "(About This Report) Tj\n"
        "ET\n"

        "BT\n"
        "/F1 10 Tf\n"
        "0.2 0.2 0.2 rg\n"
        "50 632 Td\n"
        "(This report was generated by the Brave History Access tool \\(C edition\\).) Tj\n"
        "0 -16 Td\n"
        "(It reads Brave Browser's SQLite history database directly from disk,) Tj\n"
        "0 -16 Td\n"
        "(providing comprehensive access to your browsing history.) Tj\n"
        "ET\n"

        /* Statistics header */
        "BT\n"
        "/F2 15 Tf\n"
        "0.15 0.15 0.15 rg\n"
        "50 560 Td\n"
        "(Summary Statistics) Tj\n"
        "ET\n"

        /* Stats box background */
        "q\n"
        "0.95 0.95 0.97 rg\n"
        "45 430 522 120 re f\n"
        "Q\n"

        /* Stats content */
        "BT\n"
        "/F1 11 Tf\n"
        "0.15 0.15 0.15 rg\n"
        "60 535 Td\n"
        "(Total URLs:           %d) Tj\n"
        "0 -18 Td\n"
        "(Total Visits:         %d) Tj\n"
        "0 -18 Td\n"
        "(Unique Domains:       %d) Tj\n"
        "0 -18 Td\n"
        "(Earliest Visit:       %s) Tj\n"
        "0 -18 Td\n"
        "(Latest Visit:         %s) Tj\n"
        "ET\n"

        /* Most visited */
        "BT\n"
        "/F2 15 Tf\n"
        "0.15 0.15 0.15 rg\n"
        "50 400 Td\n"
        "(Most Visited Page) Tj\n"
        "ET\n"

        "BT\n"
        "/F1 10 Tf\n"
        "0.2 0.2 0.2 rg\n"
        "60 380 Td\n"
        "(Title: %s) Tj\n"
        "0 -15 Td\n"
        "(Visits: %d) Tj\n"
        "ET\n"

        "BT\n"
        "/F3 8 Tf\n"
        "0.3 0.3 0.6 rg\n"
        "60 350 Td\n"
        "(%s) Tj\n"
        "ET\n"

        /* Access method */
        "BT\n"
        "/F2 15 Tf\n"
        "0.15 0.15 0.15 rg\n"
        "50 310 Td\n"
        "(Access Method) Tj\n"
        "ET\n"

        "BT\n"
        "/F1 10 Tf\n"
        "0.2 0.2 0.2 rg\n"
        "60 292 Td\n"
        "(Direct SQLite database read \\227 the most comprehensive method.) Tj\n"
        "0 -15 Td\n"
        "(Brave's History database was copied and queried directly.) Tj\n"
        "0 -15 Td\n"
        "(No browser extensions or APIs required.) Tj\n"
        "ET\n"

        /* Security notice */
        "BT\n"
        "/F2 15 Tf\n"
        "0.15 0.15 0.15 rg\n"
        "50 220 Td\n"
        "(Security Notice) Tj\n"
        "ET\n"

        "BT\n"
        "/F1 10 Tf\n"
        "0.4 0.15 0.15 rg\n"
        "60 202 Td\n"
        "(This tool is for personal and educational use only.) Tj\n"
        "0 -15 Td\n"
        "(Do not use it to monitor others without their consent.) Tj\n"
        "0 -15 Td\n"
        "(Incognito history is not accessible \\227 by design.) Tj\n"
        "ET\n"

        /* Footer bar */
        "q\n"
        "0.13 0.13 0.17 rg\n"
        "0 0 612 30 re f\n"
        "Q\n"

        "BT\n"
        "/F1 8 Tf\n"
        "0.5 0.5 0.55 rg\n"
        "50 10 Td\n"
        "(Brave History Access \\227 C Edition \\227 Page 1) Tj\n"
        "ET\n",

        timestamp,
        stats->total_urls,
        stats->total_visits,
        stats->unique_domains,
        stats->earliest_visit,
        stats->latest_visit,
        most_title_esc,
        stats->most_visited_count,
        most_visited_esc
    );
}

/**
 * Build a history listing page. Returns number of entries written.
 */
static int build_listing_page(char *buf, size_t bufsize,
                               const HistoryResult *result,
                               int start_idx, int page_num) {
    char *p = buf;
    char *end = buf + bufsize - 512;   /* safety margin */
    int written = 0;

    /* Header bar */
    p += snprintf(p, (size_t)(end - p),
        "q\n"
        "0.13 0.13 0.17 rg\n"
        "0 752 612 40 re f\n"
        "Q\n"
        "BT\n"
        "/F2 14 Tf\n"
        "1 0.6 0.2 rg\n"
        "50 764 Td\n"
        "(BROWSING HISTORY) Tj\n"
        "ET\n"
    );

    /* Column headers */
    p += snprintf(p, (size_t)(end - p),
        "q\n"
        "0.92 0.92 0.94 rg\n"
        "40 726 532 18 re f\n"
        "Q\n"
        "BT\n"
        "/F2 8 Tf\n"
        "0.15 0.15 0.15 rg\n"
        "45 730 Td\n"
        "(#) Tj\n"
        "30 0 Td\n"
        "(Title) Tj\n"
        "200 0 Td\n"
        "(URL) Tj\n"
        "190 0 Td\n"
        "(Visits) Tj\n"
        "45 0 Td\n"
        "(Last Visited) Tj\n"
        "ET\n"
    );

    float y = 714.0f;
    const float min_y = FOOTER_HEIGHT + 20.0f;

    for (int i = start_idx; i < result->count && y > min_y && p < end; i++) {
        const HistoryEntry *e = &result->entries[i];

        char title_esc[256];
        truncate_str(title_esc, sizeof(title_esc), e->title, 30);

        char url_esc[256];
        truncate_str(url_esc, sizeof(url_esc), e->url, 35);

        /* Extract short date (YYYY-MM-DD) */
        char date_short[32];
        size_t tlen = strlen(e->last_visit_time);
        if (tlen >= 10) {
            memcpy(date_short, e->last_visit_time, 10);
            date_short[10] = '\0';
        } else {
            strncpy(date_short, e->last_visit_time, sizeof(date_short) - 1);
            date_short[sizeof(date_short) - 1] = '\0';
        }

        /* Alternating row background */
        if (written % 2 == 0) {
            p += snprintf(p, (size_t)(end - p),
                "q\n0.97 0.97 0.98 rg\n40 %.0f 532 12 re f\nQ\n",
                y - 3.0f);
        }

        p += snprintf(p, (size_t)(end - p),
            "BT /F3 7 Tf 0.2 0.2 0.2 rg 45 %.0f Td (%d) Tj ET\n"
            "BT /F1 7 Tf 0.15 0.15 0.15 rg 75 %.0f Td (%s) Tj ET\n"
            "BT /F3 6 Tf 0.25 0.25 0.55 rg 275 %.0f Td (%s) Tj ET\n"
            "BT /F1 7 Tf 0.15 0.15 0.15 rg 465 %.0f Td (%d) Tj ET\n"
            "BT /F3 7 Tf 0.3 0.3 0.3 rg 510 %.0f Td (%s) Tj ET\n",
            y, i + 1,
            y, title_esc,
            y, url_esc,
            y, e->visit_count,
            y, date_short
        );

        y -= LINE_HEIGHT;
        written++;
    }

    /* Footer bar */
    p += snprintf(p, (size_t)(end - p),
        "q\n"
        "0.13 0.13 0.17 rg\n"
        "0 0 612 30 re f\n"
        "Q\n"
        "BT /F1 8 Tf 0.5 0.5 0.55 rg 50 10 Td "
        "(Brave History Access \\227 C Edition \\227 Page %d) Tj ET\n",
        page_num
    );

    return written;
}

/* ── Write a content stream + page object pair ───────────────────── */

static int write_page(PdfWriter *w, const char *content, int resources_id) {
    size_t stream_len = strlen(content);

    int stream_id = pdf_begin_obj(w);
    fprintf(w->f, "<< /Length %zu >>\n", stream_len);
    fprintf(w->f, "stream\n");
    fwrite(content, 1, stream_len, w->f);
    fprintf(w->f, "\nendstream\n");
    pdf_end_obj(w);

    int page_id = pdf_begin_obj(w);
    fprintf(w->f,
        "<< /Type /Page /Parent %d 0 R /MediaBox [0 0 612 792] "
        "/Resources %d 0 R /Contents %d 0 R >>\n",
        w->pages_obj_id, resources_id, stream_id);
    pdf_end_obj(w);

    return page_id;
}

/* ── Main PDF Export ─────────────────────────────────────────────── */

int export_to_pdf(const HistoryResult *result, const HistoryStats *stats,
                  const char *filepath) {
    PdfWriter w;
    memset(&w, 0, sizeof(w));

    w.f = fopen(filepath, "wb");
    if (!w.f) {
        fprintf(stderr, "[export_pdf] Cannot open %s for writing\n", filepath);
        return -1;
    }

    char timestamp[64];
    platform_timestamp_iso8601(timestamp, sizeof(timestamp));

    /* ── PDF Header ── */
    fprintf(w.f, "%%PDF-1.4\n");
    fprintf(w.f, "%%%c%c%c%c\n", 0xC0, 0xC1, 0xC2, 0xC3);

    /*
     * Object allocation strategy:
     *   1 = Catalog (written first, references Pages by reserved ID)
     *   2 = Pages   (RESERVED — written LAST, after all pages are known)
     *   3 = Font Helvetica
     *   4 = Font Helvetica-Bold
     *   5 = Font Courier
     *   6 = Resources
     *   7+ = page content streams and page objects
     *
     * Writing Pages last avoids the seek-back corruption bug.
     * PDF objects don't need to be in numerical order in the file.
     */

    /* Reserve IDs 1 and 2 */
    w.catalog_obj_id = pdf_reserve_obj(&w);   /* = 1 */
    w.pages_obj_id   = pdf_reserve_obj(&w);   /* = 2 */

    /* Write Catalog (obj 1) — references Pages as "2 0 R" */
    pdf_begin_obj_id(&w, w.catalog_obj_id);
    fprintf(w.f, "<< /Type /Catalog /Pages %d 0 R >>\n", w.pages_obj_id);
    pdf_end_obj(&w);

    /* Fonts (obj 3, 4, 5) */
    w.font_helv_id = pdf_begin_obj(&w);
    fprintf(w.f, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>\n");
    pdf_end_obj(&w);

    w.font_helv_bold_id = pdf_begin_obj(&w);
    fprintf(w.f, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>\n");
    pdf_end_obj(&w);

    w.font_courier_id = pdf_begin_obj(&w);
    fprintf(w.f, "<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>\n");
    pdf_end_obj(&w);

    /* Shared resources dict (obj 6) */
    w.resources_id = pdf_begin_obj(&w);
    fprintf(w.f,
        "<< /Font << /F1 %d 0 R /F2 %d 0 R /F3 %d 0 R >> >>\n",
        w.font_helv_id, w.font_helv_bold_id, w.font_courier_id);
    pdf_end_obj(&w);

    /* ── Generate all pages ── */

    size_t content_bufsize = 128 * 1024;
    char *content_buf = malloc(content_bufsize);
    if (!content_buf) {
        fprintf(stderr, "[export_pdf] Out of memory\n");
        fclose(w.f);
        return -1;
    }

    /* Cover page */
    build_cover_page(content_buf, content_bufsize, stats, timestamp);
    w.page_obj_ids[w.page_count++] = write_page(&w, content_buf, w.resources_id);

    /* History listing pages */
    int idx = 0;
    int page_num = 2;

    while (idx < result->count && w.page_count < MAX_PAGES) {
        int written = build_listing_page(content_buf, content_bufsize,
                                          result, idx, page_num);
        if (written == 0) break;

        w.page_obj_ids[w.page_count++] = write_page(&w, content_buf, w.resources_id);
        idx += written;
        page_num++;
    }

    free(content_buf);

    /* ── Write Pages object LAST (obj 2) — now we know all Kids ── */
    pdf_begin_obj_id(&w, w.pages_obj_id);
    fprintf(w.f, "<< /Type /Pages /Kids [");
    for (int i = 0; i < w.page_count; i++) {
        fprintf(w.f, "%d 0 R ", w.page_obj_ids[i]);
    }
    fprintf(w.f, "] /Count %d >>\n", w.page_count);
    pdf_end_obj(&w);

    /* ── Cross-reference table ── */
    long xref_offset = ftell(w.f);
    fprintf(w.f, "xref\n");
    fprintf(w.f, "0 %d\n", w.obj_count + 1);
    fprintf(w.f, "0000000000 65535 f \n");
    for (int i = 1; i <= w.obj_count; i++) {
        fprintf(w.f, "%010ld 00000 n \n", w.offsets[i]);
    }

    /* ── Trailer ── */
    fprintf(w.f, "trailer\n");
    fprintf(w.f, "<< /Size %d /Root %d 0 R >>\n", w.obj_count + 1, w.catalog_obj_id);
    fprintf(w.f, "startxref\n");
    fprintf(w.f, "%ld\n", xref_offset);
    fprintf(w.f, "%%%%EOF\n");

    fclose(w.f);

    printf("[export_pdf] Generated %d-page PDF: %s\n", w.page_count, filepath);
    return 0;
}

/* ── AI Analysis Page Content ────────────────────────────────────── */

static void build_analysis_page(char *buf, size_t bufsize,
                                 const BrowsingAnalysis *a) {
    char *p = buf;
    char *end = buf + bufsize - 512;

    /* Header bar */
    p += snprintf(p, (size_t)(end-p),
        "q\n0.13 0.13 0.17 rg\n0 692 612 100 re f\nQ\n"
        "BT /F2 22 Tf 1 0.6 0.2 rg 50 740 Td "
        "(AI BROWSING ANALYSIS) Tj ET\n"
        "BT /F1 10 Tf 0.8 0.8 0.85 rg 50 718 Td "
        "(Powered by pattern recognition engine) Tj ET\n");

    /* Insight cards */
    p += snprintf(p, (size_t)(end-p),
        "BT /F2 13 Tf 0.15 0.15 0.15 rg 50 660 Td "
        "(Key Insights) Tj ET\n"
        "q 0.97 0.95 0.92 rg 45 580 522 75 re f Q\n"
        "BT /F1 9 Tf 0.2 0.2 0.2 rg 55 640 Td (%s) Tj "
        "0 -16 Td (%s) Tj "
        "0 -16 Td (%s) Tj "
        "0 -16 Td (%s) Tj ET\n",
        a->insight_primary, a->insight_focus,
        a->insight_habit, a->insight_recommendation);

    /* Productivity score gauge */
    p += snprintf(p, (size_t)(end-p),
        "BT /F2 13 Tf 0.15 0.15 0.15 rg 50 560 Td "
        "(Productivity Score) Tj ET\n"
        /* Background bar */
        "q 0.9 0.9 0.9 rg 55 538 400 16 re f Q\n");

    /* Score fill bar - color based on score */
    float fill = 400.0f * ((float)a->productivity_score / 100.0f);
    if (a->productivity_score >= 70)
        p += snprintf(p, (size_t)(end-p),
            "q 0.2 0.8 0.4 rg 55 538 %.0f 16 re f Q\n", fill);
    else if (a->productivity_score >= 40)
        p += snprintf(p, (size_t)(end-p),
            "q 1.0 0.7 0.2 rg 55 538 %.0f 16 re f Q\n", fill);
    else
        p += snprintf(p, (size_t)(end-p),
            "q 0.9 0.3 0.3 rg 55 538 %.0f 16 re f Q\n", fill);

    p += snprintf(p, (size_t)(end-p),
        "BT /F2 11 Tf 0.15 0.15 0.15 rg 465 540 Td "
        "(%d / 100) Tj ET\n", a->productivity_score);

    /* Category breakdown */
    p += snprintf(p, (size_t)(end-p),
        "BT /F2 13 Tf 0.15 0.15 0.15 rg 50 505 Td "
        "(Category Breakdown) Tj ET\n"
        /* Column headers */
        "q 0.92 0.92 0.94 rg 45 486 522 16 re f Q\n"
        "BT /F2 8 Tf 0.15 0.15 0.15 rg "
        "55 489 Td (Category) Tj "
        "200 0 Td (URLs) Tj "
        "60 0 Td (Visits) Tj "
        "60 0 Td (Top Site) Tj ET\n");

    float cy = 472.0f;
    for (int c = 0; c < CAT_COUNT && cy > 200.0f; c++) {
        if (a->cats[c].count == 0) continue;

        /* Alternating rows */
        if ((c % 2) == 0) {
            p += snprintf(p, (size_t)(end-p),
                "q 0.97 0.97 0.98 rg 45 %.0f 522 14 re f Q\n", cy-3);
        }

        /* Color dot for category */
        p += snprintf(p, (size_t)(end-p),
            "q %s rg 55 %.0f 6 6 re f Q\n",
            category_color((Category)c), cy);

        char top_t[64] = "";
        if (a->cats[c].top_title[0]) {
            size_t tl = strlen(a->cats[c].top_title);
            if (tl > 25) { memcpy(top_t, a->cats[c].top_title, 22); strcpy(top_t+22, "..."); }
            else strncpy(top_t, a->cats[c].top_title, sizeof(top_t)-1);
        }
        /* Escape parens */
        char top_esc[128];
        pdf_escape_string(top_esc, sizeof(top_esc), top_t);

        p += snprintf(p, (size_t)(end-p),
            "BT /F1 8 Tf 0.2 0.2 0.2 rg "
            "65 %.0f Td (%s) Tj "
            "145 0 Td (%d) Tj "
            "60 0 Td (%d) Tj "
            "60 0 Td (%s) Tj ET\n",
            cy, category_name((Category)c),
            a->cats[c].count, a->cats[c].visits, top_esc);
        cy -= 16.0f;
    }

    /* Hourly activity heatmap */
    p += snprintf(p, (size_t)(end-p),
        "BT /F2 13 Tf 0.15 0.15 0.15 rg 50 %.0f Td "
        "(Hourly Activity Heatmap) Tj ET\n", cy - 20);
    cy -= 40.0f;

    /* Find max for scaling */
    int max_hv = 1;
    for (int h = 0; h < 24; h++)
        if (a->hourly_visits[h] > max_hv) max_hv = a->hourly_visits[h];

    /* Draw 24 bars */
    float bw = 20.0f;
    for (int h = 0; h < 24; h++) {
        float x = 45.0f + (float)h * bw;
        float ratio = (float)a->hourly_visits[h] / (float)max_hv;
        float bh = ratio * 60.0f;
        if (bh < 2.0f && a->hourly_visits[h] > 0) bh = 2.0f;

        /* Color intensity */
        float r = 1.0f - ratio * 0.8f;
        float g = 0.6f - ratio * 0.2f;
        float b = 0.2f;
        p += snprintf(p, (size_t)(end-p),
            "q %.2f %.2f %.2f rg %.0f %.0f %.0f %.0f re f Q\n",
            r, g, b, x, cy, bw - 2, bh);

        /* Hour label */
        if (h % 3 == 0) {
            p += snprintf(p, (size_t)(end-p),
                "BT /F3 6 Tf 0.4 0.4 0.4 rg %.0f %.0f Td (%02d) Tj ET\n",
                x + 2, cy - 10, h);
        }
    }

    /* Footer */
    p += snprintf(p, (size_t)(end-p),
        "q 0.13 0.13 0.17 rg 0 0 612 30 re f Q\n"
        "BT /F1 8 Tf 0.5 0.5 0.55 rg 50 10 Td "
        "(Brave History Access \\227 AI Analysis) Tj ET\n");
}

/* ── Analyzed PDF Export ─────────────────────────────────────────── */

int export_to_pdf_analyzed(const HistoryResult *result,
                            const HistoryStats *stats,
                            const BrowsingAnalysis *analysis,
                            const char *filepath) {
    PdfWriter w;
    memset(&w, 0, sizeof(w));

    w.f = fopen(filepath, "wb");
    if (!w.f) {
        fprintf(stderr, "[export_pdf] Cannot open %s for writing\n", filepath);
        return -1;
    }

    char timestamp[64];
    platform_timestamp_iso8601(timestamp, sizeof(timestamp));

    fprintf(w.f, "%%PDF-1.4\n");
    fprintf(w.f, "%%%c%c%c%c\n", 0xC0, 0xC1, 0xC2, 0xC3);

    w.catalog_obj_id = pdf_reserve_obj(&w);
    w.pages_obj_id   = pdf_reserve_obj(&w);

    pdf_begin_obj_id(&w, w.catalog_obj_id);
    fprintf(w.f, "<< /Type /Catalog /Pages %d 0 R >>\n", w.pages_obj_id);
    pdf_end_obj(&w);

    w.font_helv_id = pdf_begin_obj(&w);
    fprintf(w.f, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>\n");
    pdf_end_obj(&w);

    w.font_helv_bold_id = pdf_begin_obj(&w);
    fprintf(w.f, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>\n");
    pdf_end_obj(&w);

    w.font_courier_id = pdf_begin_obj(&w);
    fprintf(w.f, "<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>\n");
    pdf_end_obj(&w);

    w.resources_id = pdf_begin_obj(&w);
    fprintf(w.f, "<< /Font << /F1 %d 0 R /F2 %d 0 R /F3 %d 0 R >> >>\n",
        w.font_helv_id, w.font_helv_bold_id, w.font_courier_id);
    pdf_end_obj(&w);

    size_t content_bufsize = 128 * 1024;
    char *content_buf = malloc(content_bufsize);
    if (!content_buf) { fclose(w.f); return -1; }

    /* Page 1: Cover */
    build_cover_page(content_buf, content_bufsize, stats, timestamp);
    w.page_obj_ids[w.page_count++] = write_page(&w, content_buf, w.resources_id);

    /* Page 2: AI Analysis */
    build_analysis_page(content_buf, content_bufsize, analysis);
    w.page_obj_ids[w.page_count++] = write_page(&w, content_buf, w.resources_id);

    /* Remaining: History listing */
    int idx = 0, page_num = 3;
    while (idx < result->count && w.page_count < MAX_PAGES) {
        int written = build_listing_page(content_buf, content_bufsize,
                                          result, idx, page_num);
        if (written == 0) break;
        w.page_obj_ids[w.page_count++] = write_page(&w, content_buf, w.resources_id);
        idx += written;
        page_num++;
    }

    free(content_buf);

    /* Pages object LAST */
    pdf_begin_obj_id(&w, w.pages_obj_id);
    fprintf(w.f, "<< /Type /Pages /Kids [");
    for (int i = 0; i < w.page_count; i++)
        fprintf(w.f, "%d 0 R ", w.page_obj_ids[i]);
    fprintf(w.f, "] /Count %d >>\n", w.page_count);
    pdf_end_obj(&w);

    long xref_offset = ftell(w.f);
    fprintf(w.f, "xref\n0 %d\n", w.obj_count + 1);
    fprintf(w.f, "0000000000 65535 f \n");
    for (int i = 1; i <= w.obj_count; i++)
        fprintf(w.f, "%010ld 00000 n \n", w.offsets[i]);

    fprintf(w.f, "trailer\n<< /Size %d /Root %d 0 R >>\n",
        w.obj_count + 1, w.catalog_obj_id);
    fprintf(w.f, "startxref\n%ld\n%%%%EOF\n", xref_offset);

    fclose(w.f);
    printf("[export_pdf] Generated %d-page PDF with AI analysis: %s\n",
           w.page_count, filepath);
    return 0;
}
