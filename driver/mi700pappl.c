/* MultiImpact 700XX の Printer Application (PAPPL 版)
 *
 * PPD を使わずに、AirPrint / IPP Everywhere のプリンタとして名乗る。
 * ippeveprinter 版(mi700ipp)との違いは次の2点:
 *   - 表示名のファイル(printer-strings-uri)を配れるので、用紙の種類に
 *     「ミシン目回避」のような独自の項目を日本語で出せる
 *   - ブラウザから設定できる画面が付く
 *
 * 用紙は forms.conf から読む(PPD を作る mkppd.py と同じファイル)。
 * 201PL の組み立ては mi700enc.c を共用する。
 *
 *   mi700pappl server -o server-port=8631
 *   mi700pappl add -d "MultiImpact 700XX" -v socket://192.168.1.160:9100 -m mi700
 */
#include <pappl/pappl.h>
#include "mi700enc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_FORMS  PAPPL_MAX_MEDIA

typedef struct                          /* forms.conf の1行 */
{
  char  pwg[128];                       /* PWG の用紙名 */
  char  label[128];                     /* 表示名(日本語) */
  int   width, length;                  /* 1/100 mm */
  int   left, right, top, bottom;       /* 印字不可領域 1/100 mm */
} mi700_form_t;

static mi700_form_t forms[MAX_FORMS];
static int          num_forms;
static char         strings_ja[8192];

typedef struct                          /* ジョブ1件ぶんの状態 */
{
  mi700_job_t    job;
  unsigned char *bits;                  /* 1bit のページ画像 */
  unsigned       width, height, stride;
  int            bilevel;
} mi700_ctx_t;


/* forms.conf を読む。PWG の標準名があるものはそれを使う */
static const struct { const char *key, *pwg; } PWGNAME[] =
{
  { "A3",     "iso_a3_297x420mm" },
  { "B4",     "jis_b4_257x364mm" },
  { "A4",     "iso_a4_210x297mm" },
  { "B5",     "jis_b5_182x257mm" },
  { "A5",     "iso_a5_148x210mm" },
  { "Letter", "na_letter_8.5x11in" }
};

static void
lower(char *s)
{
  for (; *s; s ++)
    if (*s >= 'A' && *s <= 'Z') *s += 32;
}

static int
load_forms(void)
{
  static const char * const paths[] =
  {
    NULL,                               /* MI700_FORMS */
    "/usr/local/share/mi700/forms.conf"
  };
  const char *env = getenv("MI700_FORMS");
  size_t      i;
  FILE       *fp = NULL;
  char        line[512];

  for (i = 0; i < sizeof(paths) / sizeof(paths[0]) && !fp; i ++)
  {
    const char *path = i == 0 ? env : paths[i];
    if (path)
      fp = fopen(path, "r");
  }
  if (!fp)
    return (0);

  while (num_forms < MAX_FORMS && fgets(line, sizeof line, fp))
  {
    char *f[8], *p = line;
    int   n = 0;
    double w, h, m[4] = { 0, 0, 0, 0 };
    mi700_form_t *form;

    if (*line == '#' || *line == '\n')
      continue;
    while (n < 8 && p)                  /* タブ区切り */
    {
      f[n ++] = p;
      if ((p = strchr(p, '\t')) != NULL) *p ++ = '\0';
    }
    if (n < 4)
      continue;
    if (strchr(f[n - 1], '\n')) *strchr(f[n - 1], '\n') = '\0';

    w = atof(f[2]);
    h = atof(f[3]);
    if (w <= 0 || h <= 0)
      continue;
    for (i = 4; i < (size_t)n && i < 8; i ++)
      m[i - 4] = atof(f[i]);

    form = forms + num_forms;
    memset(form, 0, sizeof(*form));
    for (i = 0; i < sizeof(PWGNAME) / sizeof(PWGNAME[0]); i ++)
      if (!strcmp(f[0], PWGNAME[i].key))
        papplCopyString(form->pwg, PWGNAME[i].pwg, sizeof(form->pwg));
    if (!form->pwg[0])
    {
      char key[64];
      papplCopyString(key, f[0], sizeof(key));
      lower(key);
      snprintf(form->pwg, sizeof(form->pwg), "custom_%s_%gx%gmm", key, w, h);
    }
    papplCopyString(form->label, f[1], sizeof(form->label));
    form->width  = (int)(w * 100.0 + 0.5);
    form->length = (int)(h * 100.0 + 0.5);
    form->left   = (int)(m[0] * 100.0 + 0.5);
    form->right  = (int)(m[1] * 100.0 + 0.5);
    form->top    = (int)(m[2] * 100.0 + 0.5);
    form->bottom = (int)(m[3] * 100.0 + 0.5);
    num_forms ++;
  }
  fclose(fp);
  return (num_forms);
}


/* 表示名のファイル(日本語)。用紙名は forms.conf の表示名をそのまま使う */
static void
build_strings(void)
{
  int  i;
  char line[256];

  papplCopyString(strings_ja,
                  "\"media-type.stationery\" = \"普通紙 (白黒)\";\n"
                  "\"media-type.photographic\" = \"写真 (グレースケール)\";\n"
                  "\"media-type.com.mi700-perforation\" = \"ミシン目回避 (連続紙)\";\n"
                  "\"media-source.com.mi700-feeder\" = \"シートフィーダ (自動吸入)\";\n"
                  "\"media-source.com.mi700-guide\" = \"シートガイド (手差し)\";\n"
                  "\"media-source.com.mi700-reartractor\" = \"リアトラクタ (連続紙)\";\n"
                  "\"media-source.com.mi700-fronttractor\" = \"フロントトラクタ (連続紙)\";\n"
                  "\"output-bin.front\" = \"手前側 (シートガイド)\";\n"
                  "\"output-bin.rear\" = \"奥側 (スタッカ)\";\n",
                  sizeof(strings_ja));

  for (i = 0; i < num_forms; i ++)
  {
    snprintf(line, sizeof line, "\"media.%s\" = \"%s\";\n", forms[i].pwg, forms[i].label);
    if (strlen(strings_ja) + strlen(line) < sizeof(strings_ja))
      strncat(strings_ja, line, sizeof(strings_ja) - strlen(strings_ja) - 1);
  }
}


/* 給紙口: 独自の名前にすると Mac では日本語で出る(iPhone はキーワードのまま) */
static const char * const SOURCES[] =
{
  "com.mi700-feeder", "com.mi700-guide", "com.mi700-reartractor", "com.mi700-fronttractor"
};
static const char * const TYPES[] = { "stationery", "photographic", "com.mi700-perforation" };

static const char *                     /* 給紙口 → mi700enc の値 */
map_source(const char *s)
{
  if (!s || !*s)                                 return ("feeder");
  if (strstr(s, "reartractor")  || !strcmp(s, "rear"))   return ("rear");
  if (strstr(s, "fronttractor") || !strcmp(s, "bottom")) return ("front");
  if (strstr(s, "guide")        || !strcmp(s, "manual")) return ("guide");
  return ("feeder");
}


/* ---- 印刷 ------------------------------------------------------------ */

static void
dev_write(void *ctx, const void *data, size_t len)
{
  papplDeviceWrite((pappl_device_t *)ctx, data, len);
}

/* ドライバ(PPD)を入れた Mac からは、変換済みの 201PL がそのまま届く。
   先頭が CAN ESC M のときだけ素通しする(PDF などを流すとプリンタが文字として
   印字してしまう) */
static bool
mi700_printfile(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
  int           fd;
  unsigned char buf[65536];
  ssize_t       n;
  bool          first = true;

  (void)options;
  if ((fd = open(papplJobGetFilename(job), O_RDONLY)) < 0)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "ファイルを開けません");
    return (false);
  }

  while ((n = read(fd, buf, sizeof buf)) > 0)
  {
    if (first)
    {
      first = false;
      if (n < 3 || buf[0] != 0x18 || buf[1] != 0x1b || buf[2] != 'M')
      {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "MultiImpact 700XX 用のデータではありません");
        close(fd);
        return (false);
      }
      papplLogJob(job, PAPPL_LOGLEVEL_INFO, "変換済みのデータをそのまま送ります");
    }
    papplDeviceWrite(device, buf, (size_t)n);
  }
  close(fd);
  papplDeviceFlush(device);
  return (true);
}

static bool
mi700_rstartjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
  mi700_ctx_t *ctx = (mi700_ctx_t *)calloc(1, sizeof(mi700_ctx_t));

  if (!ctx)
    return (false);

  ctx->job.source  = map_source(options->media.source);
  ctx->job.eject   = strstr(options->output_bin, "rear") ? "rear" : "front";
  ctx->job.quality = options->print_quality == IPP_QUALITY_DRAFT ? "draft-bi" :
                     options->print_quality == IPP_QUALITY_HIGH  ? "std-uni"  : "std-bi";
  ctx->job.write   = dev_write;
  ctx->job.ctx     = device;
  /* 用紙の種類で白黒かグレースケールかを決める(印刷ダイアログに出る唯一の項目)。
     ミシン目回避はボトム領域として本体に送る */
  ctx->bilevel     = strstr(options->media.type, "photographic") == NULL;
  if (strstr(options->media.type, "perforation"))
    ctx->job.bottom = 6;                /* 6行 = 25.4mm */

  mi700_begin(&ctx->job);
  papplJobSetData(job, ctx);
  papplLogJob(job, PAPPL_LOGLEVEL_INFO, "給紙口=%s 排出=%s 品質=%s %s ボトム=%d",
              ctx->job.source, ctx->job.eject, ctx->job.quality,
              ctx->bilevel ? "白黒" : "グレー", ctx->job.bottom);
  return (true);
}

static bool
mi700_rstartpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page)
{
  mi700_ctx_t *ctx = (mi700_ctx_t *)papplJobGetData(job);
  unsigned     page_pt;

  (void)device; (void)page;
  if (!ctx)
    return (false);

  ctx->width  = options->header.cupsWidth;
  ctx->height = options->header.cupsHeight;
  ctx->stride = (ctx->width + 7) / 8;
  if ((ctx->bits = (unsigned char *)calloc(ctx->stride, ctx->height)) == NULL)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "メモリが足りません");
    return (false);
  }

  /* 用紙の長さ(ポイント)。ラスタの高さではなく media の長さを使う */
  page_pt = (unsigned)(options->media.size_length * 72.0 / 2540.0 + 0.5);
  mi700_page_begin(&ctx->job, ctx->height, options->header.HWResolution[1], page_pt);
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "ページ%u: %ux%u ドット %udpi 用紙長=%d",
              page + 1, ctx->width, ctx->height, options->header.HWResolution[1], ctx->job.lines);
  return (true);
}

static bool
mi700_rwriteline(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device,
                 unsigned y, const unsigned char *line)
{
  mi700_ctx_t   *ctx = (mi700_ctx_t *)papplJobGetData(job);
  unsigned char *dst;
  unsigned       x;

  (void)device;
  if (!ctx || !ctx->bits || y >= ctx->height)
    return (true);

  dst = ctx->bits + (size_t)y * ctx->stride;

  if (options->header.cupsBitsPerPixel == 1)
  {
    /* black_1: そのまま(1=黒) */
    memcpy(dst, line, ctx->stride);
  }
  else if (options->header.cupsBitsPerPixel == 8)
  {
    /* sgray_8: 白黒かグレースケールかに応じて 1bit にする */
    for (x = 0; x < ctx->width; x ++)
      if (mi700_dot(line[x], x, y, ctx->bilevel))
        dst[x >> 3] |= (unsigned char)(0x80u >> (x & 7));
  }
  else
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "%ubit は扱えません", options->header.cupsBitsPerPixel);
    return (false);
  }
  return (true);
}

static bool
mi700_rendpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page)
{
  mi700_ctx_t *ctx = (mi700_ctx_t *)papplJobGetData(job);

  (void)device; (void)page;
  if (!ctx || !ctx->bits)
    return (false);

  mi700_page_bits(&ctx->job, ctx->bits, ctx->width, ctx->height, ctx->stride,
                  options->header.HWResolution[1], /*invert*/0, /*xoff*/0, /*yoff*/0);
  free(ctx->bits);
  ctx->bits = NULL;
  return (true);
}

static bool
mi700_rendjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
  mi700_ctx_t *ctx = (mi700_ctx_t *)papplJobGetData(job);

  (void)options; (void)device;
  if (!ctx)
    return (false);

  mi700_end(&ctx->job);
  free(ctx->bits);
  free(ctx);
  papplJobSetData(job, NULL);
  return (true);
}


/* ---- ドライバの定義 --------------------------------------------------- */

static bool
mi700_driver(pappl_system_t *system, const char *driver_name, const char *device_uri,
             const char *device_id, pappl_pr_driver_data_t *d, ipp_t **attrs, void *data)
{
  int i;

  (void)system; (void)driver_name; (void)device_uri; (void)device_id; (void)attrs; (void)data;

  papplCopyString(d->make_and_model, "NEC MultiImpact 700XX", sizeof(d->make_and_model));
  d->kind              = PAPPL_KIND_DOCUMENT;
  d->ppm               = 4;
  d->format            = "application/octet-stream";   /* 変換済みの 201PL */
  d->printfile_cb      = mi700_printfile;
  d->raster_types      = PAPPL_PWG_RASTER_TYPE_BLACK_1 | PAPPL_PWG_RASTER_TYPE_SGRAY_8;
  d->color_supported   = PAPPL_COLOR_MODE_MONOCHROME;
  d->color_default     = PAPPL_COLOR_MODE_MONOCHROME;
  d->orient_default    = IPP_ORIENT_NONE;
  d->quality_default   = IPP_QUALITY_NORMAL;

  d->num_resolution  = 1;
  d->x_resolution[0] = d->y_resolution[0] = 160;
  d->x_default       = d->y_default       = 160;

  d->num_media = num_forms;
  for (i = 0; i < num_forms; i ++)
    d->media[i] = forms[i].pwg;

  d->num_source = (int)(sizeof(SOURCES) / sizeof(SOURCES[0]));
  for (i = 0; i < d->num_source; i ++)
    d->source[i] = SOURCES[i];

  d->num_type = (int)(sizeof(TYPES) / sizeof(TYPES[0]));
  for (i = 0; i < d->num_type; i ++)
    d->type[i] = TYPES[i];

  d->num_bin     = 2;
  d->bin[0]      = "front";
  d->bin[1]      = "rear";
  d->bin_default = 0;

  /* 印字可能範囲。A4 の値を既定にする(用紙ごとの余白は media_ready に入れる) */
  d->left_right = 160;
  d->bottom_top = 730;

  for (i = 0; i < d->num_source && i < num_forms; i ++)
  {
    const mi700_form_t *f = forms + (i == 0 ? 0 : 0);   /* 既定は先頭(A4 など) */
    papplCopyString(d->media_ready[i].size_name, f->pwg, sizeof(d->media_ready[i].size_name));
    papplCopyString(d->media_ready[i].source, SOURCES[i], sizeof(d->media_ready[i].source));
    papplCopyString(d->media_ready[i].type, "stationery", sizeof(d->media_ready[i].type));
    d->media_ready[i].size_width    = f->width;
    d->media_ready[i].size_length   = f->length;
    d->media_ready[i].left_margin   = f->left;
    d->media_ready[i].right_margin  = f->right;
    d->media_ready[i].top_margin    = f->top;
    d->media_ready[i].bottom_margin = f->bottom;
  }
  d->media_default = d->media_ready[0];

  d->rstartjob_cb  = mi700_rstartjob;
  d->rstartpage_cb = mi700_rstartpage;
  d->rwriteline_cb = mi700_rwriteline;
  d->rendpage_cb   = mi700_rendpage;
  d->rendjob_cb    = mi700_rendjob;

  return (true);
}

static pappl_pr_driver_t mi700_drivers[] =
{
  { "mi700", "NEC MultiImpact 700XX", NULL, NULL }
};


static pappl_system_t *
mi700_system(int num_options, cups_option_t *options, void *data)
{
  pappl_system_t *system;

  (void)data;

  system = papplSystemCreate(PAPPL_SOPTIONS_MULTI_QUEUE | PAPPL_SOPTIONS_WEB_INTERFACE |
                             PAPPL_SOPTIONS_WEB_LOG | PAPPL_SOPTIONS_WEB_NETWORK |
                             PAPPL_SOPTIONS_WEB_SECURITY,
                             "MultiImpact 700XX",
                             (int)cupsGetIntegerOption("server-port", num_options, options),
                             "_print,_universal",
                             cupsGetOption("spool-directory", num_options, options),
                             cupsGetOption("log-file", num_options, options),
                             PAPPL_LOGLEVEL_INFO, /*auth_service*/NULL, /*tls_only*/false);
  if (!system)
    return (NULL);

  papplSystemSetPrinterDrivers(system, (int)(sizeof(mi700_drivers) / sizeof(mi700_drivers[0])),
                               mi700_drivers, /*autoadd_cb*/NULL, /*create_cb*/NULL,
                               mi700_driver, /*data*/NULL);
  /* 表示名のファイル。これを配れるのが ippeveprinter との違い */
  papplSystemAddStringsData(system, "/ja.strings", "ja", strings_ja);
  papplSystemAddStringsData(system, "/en.strings", "en", strings_ja);
  /* 自前の初期化では TCP の待ち受けを自分で開く必要がある */
  papplSystemAddListeners(system, cupsGetOption("listen-hostname", num_options, options));
  return (system);
}


int
main(int argc, char *argv[])
{
  if (!load_forms())
  {
    fprintf(stderr, "forms.conf が見つかりません (MI700_FORMS か /usr/local/share/mi700/forms.conf)\n");
    return (1);
  }
  build_strings();

  return (papplMainloop(argc, argv, "4.0", /*footer_html*/NULL,
                        (int)(sizeof(mi700_drivers) / sizeof(mi700_drivers[0])), mi700_drivers,
                        /*autoadd_cb*/NULL, mi700_driver,
                        /*subcmd_name*/NULL, /*subcmd_cb*/NULL,
                        mi700_system, /*usage_cb*/NULL, /*data*/NULL));
}
