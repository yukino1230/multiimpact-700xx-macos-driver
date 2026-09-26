/* 201PL の組み立て (rastertomi700 から切り出したもの)
 *
 * 制御コードの出典 (201PL リファレンス / 700XX2 ユーザーズマニュアル):
 *   ESC J nnnn + data  24ドットビットイメージ。1列3バイト、バイト内は LSB が上端
 *   ESC F nnnn         横方向の絶対位置。1ドット = 1/160 インチ
 *   ESC T nn + LF      縦方向の紙送り。1単位 = 1/120 インチ、0..99
 *   ESC m 1/2/3        給紙口 (前トラクタ / カット紙 / 後トラクタ)
 *   FS 02ER / FS 02EF  カット紙の排出方向 (手前 / 奥)
 *   ESC > / ESC ]      片方向 / 両方向印字
 *   ESC v NN[,BB].     用紙長(行数, 1行=1/6インチ) とボトム領域
 *   FS 05F 2-nn        漢字コード表 (00=1978 / 01=1983 / 02=1990)
 *   ESC e 11           文字拡大を1倍に戻す(拡大中はドット間隔も拡大されるため)
 *   ESC c 8            パラメータリセット
 */
#include "mi700enc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define BAND       24      /* 24ピン */
#define TDPI      120      /* ESC T の単位 1/120 inch */
#define LINE_INCH   6      /* 簡易VFU の1行 = 1/6 inch */

static void put(mi700_job_t *j, const char *s) { j->write(j->ctx, s, strlen(s)); }
static void putb(mi700_job_t *j, int c) { unsigned char b = (unsigned char)c; j->write(j->ctx, &b, 1); }

static void putf(mi700_job_t *j, const char *fmt, ...) {
    char buf[64];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) j->write(j->ctx, buf, (size_t)n);
}

/* ESC T は2桁なので 99 単位ずつ送る。実際に送った量を返す */
static int feedto(mi700_job_t *j, int units) {
    int done = 0;
    while (units > 0) {
        int n = units > 99 ? 99 : units;
        putf(j, "\033T%02d\n", n);
        units -= n; done += n;
    }
    return done;
}

void mi700_begin(mi700_job_t *j) {
    if (!j->source)  j->source  = "feeder";
    if (!j->eject)   j->eject   = "front";
    if (!j->quality) j->quality = "std-bi";
    if (!j->kanji)   j->kanji   = "1990";
    /* 値の名前の正規化。reartractor/fronttractor は IPP 標準キーワード(rear)と
       衝突して macOS に訳語を差し替えられるのを避けるための名前 */
    if (!strcmp(j->source, "cut"))          j->source = "feeder";  /* 旧オプション値 */
    if (!strcmp(j->source, "reartractor"))  j->source = "rear";
    if (!strcmp(j->source, "fronttractor")) j->source = "front";
    if (strcmp(j->source, "feeder") && strcmp(j->source, "guide") &&
        strcmp(j->source, "rear")   && strcmp(j->source, "front")) j->source = "feeder";
    if (strcmp(j->eject, "front") && strcmp(j->eject, "rear")) j->eject = "front";
    j->is_feeder = !strcmp(j->source, "feeder");
    j->is_cut    = j->is_feeder || !strcmp(j->source, "guide");
    j->pages     = 0;
    j->lines     = 0;
}

void mi700_page_begin(mi700_job_t *j, unsigned page_dots, unsigned vdpi, unsigned page_pt) {
    if (j->pages > 0) {
        /* 次ページ: フィーダは排出して次を吸入、手差しは排出のみ、連続紙は改ページ */
        put(j, j->is_feeder ? "\033a" : j->is_cut ? "\033b" : "\014");
        j->pages ++;
        return;
    }

    /* 順序と同期点は Windows ドライバのキャプチャに合わせる。
       ESC m は用紙の退避などの機械的動作を伴うため、直後に EM で同期しないと
       後続の設定が動作中に届き、給紙後に停止する(SEL等が点滅する)。
         品質 → 漢字表 → 拡大解除 → 給紙口 → EM → 排出方向 → 用紙長 → CR
         → 吸入(ESC a) → EM → CR                                            */
    /* メモリスイッチで初期値が変わる項目は必ず明示する。
       「工場設定と同じだから省く」は誤り(初期状態表の【】は工場設定であって
       この機体の設定ではない)。特に:
         ESC M    ネイティブモード。コピーモードだと ESC T の単位が1/120→1/160になる
         ESC /136 ライトマージン。080 のままだと 1280ドットを超えた時点で
                  「印刷範囲を超えて印字」エラーになり、点滅して停止する
         ESC L000 レフトマージン
         ESC f    順方向改行 */
    /* 前のジョブが異常終了して残った印刷データを捨てる。Windows ドライバも
       初期化の先頭で送っている。ESC c8 は末尾で送っているので正常終了なら
       状態は初期化済みだが、中断された場合の保険 */
    putb(j, 0x18);                                    /* CAN */
    put(j, "\033M");
    put(j, "\033/136");
    put(j, "\033L000");
    put(j, "\033f");
    if (strcmp(j->quality, "none")) {
        put(j, strncmp(j->quality, "draft", 5) ? "\033d1" : "\033d0");
        put(j, strstr(j->quality, "uni") ? "\033>" : "\033]");
    }
    putf(j, "\034" "05F2-%s", !strcmp(j->kanji, "1978") ? "00"
                            : !strcmp(j->kanji, "1983") ? "01" : "02");
    put(j, "\033e11");
    put(j, "\033\"");                                 /* 強調印刷モード解除 */
    put(j, "\033Y");                                  /* ライン印刷モード解除 */
    putf(j, "\033m%c", !strcmp(j->source, "front") ? '1' : j->is_cut ? '2' : '3');
    putb(j, 0x19);                                    /* EM: 給紙口切替の完了を待つ */
    if (j->is_cut) {
        put(j, !strcmp(j->eject, "rear") ? "\034" "02EF" : "\034" "02ER");
        /* カット紙の用紙長 FS 05v。単位 1/120 インチ + 300(2.5インチ)。
           送らないと本体の工場設定(66行=11インチ)で判断され停止する。
           どちらのマニュアルにも無いが、Windows のキャプチャ2つで裏が取れている:
             連続紙  540 = 4.5インチ ちょうど(オフセットなし)
             カット紙 1703 = 11.69インチ(A4) + 2.5インチ
           **用紙の長さ**であってラスタの高さではない。ラスタは印字可能範囲ぶんしか
           無いので、それを使うと上下の余白のぶん(A4 で16mm)短くなる */
        double plen = page_pt ? (double)page_pt / 72.0 * 120.0
                              : (double)page_dots / vdpi * 120.0;
        int v = (int)(plen + 0.5) + 300;
        if (v >= 1 && v <= 9999) putf(j, "\034" "05v%04d", v);
        j->lines = v;
    } else {                                          /* 連続紙は簡易VFU(行数) */
        /* カット紙と同じく**用紙の長さ**から出す。ミシン目回避の用紙は
           上下25.4mmずつ印字不可なので、ラスタの高さだと2インチ短くなり
           改ページのたびに2インチずつずれていく */
        double plen = page_pt ? (double)page_pt / 72.0 : (double)page_dots / vdpi;
        int lines = (int)(plen * LINE_INCH + 0.5);
        if (lines >= 1 && lines <= 99) {
            if (j->bottom > 0 && j->bottom <= lines - 2) putf(j, "\033v%02d,%02d.", lines, j->bottom);
            else                                         putf(j, "\033v%02d.", lines);
        } else {
            lines = 0;
        }
        j->lines = lines;
    }
    putb(j, '\r');
    if (j->is_feeder) put(j, "\033a");                /* 全排出後全吸入 */
    putb(j, 0x19);                                    /* EM: 給紙の完了を待つ */
    putb(j, '\r');
    j->pages ++;
}

void mi700_page_bits(mi700_job_t *j, const unsigned char *bits,
                     unsigned w, unsigned h, unsigned stride, unsigned vdpi,
                     int invert, unsigned xoff, unsigned yoff) {
    unsigned char *cols = malloc((size_t)w * 3);
    int at = 0;
    if (!cols) return;
    for (unsigned top = 0; top < h; top += BAND) {
        long first = -1, last = -1;
        for (unsigned x = 0; x < w; x++) {
            unsigned b0 = 0, b1 = 0, b2 = 0;
            unsigned byte = x >> 3, mask = 0x80u >> (x & 7);
            for (unsigned k = 0; k < BAND; k++) {
                unsigned y = top + k;
                int bit;
                if (y >= h) continue;
                bit = (bits[(size_t)y * stride + byte] & mask) != 0;
                if (invert) bit = !bit;
                if (!bit) continue;
                if      (k < 8)  b0 |= 1u << k;          /* バイト内は LSB が上端 */
                else if (k < 16) b1 |= 1u << (k - 8);
                else             b2 |= 1u << (k - 16);
            }
            cols[x*3] = (unsigned char)b0; cols[x*3+1] = (unsigned char)b1; cols[x*3+2] = (unsigned char)b2;
            if (b0 | b1 | b2) { if (first < 0) first = x; last = x; }
        }
        if (first < 0) continue;                          /* 空白バンドは送らない */
        {   /* 縦位置は絶対値で管理する(相対送りを積むと丸め誤差が溜まる) */
            double want = (double)(top + yoff) * TDPI / (double)vdpi;
            at += feedto(j, (int)(want + 0.5) - at);
        }
        put(j, "\033H\033e11");
        putf(j, "\033F%04ld", first + (long)xoff);
        put(j, "\033H\033e11");
        putf(j, "\033J%04ld", last - first + 1);
        j->write(j->ctx, cols + first*3, (size_t)(last - first + 1) * 3);
        putb(j, '\r');
    }
    free(cols);
}

void mi700_end(mi700_job_t *j) {
    put(j, j->is_cut ? "\r\033b\033c8" : "\r\014\033c8");
}
