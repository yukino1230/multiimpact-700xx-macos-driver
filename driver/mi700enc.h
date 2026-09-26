/* 201PL の組み立て (CUPS フィルタと PAPPL のアプリで共用)
 *
 * 1bit のページ画像を受け取り、MultiImpact 700XX 用の制御コードを出力する。
 * 出力先は呼び出し側のコールバックに渡すので、標準出力でもネットワークでもよい。
 */
#ifndef MI700ENC_H
#define MI700ENC_H

#include <stddef.h>

typedef void (*mi700_write_cb)(void *ctx, const void *data, size_t len);

typedef struct {
    /* 呼び出し側が設定する */
    const char     *source;     /* feeder / guide / rear / front */
    const char     *eject;      /* front / rear (カット紙のみ) */
    const char     *quality;    /* std-bi / std-uni / draft-bi / draft-uni / none */
    const char     *kanji;      /* 1978 / 1983 / 1990 */
    int             bottom;     /* ボトム領域の行数 (連続紙のみ) */
    mi700_write_cb  write;      /* 出力コールバック */
    void           *ctx;

    /* mi700_begin が設定する */
    int             is_cut;     /* カット紙(シートフィーダ/シートガイド) */
    int             is_feeder;  /* シートフィーダ(自動吸入) */
    int             pages;      /* これまでに出力したページ数 */
    int             lines;      /* 直前に送った用紙長 (記録用) */
} mi700_job_t;

/* 値を正規化し、is_cut / is_feeder を決める。ジョブの最初に1回呼ぶ */
void mi700_begin(mi700_job_t *j);

/* ページの先頭。1ページ目は初期化列、2ページ目以降は改ページを出す。
 *   page_dots   ページの高さ(ドット)
 *   vdpi        縦の解像度
 *   page_pt     ページの高さ(ポイント)。0 ならドット数から求める
 * 用紙長は**用紙の長さ**から計算する(ラスタの高さではない) */
void mi700_page_begin(mi700_job_t *j, unsigned page_dots, unsigned vdpi, unsigned page_pt);

/* ページの中身。bits は 1=黒 の 1bit 画像(invert が真なら 1=白) */
void mi700_page_bits(mi700_job_t *j, const unsigned char *bits,
                     unsigned w, unsigned h, unsigned stride, unsigned vdpi,
                     int invert, unsigned xoff, unsigned yoff);

/* ジョブの末尾(排出とパラメータリセット) */
void mi700_end(mi700_job_t *j);

#endif
