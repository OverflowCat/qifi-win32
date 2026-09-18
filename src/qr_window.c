/*
 * qr_window.c — QR fountain display window
 *
 * Reads a file, adds metadata, compresses with zlib, encodes with Luby Transform,
 * and renders a stream of QR codes in a Win32 window at the configured FPS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include <zlib.h>
#include "qrcodegen.h"
#include "qr_window.h"

/* ========================================================================
 *  CRC32 — matches the TypeScript implementation in checksum.ts
 * ======================================================================== */

static uint32_t g_crc32_table[256];

static void init_crc32(void) {
    for (int i = 0; i < 256; i++) {
        uint32_t crc = (uint32_t)i;
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        g_crc32_table[i] = crc;
    }
}

static uint32_t lt_checksum(const uint8_t *data, size_t len, int k) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        crc = (crc >> 8) ^ g_crc32_table[(crc ^ data[i]) & 0xFF];
    return (crc ^ (uint32_t)k ^ 0xFFFFFFFF);
}

/* ========================================================================
 *  Base64
 * ======================================================================== */

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_encode(const uint8_t *src, int len, char *dst) {
    int o = 0;
    for (int i = 0; i < len; i += 3) {
        int val = src[i] << 16;
        if (i + 1 < len) val |= src[i + 1] << 8;
        if (i + 2 < len) val |= src[i + 2];
        dst[o++] = B64[(val >> 18) & 0x3F];
        dst[o++] = B64[(val >> 12) & 0x3F];
        dst[o++] = (i + 1 < len) ? B64[(val >> 6) & 0x3F] : '=';
        dst[o++] = (i + 2 < len) ? B64[val & 0x3F] : '=';
    }
    dst[o] = '\0';
    return o;
}

/* ========================================================================
 *  MIME type lookup (simple extension table)
 * ======================================================================== */

typedef struct { const char *ext; const char *mime; } MimeEntry;
static const MimeEntry MIME_TABLE[] = {
    {"txt","text/plain"}, {"html","text/html"}, {"htm","text/html"},
    {"css","text/css"}, {"js","application/javascript"}, {"json","application/json"},
    {"xml","application/xml"}, {"csv","text/csv"}, {"md","text/markdown"},
    {"jpg","image/jpeg"}, {"jpeg","image/jpeg"}, {"png","image/png"},
    {"gif","image/gif"}, {"bmp","image/bmp"}, {"svg","image/svg+xml"},
    {"webp","image/webp"}, {"ico","image/x-icon"}, {"tif","image/tiff"}, {"tiff","image/tiff"},
    {"mp3","audio/mpeg"}, {"wav","audio/wav"}, {"ogg","audio/ogg"}, {"flac","audio/flac"},
    {"aac","audio/aac"}, {"wma","audio/x-ms-wma"},
    {"mp4","video/mp4"}, {"avi","video/x-msvideo"}, {"mkv","video/x-matroska"},
    {"webm","video/webm"}, {"mov","video/quicktime"}, {"wmv","video/x-ms-wmv"},
    {"pdf","application/pdf"}, {"zip","application/zip"}, {"gz","application/gzip"},
    {"tar","application/x-tar"}, {"7z","application/x-7z-compressed"},
    {"rar","application/vnd.rar"},
    {"doc","application/msword"}, {"docx","application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {"xls","application/vnd.ms-excel"}, {"xlsx","application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {"ppt","application/vnd.ms-powerpoint"}, {"pptx","application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {"c","text/x-c"}, {"h","text/x-c"}, {"cpp","text/x-c++"}, {"hpp","text/x-c++"},
    {"py","text/x-python"}, {"java","text/x-java-source"}, {"rs","text/x-rust"},
    {"go","text/x-go"}, {"rb","text/x-ruby"}, {"sh","application/x-sh"},
    {"yaml","application/x-yaml"}, {"yml","application/x-yaml"}, {"toml","application/toml"},
    {"exe","application/octet-stream"}, {"dll","application/octet-stream"},
    {"so","application/octet-stream"}, {"bin","application/octet-stream"},
    {NULL, NULL}
};

static const char *guess_mime(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || !dot[1]) return "application/octet-stream";
    dot++;
    char lower[32];
    int i;
    for (i = 0; dot[i] && i < 31; i++)
        lower[i] = (dot[i] >= 'A' && dot[i] <= 'Z') ? dot[i] + 32 : dot[i];
    lower[i] = '\0';
    for (const MimeEntry *e = MIME_TABLE; e->ext; e++)
        if (strcmp(lower, e->ext) == 0) return e->mime;
    return "application/octet-stream";
}

/* ========================================================================
 *  Luby Transform Encoder — port of packages/luby-transform/src/encoder.ts
 * ======================================================================== */

#define LT_MAX_K 4096

typedef struct {
    int      k;
    int      slice_size;
    int      byte_count;      /* original compressed data length */
    uint32_t checksum;
    uint8_t *slices[LT_MAX_K];
    double   cumul[LT_MAX_K]; /* cumulative probabilities */
} LtEncoder;

static void lt_init(LtEncoder *enc, const uint8_t *data, int dataLen, int sliceSize) {
    enc->slice_size  = sliceSize;
    enc->byte_count  = dataLen;
    enc->k           = (dataLen + sliceSize - 1) / sliceSize;
    enc->checksum    = lt_checksum(data, dataLen, enc->k);

    for (int i = 0; i < enc->k; i++) {
        enc->slices[i] = (uint8_t *)calloc(sliceSize, 1);
        int off = i * sliceSize;
        int n   = (off + sliceSize <= dataLen) ? sliceSize : (dataLen - off);
        if (n > 0) memcpy(enc->slices[i], data + off, n);
    }

    /* Ideal Soliton Distribution — same as encoder.ts */
    double prob;
    double cumul = 0.0;
    for (int d = 1; d <= enc->k; d++) {
        prob = (d == 1) ? 1.0 / enc->k : 1.0 / (d * (d - 1));
        cumul += prob;
        enc->cumul[d - 1] = cumul;
    }
    /* Normalize — avoid floating-point drift */
    for (int i = 0; i < enc->k; i++)
        enc->cumul[i] /= cumul;
}

static void lt_free(LtEncoder *enc) {
    for (int i = 0; i < enc->k; i++)
        free(enc->slices[i]);
}

static int lt_pick_degree(const LtEncoder *enc) {
    double r = (double)rand() / (double)RAND_MAX;
    for (int i = 0; i < enc->k; i++)
        if (r < enc->cumul[i]) return i + 1;
    return enc->k;
}

static void lt_pick_indices(int k, int degree, int *out) {
    /* Fisher-Yates partial shuffle */
    int pool[LT_MAX_K];
    for (int i = 0; i < k; i++) pool[i] = i;
    for (int i = 0; i < degree; i++) {
        int j   = i + rand() % (k - i);
        int tmp = pool[i]; pool[i] = pool[j]; pool[j] = tmp;
        out[i]  = pool[i];
    }
}

/* Serialize to binary — same layout as shared.ts blockToBinary() */
static int lt_serialize(const LtEncoder *enc, const uint8_t *data,
                        const int *indices, int numIdx, uint8_t *out) {
    int pos = 0;
    uint32_t u;

    /* numIdx */
    u = (uint32_t)numIdx;
    memcpy(out + pos, &u, 4); pos += 4;
    /* indices */
    for (int i = 0; i < numIdx; i++) {
        u = (uint32_t)indices[i];
        memcpy(out + pos, &u, 4); pos += 4;
    }
    /* k, bytes, checksum */
    u = (uint32_t)enc->k;          memcpy(out + pos, &u, 4); pos += 4;
    u = (uint32_t)enc->byte_count; memcpy(out + pos, &u, 4); pos += 4;
    u = enc->checksum;             memcpy(out + pos, &u, 4); pos += 4;
    /* data */
    memcpy(out + pos, data, enc->slice_size); pos += enc->slice_size;

    return pos;
}

/* ========================================================================
 *  File metadata — matches binary-meta.ts mergeUint8Arrays / appendMeta
 *
 *  Wire format: [len0:u32be][chunk0][len1:u32be][chunk1] ...
 * ======================================================================== */

static int encode_meta(const char *filename, const char *contentType,
                       const uint8_t *fileData, int fileLen, uint8_t **out) {
    char metaJson[1024];
    int metaLen = sprintf(metaJson,
        "{\"filename\":\"%s\",\"contentType\":\"%s\"}", filename, contentType);

    /* Two chunks: meta JSON + file data */
    int totalLen = 4 + metaLen + 4 + fileLen;
    uint8_t *buf = (uint8_t *)malloc(totalLen);
    int pos = 0;

    /* Chunk 0: meta */
    buf[pos++] = (uint8_t)((metaLen >> 24) & 0xFF);
    buf[pos++] = (uint8_t)((metaLen >> 16) & 0xFF);
    buf[pos++] = (uint8_t)((metaLen >> 8)  & 0xFF);
    buf[pos++] = (uint8_t)( metaLen        & 0xFF);
    memcpy(buf + pos, metaJson, metaLen); pos += metaLen;

    /* Chunk 1: file data */
    buf[pos++] = (uint8_t)((fileLen >> 24) & 0xFF);
    buf[pos++] = (uint8_t)((fileLen >> 16) & 0xFF);
    buf[pos++] = (uint8_t)((fileLen >> 8)  & 0xFF);
    buf[pos++] = (uint8_t)( fileLen        & 0xFF);
    memcpy(buf + pos, fileData, fileLen); pos += fileLen;

    *out = buf;
    return pos;
}

/* ========================================================================
 *  Globals shared between WndProc and main logic
 * ======================================================================== */

static LtEncoder    g_enc;
static int          g_frame = 0;
static char        *g_filename;
static const char  *g_contentType;
static const char  *g_prefix;
static int          g_fps;
static enum qrcodegen_Ecc g_ecl;

/* ========================================================================
 *  Generate one frame's worth of QR text (base64 of one fountain block)
 * ======================================================================== */

static void generate_qr_text(char *dst, int dstCap) {
    uint8_t blockData[LT_MAX_K];    /* max slice_size */
    int     indices[LT_MAX_K];
    int     numIdx;
    uint8_t binary[4 + LT_MAX_K * 4 + 4 + 4 + 4 + LT_MAX_K]; /* serialized */

    int degree = lt_pick_degree(&g_enc);
    lt_pick_indices(g_enc.k, degree, indices);
    numIdx = degree;

    memset(blockData, 0, g_enc.slice_size);
    for (int i = 0; i < degree; i++)
        for (int j = 0; j < g_enc.slice_size; j++)
            blockData[j] ^= g_enc.slices[indices[i]][j];

    int binLen = lt_serialize(&g_enc, blockData, indices, numIdx, binary);

    /* Prefix + base64 */
    int off = 0;
    if (g_prefix && g_prefix[0]) {
        int plen = (int)strlen(g_prefix);
        memcpy(dst, g_prefix, plen);
        off = plen;
    }
    base64_encode(binary, binLen, dst + off);
}

/* ========================================================================
 *  WndProc — renders QR codes at timer interval
 * ======================================================================== */

#define TIMER_ID 1

static LRESULT CALLBACK qrWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int cx = (rc.right - rc.left) / 2;

        /* Background */
        FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));

        /* Generate and encode */
        char qrtext[4096];
        generate_qr_text(qrtext, sizeof(qrtext));

        uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
        uint8_t tempbuf[qrcodegen_BUFFER_LEN_MAX];
        bool ok = qrcodegen_encodeText(qrtext, tempbuf, qrcode,
                                        g_ecl,
                                        qrcodegen_VERSION_MIN,
                                        qrcodegen_VERSION_MAX,
                                        qrcodegen_Mask_AUTO,
                                        false);

        if (ok) {
            int sz    = qrcodegen_getSize(qrcode);
            int scale = (QR_AREA - 40) / (sz + 2);   /* +2 quiet zone */
            if (scale < 1) scale = 1;
            int qrPix = sz * scale;
            int x0    = cx - qrPix / 2;
            int y0    = 15;

            /* White background for quiet zone */
            RECT bg = {x0 - scale * 2, y0 - scale * 2,
                       x0 + qrPix + scale * 2, y0 + qrPix + scale * 2};
            HBRUSH hW = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(hdc, &bg, hW);
            DeleteObject(hW);

            /* Black modules */
            HBRUSH hB = CreateSolidBrush(RGB(0, 0, 0));
            for (int y = 0; y < sz; y++)
                for (int x = 0; x < sz; x++)
                    if (qrcodegen_getModule(qrcode, x, y)) {
                        RECT m = {x0 + x * scale, y0 + y * scale,
                                  x0 + (x + 1) * scale, y0 + (y + 1) * scale};
                        FillRect(hdc, &m, hB);
                    }
            DeleteObject(hB);

            /* QR version label */
            char verlbl[32];
            sprintf(verlbl, "v%d  %dx%d", (sz - 17) / 4, sz, sz);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(100, 100, 100));
            RECT vrc = {x0, y0 + qrPix + 4, x0 + qrPix, y0 + qrPix + 20};
            DrawTextA(hdc, verlbl, -1, &vrc, DT_CENTER | DT_SINGLELINE);
        }

        /* Status bar at bottom */
        int textY = QR_AREA + 30;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));

        char line1[512], line2[256];
        sprintf(line1, "%s  (%s)", g_filename, g_contentType);
        sprintf(line2, "Frame: %d | Slice: %d | Blocks: %d | %d FPS",
                g_frame, g_enc.slice_size, g_enc.k, g_fps);

        RECT tr1 = {10, textY, rc.right - 10, textY + 18};
        DrawTextA(hdc, line1, -1, &tr1, DT_LEFT | DT_SINGLELINE);
        RECT tr2 = {10, textY + 20, rc.right - 10, textY + 38};
        DrawTextA(hdc, line2, -1, &tr2, DT_LEFT | DT_SINGLELINE);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        g_frame++;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        lt_free(&g_enc);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

/* ========================================================================
 *  Public entry — runs the QR window (blocks until closed)
 * ======================================================================== */

bool qr_window_run(const char *filepath, const QifiSettings *settings) {
    init_crc32();
    srand((unsigned)time(NULL));

    g_fps   = settings->fps;
    g_prefix = settings->prefix;
    g_ecl   = (enum qrcodegen_Ecc)settings->ecc;

    /* ---- Read file ---- */
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        char msg[512];
        sprintf(msg, "Cannot open file:\n%s", filepath);
        MessageBoxA(NULL, msg, "Qifi Error", MB_ICONERROR);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long fileLen = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *fileData = (uint8_t *)malloc(fileLen);
    fread(fileData, 1, fileLen, f);
    fclose(f);

    /* Extract basename */
    const char *base = strrchr(filepath, '/');
    if (!base) base = strrchr(filepath, '\\');
    base = base ? base + 1 : filepath;
    g_filename    = _strdup(base);
    g_contentType = guess_mime(filepath);

    printf("File: %s (%s, %ld bytes)\n", g_filename, g_contentType, fileLen);

    /* ---- Encode metadata ---- */
    uint8_t *merged;
    int mergedLen = encode_meta(g_filename, g_contentType,
                                fileData, (int)fileLen, &merged);
    free(fileData);

    /* ---- Compress with zlib ---- */
    uLongf compBound = compressBound(mergedLen);
    uint8_t *compressed = (uint8_t *)malloc(compBound);
    int zret = compress(compressed, &compBound, merged, mergedLen);
    free(merged);
    if (zret != Z_OK) {
        fprintf(stderr, "zlib compress failed: %d\n", zret);
        free(compressed);
        return false;
    }
    printf("Compressed: %d -> %lu bytes\n", mergedLen, (unsigned long)compBound);

    /* ---- Luby Transform encoder ---- */
    lt_init(&g_enc, compressed, (int)compBound, settings->slice_size);
    free(compressed);

    printf("LT encoder: k=%d, slice=%d, checksum=0x%08X\n",
           g_enc.k, g_enc.slice_size, g_enc.checksum);

    /* ---- Win32 window ---- */
    const char *CLS = "QifiQRWindow";
    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = qrWndProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLS;
    RegisterClassExA(&wc);

    RECT wr = {0, 0, WINDOW_W, WINDOW_H};
    DWORD style = (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_VISIBLE;
    AdjustWindowRect(&wr, style, FALSE);

    char title[256];
    sprintf(title, "Qifi - %s", g_filename);

    HWND hwnd = CreateWindowExA(0, CLS, title, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!hwnd) { fprintf(stderr, "CreateWindowEx failed\n"); return false; }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    SetTimer(hwnd, TIMER_ID, 1000 / g_fps, NULL);

    printf("Displaying at %d FPS. Close the window to exit.\n", g_fps);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    free(g_filename);
    g_filename = NULL;
    return true;
}
