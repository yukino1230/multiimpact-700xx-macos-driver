/* CUPS フィルタ: CUPS ラスタ -> NEC MultiImpact 700XX (201PL)
 *
 * PDF のラスタライズは macOS 標準の cgpdftoraster が行うため外部依存はない。
 * 依存を作らないため libcups は使わず、ラスタを自前で解析する。
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>

#define BAND       24      /* 24ピン */
#define TDPI      120      /* ESC T の単位 1/120 inch */
#define LINE_INCH   6      /* 簡易VFU の1行 = 1/6 inch */
#define HDRSIZE  1796      /* cups_page_header2_t */

static FILE *in;
static int   little, compressed;

static void logmsg(const char *lv, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "%s: [rastertomi700] ", lv);
    vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}

static unsigned rd32(const unsigned char *p) {
    return little ? (unsigned)p[0] | p[1]<<8 | p[2]<<16 | (unsigned)p[3]<<24
                  : (unsigned)p[3] | p[2]<<8 | p[1]<<16 | (unsigned)p[0]<<24;
}

static size_t readn(void *buf, size_t n) {
    size_t got = 0, r;
    while (got < n && (r = fread((char*)buf + got, 1, n - got, in)) > 0) got += r;
    return got;
}

/* CUPS ラスタ v2 の RLE 展開 (v3 は無圧縮なので通常は通らない) */
static int unrle(unsigned char *out, unsigned stride, unsigned height, unsigned bpp) {
    unsigned px = (bpp + 7) / 8; if (!px) px = 1;
    unsigned char *line = malloc(stride);
    unsigned y = 0;
    if (!line) return -1;
    while (y < height) {
        int c = fgetc(in);
        if (c == EOF) break;
        unsigned rep = (unsigned)c + 1, len = 0;
        while (len < stride) {
            int n = fgetc(in);
            if (n == EOF) break;
            if (n < 128) {
                unsigned char pix[8];
                if (readn(pix, px) != px) break;
                for (int i = 0; i <= n && len < stride; i++)
                    for (unsigned k = 0; k < px && len < stride; k++) line[len++] = pix[k];
            } else {
                unsigned cnt = (257 - (unsigned)n) * px;
                for (unsigned k = 0; k < cnt && len < stride; k++) {
                    int b = fgetc(in); if (b == EOF) break;
                    line[len++] = (unsigned char)b;
                }
            }
        }
        while (len < stride) line[len++] = 0;
        for (unsigned r = 0; r < rep && y < height; r++, y++)
            memcpy(out + (size_t)y * stride, line, stride);
    }
    free(line);
    return 0;
}

/* ESC T は2桁なので 99 単位ずつ送る。実際に送った量を返す */
static int feedto(int units) {
    int done = 0;
    while (units > 0) {
        int n = units > 99 ? 99 : units;
        printf("\033T%02d\n", n);
        units -= n; done += n;
    }
    return done;
}

static void encode_page(const unsigned char *ras, unsigned w, unsigned h,
                        unsigned stride, unsigned vdpi, int invert,
                        unsigned xoff, unsigned yoff) {
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
                bit = (ras[(size_t)y * stride + byte] & mask) != 0;
                if (invert) bit = !bit;
                if (!bit) continue;
                if      (k < 8)  b0 |= 1u << k;          /* バイト内は LSB が上端 */
                else if (k < 16) b1 |= 1u << (k - 8);
                else             b2 |= 1u << (k - 16);
            }
            cols[x*3] = b0; cols[x*3+1] = b1; cols[x*3+2] = b2;
            if (b0 | b1 | b2) { if (first < 0) first = x; last = x; }
        }
        if (first < 0) continue;                          /* 空白バンドは送らない */
        {   /* 縦位置は絶対値で管理する(相対送りを積むと丸め誤差が溜まる) */
            double want = (double)(top + yoff) * TDPI / (double)vdpi;
            at += feedto((int)(want + 0.5) - at);
        }
        printf("\033H\033e11");
        printf("\033F%04ld", first + (long)xoff);
        printf("\033H\033e11");
        printf("\033J%04ld", last - first + 1);
        fwrite(cols + first*3, 1, (size_t)(last - first + 1) * 3, stdout);
        fputc('\r', stdout);
    }
    free(cols);
}

/* ---- プリンタの状態を SNMP で読む -----------------------------------
 * この機種の LAN カード(NEC FastEthernet, enterprises.119.1.40)は
 * Printer-MIB(1.3.6.1.2.1.43) を実装していないので、給紙口の装備や
 * リボン残量は取れない。取れるのは Host Resources MIB の2つだけ:
 *   hrPrinterStatus.1            1.3.6.1.2.1.25.3.5.1.1.1
 *   hrPrinterDetectedErrorState.1 1.3.6.1.2.1.25.3.5.1.2.1  (ビットフラグ)
 * net-snmp に依存させないため SNMPv1 の GetRequest だけ手書きする。
 */
#define ERR_LOWPAPER 0x80
#define ERR_NOPAPER  0x40
#define ERR_LOWTONER 0x20
#define ERR_NOTONER  0x10
#define ERR_DOOROPEN 0x08
#define ERR_JAMMED   0x04
#define ERR_OFFLINE  0x02
#define ERR_SERVICE  0x01

static const unsigned char OID_ERRSTATE[] = {0x2b,6,1,2,1,25,3,5,1,2,1};
static const unsigned char OID_PRSTATUS[] = {0x2b,6,1,2,1,25,3,5,1,1,1};

/* BER: タグを1つ開いて中身の先頭に進む。*cend に中身の末尾を入れる */
static int ber_open(const unsigned char **p, const unsigned char *end,
                    int tag, const unsigned char **cend) {
    int n;
    if (*p >= end || **p != tag) return 0;
    (*p)++;
    if (*p >= end) return 0;
    n = *(*p)++;
    if (n & 0x80) {                      /* 長さが複数バイトの場合 */
        int k = n & 0x7f, v = 0;
        if (k < 1 || k > 3 || *p + k > end) return 0;
        while (k--) v = (v << 8) | *(*p)++;
        n = v;
    }
    if (n < 0 || *p + n > end) return 0;
    *cend = *p + n;
    return 1;
}
/* プリミティブを1つ読み飛ばす(値が要るときは val/len を受け取る) */
static int ber_take(const unsigned char **p, const unsigned char *end,
                    int tag, const unsigned char **val, int *len) {
    const unsigned char *ce;
    if (!ber_open(p, end, tag, &ce)) return 0;
    if (val) *val = *p;
    if (len) *len = (int)(ce - *p);
    *p = ce;
    return 1;
}

/* 1つの OID を GET する。成功したら値のタグを返し、val/len に中身を入れる */
static int snmp_get(const char *host, const unsigned char *oid, int oidlen,
                    unsigned char *val, int valmax, int *vallen) {
    const char *comm = getenv("MI700_SNMP_COMMUNITY");
    unsigned char msg[256], buf[1024], vb[64], pd[128];
    int clen, vn = 0, ln, pn = 0, mlen, total, got, tag = -1;
    struct addrinfo hints, *ai = NULL;
    struct timeval tv;
    int fd, try_;
    const unsigned char *p, *end, *ce, *v;
    int vlen;

    if (!comm || !*comm) comm = "public";
    clen = (int)strlen(comm);
    if (clen > 64) return -1;

    vb[vn++] = 0x30; vb[vn++] = (unsigned char)(2 + oidlen + 2);   /* VarBind */
    vb[vn++] = 0x06; vb[vn++] = (unsigned char)oidlen;
    memcpy(vb + vn, oid, (size_t)oidlen); vn += oidlen;
    vb[vn++] = 0x05; vb[vn++] = 0x00;                              /* NULL */
    ln = 2 + vn;                                                   /* VarBindList */

    pd[pn++] = 0xA0; pd[pn++] = (unsigned char)(9 + ln);           /* GetRequest */
    pd[pn++] = 0x02; pd[pn++] = 0x01; pd[pn++] = (unsigned char)(getpid() & 0x7f);
    pd[pn++] = 0x02; pd[pn++] = 0x01; pd[pn++] = 0x00;             /* error-status */
    pd[pn++] = 0x02; pd[pn++] = 0x01; pd[pn++] = 0x00;             /* error-index  */
    pd[pn++] = 0x30; pd[pn++] = (unsigned char)vn;
    memcpy(pd + pn, vb, (size_t)vn); pn += vn;

    mlen = 3 + (2 + clen) + pn;
    if (mlen > 127) return -1;                                     /* 長さ1バイトで足りる範囲だけ */
    total = 0;
    msg[total++] = 0x30; msg[total++] = (unsigned char)mlen;
    msg[total++] = 0x02; msg[total++] = 0x01; msg[total++] = 0x00; /* version = SNMPv1 */
    msg[total++] = 0x04; msg[total++] = (unsigned char)clen;
    memcpy(msg + total, comm, (size_t)clen); total += clen;
    memcpy(msg + total, pd, (size_t)pn); total += pn;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, "161", &hints, &ai) || !ai) return -1;
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) { freeaddrinfo(ai); return -1; }
    tv.tv_sec = 1; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    got = -1;
    for (try_ = 0; try_ < 2 && got < 0; try_++) {
        if (sendto(fd, msg, (size_t)total, 0, ai->ai_addr, ai->ai_addrlen) < 0) break;
        got = (int)recv(fd, buf, sizeof buf, 0);
    }
    close(fd); freeaddrinfo(ai);
    if (got <= 0) return -1;

    p = buf; end = buf + got;
    if (!ber_open(&p, end, 0x30, &ce)) return -1;   end = ce;
    if (!ber_take(&p, end, 0x02, NULL, NULL)) return -1;          /* version   */
    if (!ber_take(&p, end, 0x04, NULL, NULL)) return -1;          /* community */
    if (!ber_open(&p, end, 0xA2, &ce)) return -1;   end = ce;     /* GetResponse */
    if (!ber_take(&p, end, 0x02, NULL, NULL)) return -1;          /* request-id   */
    if (!ber_take(&p, end, 0x02, &v, &vlen)) return -1;           /* error-status */
    if (vlen != 1 || v[0] != 0) return -1;
    if (!ber_take(&p, end, 0x02, NULL, NULL)) return -1;          /* error-index  */
    if (!ber_open(&p, end, 0x30, &ce)) return -1;   end = ce;     /* VarBindList  */
    if (!ber_open(&p, end, 0x30, &ce)) return -1;   end = ce;     /* VarBind      */
    if (!ber_take(&p, end, 0x06, NULL, NULL)) return -1;          /* 名前(OID)    */
    if (p >= end) return -1;
    tag = *p;
    if (!ber_take(&p, end, tag, &v, &vlen)) return -1;
    if (vlen > valmax) vlen = valmax;
    memcpy(val, v, (size_t)vlen);
    *vallen = vlen;
    return tag;
}

/* キューの接続先ホストを調べる。DEVICE_URI はフィルタには渡らないことがあるので
   その場合は lpstat から取る */
static const char *printer_host(void) {
    static char host[256];
    const char *uri = getenv("DEVICE_URI");
    char line[512], cmd[300];
    const char *pr, *q;
    size_t i;

    if (!uri || !strstr(uri, "://")) {
        FILE *f;
        pr = getenv("PRINTER");
        if (!pr || !*pr) return NULL;
        for (q = pr; *q; q++)                    /* キュー名を検証してから渡す */
            if (!isalnum((unsigned char)*q) && *q != '_' && *q != '-' && *q != '.')
                return NULL;
        snprintf(cmd, sizeof cmd, "/usr/bin/lpstat -v %s 2>/dev/null", pr);
        f = popen(cmd, "r");
        if (!f) return NULL;
        uri = NULL;
        while (fgets(line, sizeof line, f)) {
            char *s2 = strstr(line, "://");
            if (s2) { uri = s2 + 3; break; }     /* 表示は日本語なので :// を探す */
        }
        if (!uri) { pclose(f); return NULL; }
        for (i = 0; i < sizeof host - 1 && uri[i] && uri[i] != ':' &&
                    uri[i] != '/' && !isspace((unsigned char)uri[i]); i++)
            host[i] = uri[i];
        host[i] = 0;
        pclose(f);
        return host[0] ? host : NULL;
    }
    uri = strstr(uri, "://") + 3;
    if (strchr(uri, '@')) uri = strchr(uri, '@') + 1;   /* user:pass は捨てる */
    for (i = 0; i < sizeof host - 1 && uri[i] && uri[i] != ':' && uri[i] != '/'; i++)
        host[i] = uri[i];
    host[i] = 0;
    return host[0] ? host : NULL;
}

/* 状態を読んで CUPS に伝える。プリントキューの画面に理由が出る。
   届かない/答えない場合は黙って何もしない(印刷は止めない) */
static void report_status(void) {
    static const char *ALL =
        "media-empty-warning,marker-supply-low-warning,marker-supply-empty-warning,"
        "cover-open-warning,media-jam-warning,offline-report,other-warning";
    unsigned char v[64];
    int len = 0, tag, b;
    const char *host;
    char on[256]; size_t n = 0;
    const char *msg = NULL;

    if (getenv("MI700_SNMP") && !strcmp(getenv("MI700_SNMP"), "off")) return;
    host = printer_host();
    if (!host) return;
    tag = snmp_get(host, OID_ERRSTATE, (int)sizeof OID_ERRSTATE, v, sizeof v, &len);
    if (tag != 0x04) return;                     /* OCTET STRING 以外は解釈しない */
    b = len > 0 ? v[0] : 0;
    /* オフラインは用紙切れ等の結果として立つ。原因が分かっているときは
       原因の方を見せる(offline-report を出すと見出しが「オフラインです」になる) */
    if (b & (ERR_NOPAPER | ERR_JAMMED | ERR_DOOROPEN | ERR_NOTONER)) b &= ~ERR_OFFLINE;

#define ADD(c, kw, jp) do { if (b & (c)) { \
        n += (size_t)snprintf(on + n, sizeof on - n, "%s%s", n ? "," : "", kw); \
        if (!msg) msg = jp; } } while (0)
    ADD(ERR_NOPAPER,  "media-empty-warning",         "用紙がありません");
    ADD(ERR_JAMMED,   "media-jam-warning",           "紙詰まりです");
    ADD(ERR_DOOROPEN, "cover-open-warning",          "カバーが開いています");
    ADD(ERR_OFFLINE,  "offline-report",              "オフラインです(印刷可を押してください)");
    ADD(ERR_NOTONER,  "marker-supply-empty-warning", "リボンがありません");
    ADD(ERR_LOWTONER, "marker-supply-low-warning",   "リボンが残り少なくなっています");
    ADD(ERR_SERVICE,  "other-warning",               "サービスを要求しています");
#undef ADD

    fprintf(stderr, "STATE: -%s\n", ALL);         /* 前回の分を消してから付け直す */
    if (n) {
        fprintf(stderr, "STATE: +%s\n", on);
        logmsg("DEBUG", "プリンタの状態 0x%02x", b);   /* INFO の前に出す(後だと上書きする) */
        fprintf(stderr, "INFO: %s\n", msg);
    }
}

/* --status: 状態を人が読める形で表示する(印刷はしない) */
static int show_status(const char *host) {
    static const char *PRST[] = {"", "other", "unknown", "待機中", "印刷中", "ウォームアップ中"};
    unsigned char v[64];
    int len = 0, tag, b, i;
    static const struct { int bit; const char *jp; } T[] = {
        {ERR_LOWPAPER, "用紙残り少"}, {ERR_NOPAPER,  "用紙なし"},
        {ERR_LOWTONER, "リボン残り少"}, {ERR_NOTONER, "リボンなし"},
        {ERR_DOOROPEN, "カバー開"},   {ERR_JAMMED,   "紙詰まり"},
        {ERR_OFFLINE,  "オフライン"}, {ERR_SERVICE,  "サービス要求"},
    };
    if (!host) host = printer_host();
    if (!host) { fprintf(stderr, "接続先が分かりません。ホスト名かIPを指定してください。\n"); return 1; }
    printf("接続先: %s\n", host);
    tag = snmp_get(host, OID_PRSTATUS, (int)sizeof OID_PRSTATUS, v, sizeof v, &len);
    if (tag == 0x02 && len == 1 && v[0] >= 1 && v[0] <= 5)
        printf("状態  : %s\n", PRST[v[0]]);
    else if (tag < 0) { printf("状態  : 応答なし(電源またはネットワークを確認してください)\n"); return 1; }
    tag = snmp_get(host, OID_ERRSTATE, (int)sizeof OID_ERRSTATE, v, sizeof v, &len);
    if (tag != 0x04) { printf("エラー: 取得できません\n"); return 1; }
    b = len > 0 ? v[0] : 0;
    printf("エラー: ");
    if (!b) printf("なし");
    for (i = 0; i < (int)(sizeof T / sizeof T[0]); i++)
        if (b & T[i].bit) printf("%s ", T[i].jp);
    printf(" (0x%02x)\n", b);
    printf("\n※ この機種の LAN カードは Printer-MIB を実装していないため、\n"
           "   給紙口の装備・用紙残量・リボン残量は取得できません。\n");
    return 0;
}

int main(int argc, char *argv[]) {
    /* 給紙口: feeder=シートフィーダ(自動吸入) guide=シートガイド(手差し)
                front=フロントトラクタ rear=リアトラクタ
       印刷品質: std-bi / std-uni / draft-bi / draft-uni / none  (Windows ドライバに合わせた) */
    const char *source = "feeder", *eject = "front", *quality = "std-bi",
               *kanji = "1990"; int bottom = 0;
    int is_cut, is_feeder;
    unsigned char magic[4];
    int pages = 0;

    if (argc >= 2 && !strcmp(argv[1], "--status"))
        return show_status(argc > 2 ? argv[2] : NULL);
    if (argc < 6) { logmsg("ERROR", "引数が足りません"); return 1; }
    report_status();          /* 止まる理由をプリントキューに出しておく */
    /* argv[5] は "key=value key=value ..." */
    {
        char *o = strdup(argv[5]), *tok, *sp = NULL;
        for (tok = strtok_r(o, " \t", &sp); tok; tok = strtok_r(NULL, " \t", &sp)) {
            char *eq = strchr(tok, '='); if (!eq) continue; *eq = 0;
            /* 標準名(InputSlot/OutputBin/OutputMode)を優先。MI700* は旧名の互換 */
            if      (!strcmp(tok, "InputSlot") ||
                     !strcmp(tok, "MI700Source"))    source = strdup(eq+1);
            else if (!strcmp(tok, "OutputBin") ||
                     !strcmp(tok, "MI700Eject"))     eject  = strdup(eq+1);
            else if (!strcmp(tok, "OutputMode") ||
                     !strcmp(tok, "MI700Quality"))   quality = strdup(eq+1);
            else if (!strcmp(tok, "MI700Direction")) {   /* 旧オプション名の互換 */
                quality = !strcmp(eq+1, "uni") ? "std-uni" : "std-bi";
            }
            else if (!strcmp(tok, "MI700Kanji"))     kanji  = strdup(eq+1);
            else if (!strcmp(tok, "MI700Bottom"))    bottom = atoi(eq+1);
        }
    }
    /* 値の名前の正規化。reartractor/fronttractor は IPP 標準キーワード(rear)と
       衝突して macOS に訳語を差し替えられるのを避けるための名前 */
    if (!strcmp(source, "cut"))          source = "feeder";   /* 旧オプション値の互換 */
    if (!strcmp(source, "reartractor"))  source = "rear";
    if (!strcmp(source, "fronttractor")) source = "front";
    if (strcmp(source,"feeder") && strcmp(source,"guide") &&
        strcmp(source,"rear")   && strcmp(source,"front")) source = "feeder";
    if (strcmp(eject,"front") && strcmp(eject,"rear")) eject = "front";
    is_feeder = !strcmp(source, "feeder");
    is_cut    = is_feeder || !strcmp(source, "guide");

    in = (argc > 6) ? fopen(argv[6], "rb") : stdin;
    if (!in) { logmsg("ERROR", "入力を開けません"); return 1; }
    if (readn(magic, 4) != 4) { logmsg("ERROR", "入力が空です"); return 1; }
    if      (!memcmp(magic, "RaS2", 4)) { little = 0; compressed = 1; }
    else if (!memcmp(magic, "2SaR", 4)) { little = 1; compressed = 1; }
    else if (!memcmp(magic, "RaS3", 4)) { little = 0; compressed = 0; }
    else if (!memcmp(magic, "3SaR", 4)) { little = 1; compressed = 0; }
    else { logmsg("ERROR", "CUPS ラスタではありません"); return 1; }

    for (;;) {
        unsigned char hdr[HDRSIZE];
        unsigned vdpi, hdpi, w, h, bpp, stride, cspace;
        unsigned bbtop, mleft, pgh, xoff = 0, yoff = 0;
        unsigned char *ras;
        if (readn(hdr, HDRSIZE) != HDRSIZE) break;
        hdpi   = rd32(hdr + 276);
        vdpi   = rd32(hdr + 280);
        /* ラスタは印字可能範囲ぶんしか無いので、ページ原点からのずれを足し戻す。
           これをしないと内容が左上にずれる(連続紙は余白0なので影響しない) */
        bbtop  = rd32(hdr + 296);            /* ImagingBoundingBox の上辺(下端基準pt) */
        mleft  = rd32(hdr + 312);            /* Margins[0] = 左余白(pt) */
        pgh    = rd32(hdr + 356);            /* PageSize[1] = ページ高さ(pt) */
        w      = rd32(hdr + 372);
        h      = rd32(hdr + 376);
        bpp    = rd32(hdr + 388);
        stride = rd32(hdr + 392);
        cspace = rd32(hdr + 400);
        if (bpp != 1) { logmsg("ERROR", "1bit 以外は非対応です (bpp=%u)", bpp); return 1; }
        if (!vdpi || !w || !h || !stride) { logmsg("ERROR", "ラスタヘッダが不正です"); return 1; }
        ras = calloc((size_t)stride * h, 1);
        if (!ras) { logmsg("ERROR", "メモリ不足"); return 1; }
        if (compressed) unrle(ras, stride, h, bpp);
        else            readn(ras, (size_t)stride * h);

        if (pages == 0) {
            int lines = 0;
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
            fputs("\033M", stdout);
            fputs("\033/136", stdout);
            fputs("\033L000", stdout);
            fputs("\033f", stdout);
            if (strcmp(quality, "none")) {
                fputs(strncmp(quality, "draft", 5) ? "\033d1" : "\033d0", stdout);
                fputs(strstr(quality, "uni") ? "\033>" : "\033]", stdout);
            }
            printf("\034" "05F2-%s", !strcmp(kanji,"1978") ? "00"
                                   : !strcmp(kanji,"1983") ? "01" : "02");
            fputs("\033e11", stdout);
            printf("\033m%c", !strcmp(source,"front") ? '1' : is_cut ? '2' : '3');
            fputc(0x19, stdout);                          /* EM: 給紙口切替の完了を待つ */
            if (is_cut) {
                fputs(!strcmp(eject,"rear") ? "\034" "02EF" : "\034" "02ER", stdout);
                /* カット紙の用紙長 FS 05v。単位 1/120 インチ + 300(2.5インチ)。
                   送らないと本体の工場設定(66行=11インチ)で判断され停止する。
                   どちらのマニュアルにも無いが、Windows のキャプチャ2つで裏が取れている:
                     連続紙  540 = 4.5インチ ちょうど(オフセットなし)
                     カット紙 1703 = 11.69インチ(A4) + 2.5インチ
                   **用紙の長さ**であってラスタの高さではない。ラスタは印字可能範囲ぶんしか
                   無いので、それを使うと上下の余白のぶん(A4 で16mm)短くなる */
                {
                    double plen = pgh ? (double)pgh / 72.0 * 120.0
                                      : (double)h / vdpi * 120.0;
                    int v = (int)(plen + 0.5) + 300;
                    if (v >= 1 && v <= 9999) printf("\034" "05v%04d", v);
                    else logmsg("WARNING", "用紙長 %d は範囲外のため送りません", v);
                    lines = v;
                }
            } else {                                      /* 連続紙は簡易VFU(行数) */
                lines = (int)((double)h / vdpi * LINE_INCH + 0.5);
                if (lines >= 1 && lines <= 99) {
                    if (bottom > 0 && bottom <= lines - 2) printf("\033v%02d,%02d.", lines, bottom);
                    else                                   printf("\033v%02d.", lines);
                } else { logmsg("WARNING", "用紙長 %d 行は範囲外のため送りません", lines); lines = 0; }
            }
            fputc('\r', stdout);
            if (is_feeder) fputs("\033a", stdout);        /* 全排出後全吸入 */
            fputc(0x19, stdout);                          /* EM: 給紙の完了を待つ */
            fputc('\r', stdout);
            logmsg("DEBUG", "source=%s eject=%s quality=%s bottom=%d %udpi lines=%d",
                   source, eject, quality, bottom, vdpi, lines);
        } else {
            /* 次ページ: フィーダは排出して次を吸入、手差しは排出のみ、連続紙は改ページ */
            fputs(is_feeder ? "\033a" : is_cut ? "\033b" : "\014", stdout);
        }
        logmsg("DEBUG", "ページ%d: %ux%u ドット", pages + 1, w, h);
        if (hdpi && mleft)            xoff = (unsigned)((double)mleft * hdpi / 72.0 + 0.5);
        if (pgh && bbtop && pgh > bbtop)
            yoff = (unsigned)((double)(pgh - bbtop) * vdpi / 72.0 + 0.5);
        if (xoff || yoff)
            logmsg("DEBUG", "印字可能範囲のオフセット: 左%uドット 上%uドット", xoff, yoff);
        encode_page(ras, w, h, stride, vdpi, cspace == 0, xoff, yoff);
        free(ras);
        pages++;
    }
    if (!pages) { logmsg("ERROR", "ページがありません"); return 1; }
    fputs(is_cut ? "\r\033b\033c8" : "\r\014\033c8", stdout);
    fflush(stdout);
    return 0;
}
