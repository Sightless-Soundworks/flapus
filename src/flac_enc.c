/* ---- FLAC encoder: searches block sizes, predictors and residual coding for the smallest file ----

   Per block and channel the encoder compares constant, verbatim, fixed and LPC subframes.
   LPC candidates come from windowed autocorrelation (several apodizations), the covariance
   method (exact least squares), iteratively reweighted least squares that approximates the
   Rice cost, and a hill climb on the quantized coefficients. Rice partitions and parameters
   are chosen exactly. Block sizes are chosen per segment by dynamic programming (variable
   block size stream). Every frame is decoded again and compared before it is written. */

#define FL_FLAC_SEGMENT 65536   /* samples per channel analysed at once */
#define FL_FLAC_MAX_ORDER 32
#define FL_PI 3.14159265358979323846
#define FL_SLACK 288   /* extra entries after every sample buffer */

/* SSE2 versions of the hot loops (same results as the C code) */
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define FL_FLAC_SSE2 1
#endif

/* ---- Optional threads: fl_parallel() runs fn(ctx, t, nt) for t = 0..nt-1 ---- */
#if !defined(FLAPUS_NO_THREADS) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define FL_THREADS 1
#elif !defined(FLAPUS_NO_THREADS) && (defined(__unix__) || defined(__APPLE__))
#include <pthread.h>
#include <unistd.h>
#define FL_THREADS 2
#else
#define FL_THREADS 0
#endif
#define FL_MAX_THREADS 64

typedef struct {
   void (*fn)(void *ctx, int t, int nt);
   void *ctx;
   int t, nt;
} fl_job;

#if FL_THREADS == 1
static DWORD WINAPI fl_job_main(LPVOID p) {
   fl_job *j = (fl_job*)p;
   j->fn(j->ctx, j->t, j->nt);
   return 0;
}
#elif FL_THREADS == 2
static void *fl_job_main(void *p) {
   fl_job *j = (fl_job*)p;
   j->fn(j->ctx, j->t, j->nt);
   return NULL;
}
#endif

static void fl_parallel(void (*fn)(void*, int, int), void *ctx, int nt) {
   fl_job jobs[FL_MAX_THREADS];
   int t;
#if FL_THREADS == 1
   HANDLE th[FL_MAX_THREADS];
#elif FL_THREADS == 2
   pthread_t th[FL_MAX_THREADS];
   int ok[FL_MAX_THREADS];
#endif
   for (t = 0; t < nt; t++) {
      jobs[t].fn = fn;
      jobs[t].ctx = ctx;
      jobs[t].t = t;
      jobs[t].nt = nt;
   }
#if FL_THREADS == 1
   for (t = 1; t < nt; t++) th[t] = CreateThread(NULL, 0, fl_job_main, &jobs[t], 0, NULL);
   fn(ctx, 0, nt);
   for (t = 1; t < nt; t++) {
      if (th[t]) {
         WaitForSingleObject(th[t], INFINITE);
         CloseHandle(th[t]);
      } else fn(ctx, t, nt);
   }
#elif FL_THREADS == 2
   for (t = 1; t < nt; t++) ok[t] = pthread_create(&th[t], NULL, fl_job_main, &jobs[t]) == 0;
   fn(ctx, 0, nt);
   for (t = 1; t < nt; t++) {
      if (ok[t]) pthread_join(th[t], NULL);
      else fn(ctx, t, nt);
   }
#else
   for (t = 0; t < nt; t++) fn(ctx, t, nt);
#endif
}

static int fl_cpu_count(void) {
#if FL_THREADS == 1
   SYSTEM_INFO si;
   GetSystemInfo(&si);
   return (int)si.dwNumberOfProcessors;
#elif FL_THREADS == 2
   long n = sysconf(_SC_NPROCESSORS_ONLN);
   return n > 0 ? (int)n : 1;
#else
   return 1;
#endif
}

typedef struct {
   int max_order;     /* LPC order limit */
   int max_porder;    /* Rice partition order limit */
   int max_block;     /* block size limit */
   int unit;          /* block size grid for the search (0 = fixed block size) */
   int fixed_block;   /* block size when unit == 0 */
   int nwin;          /* apodization windows tried in the final search */
   int all_orders;    /* evaluate every LPC order (else the best few by prediction error) */
   int prec_range;    /* coefficient precisions tried around the default (99 = all) */
   int ls;            /* covariance method candidates */
   int irls;          /* IRLS iterations */
   int climb;         /* hill climbing passes */
   int dp_search;     /* block costs: 0 = quick estimate from prefix sums, 1 = light search */
   int coarse;        /* longer blocks are tried in steps of coarse_step only (0 = all) */
   int coarse_step;
   int estimate;      /* report the estimated instead of the exact subframe size */
} fl_flac_cfg;

static void fl_flac_level(fl_flac_cfg *c, int level, int lax, int rate) {
   static const fl_flac_cfg levels[9] = {
      /* order porder block unit fixed nwin all prec ls irls climb dp coarse step */
      {8, 6, 4608, 0, 4096, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {12, 8, 4608, 0, 4096, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0},
      {12, 8, 4608, 0, 4096, 3, 0, 2, 1, 2, 0, 0, 0, 0, 0},
      {12, 8, 4608, 512, 4096, 3, 0, 2, 1, 2, 0, 0, 0, 0, 0},
      {12, 8, 4608, 512, 4096, 3, 0, 99, 0, 3, 2, 1, 0, 0, 0},
      {12, 8, 4608, 256, 4096, 3, 0, 99, 0, 4, 4, 1, 0, 0, 0},
      {12, 8, 4608, 256, 4096, 3, 0, 99, 0, 6, 8, 1, 0, 0, 0},
      {12, 8, 4608, 128, 4096, 3, 0, 99, 0, 6, 8, 1, 2048, 256, 0},
      {12, 8, 4608, 128, 4096, 5, 1, 99, 1, 8, 8, 1, 0, 0, 0}};
   *c = levels[level < 0 ? 0 : level > 8 ? 8 : level];
   if (lax) {
      if (c->max_order > 8) c->max_order = 32;
      c->max_porder = 15;
      c->max_block = rate > 48000 ? 32768 : 16384;
   } else if (rate > 48000) c->max_block = 16384;
   if (rate > 48000) {
      if (c->max_order == 12) c->max_order = 32;
      if (level >= 2) c->max_porder = 15;
   }
   if (c->unit && c->max_block > 4608) {
      /* long blocks: keep the search affordable */
      if (c->unit < 256) c->unit = 256;
      c->coarse = 4096;
      c->coarse_step = 1024;
   }
}

/* ---- Bit writer (MSB first) ---- */
typedef struct {
   unsigned char *buf;
   size_t cap, len;
   fl_u64 acc;
   int n;
} fl_bw;

static int fl_bw_reserve(fl_bw *w, size_t bytes) {
   if (w->len+bytes > w->cap) {
      size_t cap = w->cap ? w->cap : 1<<16;
      unsigned char *nb;
      while (cap < w->len+bytes) cap *= 2;
      if (!(nb = (unsigned char*)realloc(w->buf, cap))) return -1;
      w->buf = nb;
      w->cap = cap;
   }
   return 0;
}

static void fl_bw_put(fl_bw *w, fl_u32 v, int k) {
   if (k == 0) return;
   if (k < 32) v &= (1u<<k)-1;
   w->acc = w->acc<<k|v;
   w->n += k;
   while (w->n >= 8) {
      w->n -= 8;
      w->buf[w->len++] = (unsigned char)(w->acc>>w->n);
   }
}

static void fl_bw_put_signed(fl_bw *w, fl_i32 v, int k) {
   if (k > 32) {
      /* 33-bit side channel of 32-bit audio: the sign extension first */
      fl_bw_put(w, v < 0 ? (1u<<(k-32))-1 : 0, k-32);
      k = 32;
   }
   fl_bw_put(w, (fl_u32)v, k);
}

static void fl_bw_zeros(fl_bw *w, fl_u32 q) {
   while (q >= 32) {
      fl_bw_put(w, 0, 32);
      q -= 32;
   }
   fl_bw_put(w, 0, (int)q);
}

static void fl_bw_align(fl_bw *w) {
   if (w->n) fl_bw_put(w, 0, 8-w->n);
}

/* Number of bits needed for x (0 for 0). */
static int fl_bitlen(fl_u32 x) {
#if defined(__GNUC__)
   return x ? 32-__builtin_clz(x) : 0;
#else
   return fl_ilog(x);
#endif
}

/* ---- Rice coding ---- */
static fl_u32 fl_zz(fl_i32 r) { return ((fl_u32)r<<1)^(fl_u32)(r>>31); }

/* Starting Rice parameter of a partition: bit length of min(s/n, 2^32-1) minus 1 for s > n,
   else 0, without the 64-bit division. */
static int fl_rice_k0(fl_u64 s, fl_u32 n) {
   int t;
   if (s <= n) return 0;
#if defined(__GNUC__)
   t = 64-__builtin_clzll(s)-fl_bitlen(n);
#else
   t = (s>>32 ? 32+fl_bitlen((fl_u32)(s>>32)) : fl_bitlen((fl_u32)s))-fl_bitlen(n);
#endif
   if (((fl_u64)n<<t) > s) t--;
   return t > 31 ? 31 : t;
}

/* Estimated bits of one partition with parameter k: n*(k+1)+(max(s-corr, 0)>>k), where
   corr = n*(2^k-1)/2 rounded down; exact for k == 0. */
static fl_u64 fl_rice_q(fl_u64 s, fl_u64 corr, int k) { return s > corr ? (s-corr)>>k : 0; }

/* Best estimated cost of a partition (Rice parameter k0-1, k0, k0+1 or escape). An escape
   stores at most 31 bits per sample. */
static fl_u64 fl_rice_part_est(fl_u64 s, fl_u32 n, fl_u32 umax, int kmax) {
   fl_u64 nk, base, best, c;
   int k, eb;
   if (n == 0) return 0;
   k = fl_rice_k0(s, n);
   if (k > kmax) k = kmax;
   nk = (fl_u64)n<<k;
   base = (fl_u64)n*(k+1);
   best = base+fl_rice_q(s, (nk-n)>>1, k);
   if (k > 0 && (c = base-n+fl_rice_q(s, ((nk>>1)-n)>>1, k-1)) < best) best = c;
   if (k < kmax && (c = base+n+fl_rice_q(s, ((nk<<1)-n)>>1, k+1)) < best) best = c;
   if ((eb = fl_bitlen(umax)) > 31) return best;
   c = 5+(fl_u64)n*eb;
   return c < best ? c : best;
}

typedef struct {
   fl_u64 *sum;
   fl_u32 *umax;
   int cap;
} fl_rice_ws;

/* Highest usable partition order for a block of n samples and predictor order. */
static int fl_rice_max_porder(int n, int order, int limit) {
   int p = limit;
   while (p > 0 && (((n>>p)<<p) != n || (n>>p) <= order)) p--;
   return p;
}

/* Fast estimate of the residual bits (including the residual header). Estimates of limit or
   more are not computed to the end: the result is then limit. */
static fl_u64 fl_rice_estimate(fl_rice_ws *ws, const fl_u32 *u, int n, int order, int max_porder, int kmax, fl_u64 limit) {
   int pmax, parts, p, i, pbits = kmax > 14 ? 5 : 4;
   fl_u64 best = limit;
   int psize;
   const fl_u32 *q = u;
   while (max_porder > 0 && (n>>max_porder) < 16) max_porder--;
   pmax = fl_rice_max_porder(n, order, max_porder);
   parts = 1<<pmax;
   psize = n>>pmax;
   /* finest level sums (four independent sums: integer addition, so the order is free) */
   for (p = 0; p < parts; p++) {
      int cnt = p == 0 ? psize-order : psize;
      fl_u64 s0 = 0, s1 = 0, s2 = 0, s3 = 0;
      fl_u32 m0 = 0, m1 = 0, m2 = 0, m3 = 0;
#ifdef FL_FLAC_SSE2
      __m128i vs0 = _mm_setzero_si128(), vs1 = vs0, vm = vs0, z = vs0;
      fl_u64 t2[2];
      for (i = 0; i+3 < cnt; i += 4) {
         __m128i v = _mm_loadu_si128((const __m128i*)(q+i));
         vm = _mm_or_si128(vm, v);
         vs0 = _mm_add_epi64(vs0, _mm_unpacklo_epi32(v, z));
         vs1 = _mm_add_epi64(vs1, _mm_unpackhi_epi32(v, z));
      }
      _mm_storeu_si128((__m128i*)t2, _mm_add_epi64(vs0, vs1));
      s0 = t2[0];
      s1 = t2[1];
      vm = _mm_or_si128(vm, _mm_shuffle_epi32(vm, _MM_SHUFFLE(1, 0, 3, 2)));
      vm = _mm_or_si128(vm, _mm_shuffle_epi32(vm, _MM_SHUFFLE(2, 3, 0, 1)));
      m0 = (fl_u32)_mm_cvtsi128_si32(vm);
#else
      for (i = 0; i+3 < cnt; i += 4) {
         s0 += q[i];
         s1 += q[i+1];
         s2 += q[i+2];
         s3 += q[i+3];
         m0 |= q[i];
         m1 |= q[i+1];
         m2 |= q[i+2];
         m3 |= q[i+3];
      }
#endif
      for (; i < cnt; i++) {
         s0 += q[i];
         m0 |= q[i];
      }
      q += cnt;
      ws->sum[p] = (s0+s1)+(s2+s3);
      ws->umax[p] = (m0|m1)|(m2|m3);  /* OR has the same bit length as the maximum */
   }
   for (;;) {
      fl_u64 tot = 6+(fl_u64)parts*pbits;
      /* partition costs only add up: stop the level once it cannot win */
      for (p = 0; p < parts && tot < best; p++) {
         fl_u32 cnt = (fl_u32)((n>>pmax)-(p == 0 ? order : 0));
         tot += fl_rice_part_est(ws->sum[p], cnt, ws->umax[p], kmax);
      }
      if (tot < best) best = tot;
      if (pmax == 0) break;
      pmax--;
      parts >>= 1;
      for (p = 0; p < parts; p++) {
         ws->sum[p] = ws->sum[2*p]+ws->sum[2*p+1];
         ws->umax[p] = ws->umax[2*p]|ws->umax[2*p+1];
      }
   }
   return best;
}

/* Limit for an estimate that only has to beat best after header bits. */
static fl_u64 fl_est_limit(fl_u64 best, fl_u64 header) { return best > header ? best-header : 0; }

/* Exact Rice coding plan of a residual. */
typedef struct {
   int porder, method;
   int *k;     /* parameter per partition; escapes are stored as -1-bits */
   int cap;
} fl_rice_plan;

static fl_u64 fl_rice_part_exact(const fl_u32 *u, fl_u32 n, int k) {
   fl_u64 s = (fl_u64)n*(k+1);
   fl_u32 i;
   for (i = 0; i < n; i++) s += u[i]>>k;
   return s;
}

/* Best exact parameter (or escape, at most 31 bits) of one partition; returns bits without the
   parameter field. */
static fl_u64 fl_rice_part_best(const fl_u32 *u, fl_u32 n, int kmax, int *kout) {
   fl_u64 s = 0, best, c;
   fl_u32 m = 0, i;
   int k, k0, eb;
   if (n == 0) {
      *kout = 0;
      return 0;
   }
   for (i = 0; i < n; i++) {
      s += u[i];
      m |= u[i];
   }
   k0 = fl_rice_k0(s, n);
   if (k0 > kmax) k0 = kmax;
   best = fl_rice_part_exact(u, n, k0);
   *kout = k0;
   for (k = k0-1; k >= 0 && (c = fl_rice_part_exact(u, n, k)) < best; k--) {
      best = c;
      *kout = k;
   }
   if (*kout == k0)
      for (k = k0+1; k <= kmax && (c = fl_rice_part_exact(u, n, k)) < best; k++) {
         best = c;
         *kout = k;
      }
   eb = fl_bitlen(m);
   c = 5+(fl_u64)n*eb;
   if (eb <= 31 && c < best) {
      best = c;
      *kout = -1-eb;
   }
   return best;
}

/* Exact plan: best partition order and parameters (method 0 and 1 compared). */
static fl_u64 fl_rice_exact(fl_rice_plan *plan, int *tmp, const fl_u32 *u, int n, int order, int max_porder, int allow_rice2) {
   int pmax = fl_rice_max_porder(n, order, max_porder), method, p, j;
   fl_u64 best = ~(fl_u64)0;
   for (method = 0; method <= allow_rice2; method++) {
      int kmax = method ? 30 : 14, pbits = method ? 5 : 4;
      for (p = 0; p <= pmax; p++) {
         int parts = 1<<p, psize = n>>p;
         const fl_u32 *q = u;
         fl_u64 tot = 6+(fl_u64)parts*pbits;
         for (j = 0; j < parts && tot < best; j++) {
            fl_u32 cnt = (fl_u32)(psize-(j == 0 ? order : 0));
            tot += fl_rice_part_best(q, cnt, kmax, &tmp[j]);
            q += cnt;
         }
         if (tot < best) {
            best = tot;
            plan->porder = p;
            plan->method = method;
            memcpy(plan->k, tmp, sizeof(int)*parts);
         }
      }
   }
   return best;
}

static void fl_rice_write(fl_bw *w, const fl_rice_plan *plan, const fl_i32 *res, int n, int order) {
   int parts = 1<<plan->porder, psize = n>>plan->porder, pbits = plan->method ? 5 : 4, j, i = order;
   fl_bw_put(w, plan->method, 2);
   fl_bw_put(w, plan->porder, 4);
   for (j = 0; j < parts; j++) {
      int k = plan->k[j], end = (j+1)*psize;
      if (k < 0) {
         int eb = -1-k;
         fl_bw_put(w, (1u<<pbits)-1, pbits);
         fl_bw_put(w, eb, 5);
         for (; i < end; i++) fl_bw_put_signed(w, res[i], eb);
      } else {
         fl_bw_put(w, k, pbits);
         for (; i < end; i++) {
            fl_u32 v = fl_zz(res[i]), q = v>>k;
            if (q+1+k <= 32) fl_bw_put(w, (1u<<k|(v&((1u<<k)-1))), (int)q+1+k);
            else {
               fl_bw_zeros(w, q);
               fl_bw_put(w, 1, 1);
               fl_bw_put(w, v, k);
            }
         }
      }
   }
}

/* ---- Subframe description ---- */
typedef struct {
   int type;       /* 0 constant, 1 verbatim, 2 fixed, 3 LPC */
   int order, wasted, prec, shift;
   fl_i32 coef[FL_FLAC_MAX_ORDER];
   fl_u64 bits;    /* exact size of the subframe */
} fl_sf;

/* Per-thread work space for the subframe search. */
typedef struct {
   int cap;
   fl_i32 *x, *res;
   fl_i16 *x16, *x16l;   /* x in 16-bit parts for fl_lpc_sums (see fl_fill_x16) */
   fl_u32 *u;
   fl_i64 *acc;
   double *xd, *wx, *wt, *e;
   fl_rice_ws rws;
   fl_rice_plan plan;
   int *ktmp;
} fl_ws;

static void fl_ws_free(fl_ws *w) {
   free(w->x);
   free(w->x16);
   free(w->x16l);
   free(w->res);
   free(w->u);
   free(w->acc);
   free(w->xd);
   free(w->wx);
   free(w->wt);
   free(w->e);
   free(w->rws.sum);
   free(w->rws.umax);
   free(w->plan.k);
   free(w->ktmp);
   memset(w, 0, sizeof(*w));
}

static int fl_ws_alloc(fl_ws *w, int n, int max_porder) {
   int parts = 1<<max_porder;
   if (n <= w->cap) return 0;
   fl_ws_free(w);
   w->cap = n;
   n += FL_SLACK;
   w->x = (fl_i32*)calloc(n, sizeof(fl_i32));
   w->x16 = (fl_i16*)calloc(n, sizeof(fl_i16));
   w->x16l = (fl_i16*)calloc(n, sizeof(fl_i16));
   w->res = (fl_i32*)malloc(sizeof(fl_i32)*n);
   w->u = (fl_u32*)malloc(sizeof(fl_u32)*n);
   w->acc = (fl_i64*)malloc(sizeof(fl_i64)*n);
   w->xd = (double*)calloc(n, sizeof(double));
   w->wx = (double*)malloc(sizeof(double)*n);
   w->wt = (double*)malloc(sizeof(double)*n);
   w->e = (double*)malloc(sizeof(double)*n);
   w->rws.sum = (fl_u64*)malloc(sizeof(fl_u64)*parts);
   w->rws.umax = (fl_u32*)malloc(sizeof(fl_u32)*parts);
   w->plan.k = (int*)malloc(sizeof(int)*parts);
   w->ktmp = (int*)malloc(sizeof(int)*parts);
   return w->x && w->x16 && w->x16l && w->res && w->u && w->acc && w->xd && w->wx && w->wt && w->e &&
          w->rws.sum && w->rws.umax && w->plan.k && w->ktmp ? 0 : -1;
}

/* ---- Prediction ---- */

/* Fixed predictor residual r and its zigzag values u[i-order]; returns 0 if a residual does
   not fit in 32 bits. For bps <= 27 every residual and partial sum is below 2^30 in
   magnitude, so 32-bit arithmetic is exact. */
static int fl_fixed_residual(const fl_i32 *x, int n, int order, int bps, fl_i32 *r, fl_u32 *u) {
   int i;
   for (i = 0; i < order; i++) r[i] = x[i];
   if (bps <= 27) {
      fl_i32 v;
      switch (order) {
      case 0:
         for (i = 0; i < n; i++) {
            r[i] = x[i];
            u[i] = fl_zz(x[i]);
         }
         break;
      case 1:
         for (i = 1; i < n; i++) {
            v = x[i]-x[i-1];
            r[i] = v;
            u[i-1] = fl_zz(v);
         }
         break;
      case 2:
         for (i = 2; i < n; i++) {
            v = x[i]-2*x[i-1]+x[i-2];
            r[i] = v;
            u[i-2] = fl_zz(v);
         }
         break;
      case 3:
         for (i = 3; i < n; i++) {
            v = x[i]-3*(x[i-1]-x[i-2])-x[i-3];
            r[i] = v;
            u[i-3] = fl_zz(v);
         }
         break;
      default:
         for (i = 4; i < n; i++) {
            v = x[i]-4*(x[i-1]+x[i-3])+6*x[i-2]+x[i-4];
            r[i] = v;
            u[i-4] = fl_zz(v);
         }
         break;
      }
      return 1;
   }
   for (i = order; i < n; i++) {
      fl_i64 p;
      switch (order) {
      case 0: p = 0; break;
      case 1: p = x[i-1]; break;
      case 2: p = 2*(fl_i64)x[i-1]-x[i-2]; break;
      case 3: p = 3*((fl_i64)x[i-1]-x[i-2])+x[i-3]; break;
      default: p = 4*((fl_i64)x[i-1]+x[i-3])-6*(fl_i64)x[i-2]-x[i-4]; break;
      }
      p = x[i]-p;
      if (p > 0x7FFFFFFF || p < -0x7FFFFFFF) return 0;
      r[i] = (fl_i32)p;
      u[i-order] = fl_zz((fl_i32)p);
   }
   return 1;
}

/* Unshifted LPC prediction sums acc[i] = sum c[j]*x[i-1-j] for i >= order. mode 0: the sum
   fits in 32 bits (wrapping 32-bit arithmetic is exact), 3: the same with 16-bit samples
   (x16 = x) and coefficients, two taps at a time, 4 (SSE2): the same with x = x16*2^15+x16l,
   1: double is exact (xd = x as double), 2: 64-bit. Modes 0, 1, 3 and 4 read and write up
   to 255 entries past n (buffers have FL_SLACK). */
static void fl_lpc_sums(const fl_i32 *x, const double *xd, const fl_i16 *x16, const fl_i16 *x16l, int n,
                        const fl_i32 *c, int order, int mode, fl_i64 *acc) {
   int i, j, k;
#ifdef FL_FLAC_SSE2
   if (mode == 4) {
      /* like mode 3 for the high and the low parts; the parts fit pmaddwd the same way */
      __m128i cv[FL_FLAC_MAX_ORDER/2+1], z = _mm_setzero_si128();
      int np = order/2;
      for (j = 0; j < np; j++) cv[j] = _mm_set1_epi32((fl_i32)((fl_u32)(fl_u16)c[2*j+1]<<16|(fl_u16)c[2*j]));
      if (order&1) cv[np] = _mm_set1_epi32((fl_i32)(fl_u16)c[order-1]);
      for (i = order; i < n; i += 8) {
         __m128i h0 = z, h1 = z, l0 = z, l1 = z, a, sg;
         for (j = 0; j < np; j++) {
            const fl_i16 *ph = x16+i-1-2*j, *pl = x16l+i-1-2*j;
            __m128i va = _mm_loadu_si128((const __m128i*)ph), vb = _mm_loadu_si128((const __m128i*)(ph-1));
            __m128i la = _mm_loadu_si128((const __m128i*)pl), lb = _mm_loadu_si128((const __m128i*)(pl-1));
            h0 = _mm_add_epi32(h0, _mm_madd_epi16(_mm_unpacklo_epi16(va, vb), cv[j]));
            h1 = _mm_add_epi32(h1, _mm_madd_epi16(_mm_unpackhi_epi16(va, vb), cv[j]));
            l0 = _mm_add_epi32(l0, _mm_madd_epi16(_mm_unpacklo_epi16(la, lb), cv[j]));
            l1 = _mm_add_epi32(l1, _mm_madd_epi16(_mm_unpackhi_epi16(la, lb), cv[j]));
         }
         if (order&1) {
            __m128i va = _mm_loadu_si128((const __m128i*)(x16+i-order)), la = _mm_loadu_si128((const __m128i*)(x16l+i-order));
            h0 = _mm_add_epi32(h0, _mm_madd_epi16(_mm_unpacklo_epi16(va, z), cv[np]));
            h1 = _mm_add_epi32(h1, _mm_madd_epi16(_mm_unpackhi_epi16(va, z), cv[np]));
            l0 = _mm_add_epi32(l0, _mm_madd_epi16(_mm_unpacklo_epi16(la, z), cv[np]));
            l1 = _mm_add_epi32(l1, _mm_madd_epi16(_mm_unpackhi_epi16(la, z), cv[np]));
         }
         a = _mm_add_epi32(_mm_slli_epi32(h0, 15), l0);
         sg = _mm_srai_epi32(a, 31);
         _mm_storeu_si128((__m128i*)(acc+i), _mm_unpacklo_epi32(a, sg));
         _mm_storeu_si128((__m128i*)(acc+i+2), _mm_unpackhi_epi32(a, sg));
         a = _mm_add_epi32(_mm_slli_epi32(h1, 15), l1);
         sg = _mm_srai_epi32(a, 31);
         _mm_storeu_si128((__m128i*)(acc+i+4), _mm_unpacklo_epi32(a, sg));
         _mm_storeu_si128((__m128i*)(acc+i+6), _mm_unpackhi_epi32(a, sg));
      }
      return;
   }
#else
   (void)x16l;
#endif
   if (mode == 3) {
      /* |c*x| <= 2^29, so a pair of products fits in 32 bits even for the stale samples
         past n; the sums wrap like mode 0 */
#ifdef FL_FLAC_SSE2
      /* 8 sums at a time: pmaddwd on the pairs (x[i-1-j], x[i-2-j]) and (c[j], c[j+1]) */
      __m128i cv[FL_FLAC_MAX_ORDER/2+1], z = _mm_setzero_si128();
      int np = order/2;
      for (j = 0; j < np; j++) cv[j] = _mm_set1_epi32((fl_i32)((fl_u32)(fl_u16)c[2*j+1]<<16|(fl_u16)c[2*j]));
      if (order&1) cv[np] = _mm_set1_epi32((fl_i32)(fl_u16)c[order-1]);
      for (i = order; i < n; i += 8) {
         __m128i a0 = z, a1 = z, sg;
         for (j = 0; j < np; j++) {
            const fl_i16 *h0 = x16+i-1-2*j;
            __m128i va = _mm_loadu_si128((const __m128i*)h0), vb = _mm_loadu_si128((const __m128i*)(h0-1));
            a0 = _mm_add_epi32(a0, _mm_madd_epi16(_mm_unpacklo_epi16(va, vb), cv[j]));
            a1 = _mm_add_epi32(a1, _mm_madd_epi16(_mm_unpackhi_epi16(va, vb), cv[j]));
         }
         if (order&1) {
            __m128i va = _mm_loadu_si128((const __m128i*)(x16+i-order));
            a0 = _mm_add_epi32(a0, _mm_madd_epi16(_mm_unpacklo_epi16(va, z), cv[np]));
            a1 = _mm_add_epi32(a1, _mm_madd_epi16(_mm_unpackhi_epi16(va, z), cv[np]));
         }
         sg = _mm_srai_epi32(a0, 31);
         _mm_storeu_si128((__m128i*)(acc+i), _mm_unpacklo_epi32(a0, sg));
         _mm_storeu_si128((__m128i*)(acc+i+2), _mm_unpackhi_epi32(a0, sg));
         sg = _mm_srai_epi32(a1, 31);
         _mm_storeu_si128((__m128i*)(acc+i+4), _mm_unpacklo_epi32(a1, sg));
         _mm_storeu_si128((__m128i*)(acc+i+6), _mm_unpackhi_epi32(a1, sg));
      }
#else
      fl_u32 t[256];
      for (i = order; i < n; i += 256) {
         for (k = 0; k < 256; k++) t[k] = 0;
         for (j = 0; j+1 < order; j += 2) {
            const fl_i16 *h0 = x16+i-1-j, *h1 = x16+i-2-j;
            fl_i32 c0 = c[j], c1 = c[j+1];
            for (k = 0; k < 256; k++) t[k] += (fl_u32)(c0*h0[k]+c1*h1[k]);
         }
         if (j < order) {
            const fl_i16 *h0 = x16+i-1-j;
            fl_i32 c0 = c[j];
            for (k = 0; k < 256; k++) t[k] += (fl_u32)(c0*h0[k]);
         }
         for (k = 0; k < 256; k++) acc[i+k] = (fl_i32)t[k];
      }
#endif
   } else if (mode == 0) {
      fl_u32 t[256];
      for (i = order; i < n; i += 256) {
         for (k = 0; k < 256; k++) t[k] = 0;
         for (j = 0; j < order; j++) {
            const fl_i32 *h = x+i-1-j;
            fl_u32 cj = (fl_u32)c[j];
            for (k = 0; k < 256; k++) t[k] += cj*(fl_u32)h[k];
         }
         for (k = 0; k < 256; k++) acc[i+k] = (fl_i32)t[k];
      }
   } else if (mode == 1) {
      double t[256];
      for (i = order; i < n; i += 256) {
         for (k = 0; k < 256; k++) t[k] = 0;
         for (j = 0; j < order; j++) {
            const double *h = xd+i-1-j;
            double cj = c[j];
            for (k = 0; k < 256; k++) t[k] += cj*h[k];
         }
         for (k = 0; k < 256; k++) acc[i+k] = (fl_i64)t[k];
      }
   } else {
      for (i = order; i < n; i++) {
         fl_i64 sum = 0;
         const fl_i32 *h = x+i-1;
         for (j = 0; j < order; j++) sum += (fl_i64)c[j]*h[-j];
         acc[i] = sum;
      }
   }
}

/* Arithmetic mode for fl_lpc_sums: |sum| < order*2^(prec-1)*2^(bps-1) < 2^(need-2). */
static int fl_lpc_mode(int bps, int prec, int order) {
   int need = bps+prec+fl_bitlen(order);
#ifdef FL_FLAC_SSE2
   if (need <= 33) return bps <= 16 ? 3 : 4;
#else
   if (need <= 33) return bps <= 16 ? 3 : 0;
#endif
   return need <= 53 ? 1 : 2;
}

/* The 16-bit parts of x for modes 3 (bps <= 16: x16 = x) and 4 (x16 = x>>15, x16l = x&0x7FFF). */
static void fl_fill_x16(fl_ws *ws, const fl_i32 *x, int n, int bps) {
   int i = 0;
   if (bps <= 16) {
#ifdef FL_FLAC_SSE2
      for (; i+3 < n; i += 4) {
         __m128i v = _mm_loadu_si128((const __m128i*)(x+i));
         _mm_storel_epi64((__m128i*)(ws->x16+i), _mm_packs_epi32(v, v));
      }
#endif
      for (; i < n; i++) ws->x16[i] = (fl_i16)x[i];
   }
#ifdef FL_FLAC_SSE2
   else if (bps <= 27) {
      __m128i m = _mm_set1_epi32(0x7FFF);
      for (; i+3 < n; i += 4) {
         __m128i v = _mm_loadu_si128((const __m128i*)(x+i)), h = _mm_srai_epi32(v, 15), l = _mm_and_si128(v, m);
         _mm_storel_epi64((__m128i*)(ws->x16+i), _mm_packs_epi32(h, h));
         _mm_storel_epi64((__m128i*)(ws->x16l+i), _mm_packs_epi32(l, l));
      }
      for (; i < n; i++) {
         ws->x16[i] = (fl_i16)(x[i]>>15);
         ws->x16l[i] = (fl_i16)(x[i]&0x7FFF);
      }
   }
#endif
}

/* Zigzag residual u[i-order] from prediction sums; returns 0 on 32-bit overflow. With
   mode 0, 3 or 4 sums (|acc| < 2^31, bps <= 27) and shift >= 1 the residual cannot overflow
   and is computed in 32 bits. */
static int fl_lpc_residual(const fl_i32 *x, int n, int order, int shift, int mode, const fl_i64 *acc, fl_u32 *u) {
   int i = order;
   fl_i64 bad = 0;
   if ((mode == 0 || mode >= 3) && shift >= 1) {
#ifdef FL_FLAC_SSE2
      __m128i sh = _mm_cvtsi32_si128(shift);
      for (; i+3 < n; i += 4) {
         /* the low halves of four 64-bit sums */
         __m128i lo = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i*)(acc+i)), _MM_SHUFFLE(2, 0, 2, 0));
         __m128i hi = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i*)(acc+i+2)), _MM_SHUFFLE(2, 0, 2, 0));
         __m128i v = _mm_sub_epi32(_mm_loadu_si128((const __m128i*)(x+i)), _mm_sra_epi32(_mm_unpacklo_epi64(lo, hi), sh));
         _mm_storeu_si128((__m128i*)(u+i-order), _mm_xor_si128(_mm_slli_epi32(v, 1), _mm_srai_epi32(v, 31)));
      }
#endif
      for (; i < n; i++) u[i-order] = fl_zz(x[i]-((fl_i32)acc[i]>>shift));
      return 1;
   }
   for (; i < n; i++) {
      fl_i64 v = x[i]-(acc[i]>>shift);
      bad |= (v+0x7FFFFFFF)>>32;  /* nonzero outside [-2^31+1, 2^31-1] */
      u[i-order] = fl_zz((fl_i32)v);
   }
   return bad == 0;
}

/* Levinson-Durbin: lpc[o-1][0..o-1] for every order o <= max_order; err[o] prediction error. */
static int fl_levinson(const double *R, int max_order, double lpc[][FL_FLAC_MAX_ORDER], double *err) {
   double a[FL_FLAC_MAX_ORDER+1], t[FL_FLAC_MAX_ORDER+1], e = R[0];
   int i, j, o;
   if (!(e > 0)) return 0;
   a[0] = 1;
   for (o = 1; o <= max_order; o++) {
      double k = -R[o];
      for (j = 1; j < o; j++) k -= a[j]*R[o-j];
      k /= e;
      if (!(k > -1.0 && k < 1.0)) return o-1;
      for (j = 1; j < o; j++) t[j] = a[j]+k*a[o-j];
      for (j = 1; j < o; j++) a[j] = t[j];
      a[o] = k;
      e *= 1-k*k;
      if (!(e > 0)) e = R[0]*1e-15;
      for (i = 0; i < o; i++) lpc[o-1][i] = -a[i+1];
      err[o] = e;
   }
   return max_order;
}

/* Quantizes coefficients like libFLAC (error feedback). Returns 0 if not representable. */
static int fl_quantize(const double *lp, int order, int prec, fl_i32 *q, int *shift_out) {
   double cmax = 0, err = 0;
   int i, log2cmax, shift, qmax = (1<<(prec-1))-1, qmin = -qmax-1;
   for (i = 0; i < order; i++) cmax = FL_MAX(cmax, fabs(lp[i]));
   if (!(cmax > 0)) return 0;
   frexp(cmax, &log2cmax);
   shift = prec-1-log2cmax;
   if (shift > 15) shift = 15;
   if (shift < 0) return 0;
   for (i = 0; i < order; i++) {
      long v;
      err += lp[i]*(1<<shift);
      v = lround(err);
      if (v > qmax) v = qmax;
      else if (v < qmin) v = qmin;
      err -= v;
      q[i] = (fl_i32)v;
   }
   *shift_out = shift;
   return 1;
}

/* Apodization windows: 0 tukey(0.5), 1-2 partial_tukey(2), 3-5 punchout_tukey(3) (libFLAC
   definitions), 6 welch, 7 tukey(0.25), 8 hann. */
static void fl_window(double *w, int n, int type) {
   int i, k;
   if (type == 0 || type == 7 || type == 8) {
      double p = type == 0 ? 0.5 : type == 7 ? 0.25 : 1.0;
      int np = (int)(p/2*n);
      for (i = 0; i < n; i++) w[i] = 1;
      for (i = 0; i < np; i++) w[i] = w[n-1-i] = 0.5-0.5*cos(FL_PI*(i+1)/(np+1));
   } else if (type == 6) {
      double h = (n-1)/2.0;
      for (i = 0; i < n; i++) w[i] = 1-((i-h)/(h+1))*((i-h)/(h+1));
   } else if (type <= 2) {
      double units = 1.0/(1.0-0.1)-1.0, start = (type-1)/(2+units), end = (type+units)/(2+units);
      int sn = (int)(start*n), en = (int)(end*n), Np = (int)(0.2/2*(en-sn));
      for (i = 0; i < n; i++) w[i] = i >= sn && i < en ? 1 : 0;
      for (k = 1; k <= Np; k++) {
         if (sn+k-1 < n) w[sn+k-1] = 0.5-0.5*cos(FL_PI*k/Np);
         if (en-k >= 0 && en-k < n) w[en-k] = 0.5-0.5*cos(FL_PI*k/Np);
      }
   } else {
      double units = 1.0/(1.0-0.2)-1.0, start = (type-3)/(3+units), end = (type-2+units)/(3+units);
      int sn = (int)(start*n), en = (int)(end*n), Ns = (int)(0.2/2*sn), Ne = (int)(0.2/2*(n-en));
      for (i = 0; i < n; i++) w[i] = i >= sn && i < en ? 0 : 1;
      for (k = 1; k <= Ns; k++) {
         w[k-1] = 0.5-0.5*cos(FL_PI*k/Ns);
         if (sn-k >= 0) w[sn-k] = 0.5-0.5*cos(FL_PI*k/Ns);
      }
      for (k = 1; k <= Ne; k++) {
         if (en+k-1 < n) w[en+k-1] = 0.5-0.5*cos(FL_PI*k/Ne);
         w[n-k] = 0.5-0.5*cos(FL_PI*k/Ne);
      }
   }
}

/* Window cache keyed by (length, type). */
typedef struct {
   int n, type;
   double *w;
} fl_wcache_entry;

#define FL_WCACHE 128
typedef struct {
   fl_wcache_entry e[FL_WCACHE];
   int next;
} fl_wcache;

static const double *fl_window_get(fl_wcache *c, int n, int type) {
   int i;
   fl_wcache_entry *e;
   for (i = 0; i < FL_WCACHE; i++)
      if (c->e[i].w && c->e[i].n == n && c->e[i].type == type) return c->e[i].w;
   e = &c->e[c->next];
   c->next = (c->next+1)%FL_WCACHE;
   free(e->w);
   e->w = (double*)malloc(sizeof(double)*n);
   if (!e->w) return NULL;
   e->n = n;
   e->type = type;
   fl_window(e->w, n, type);
   return e->w;
}

static void fl_wcache_free(fl_wcache *c) {
   int i;
   for (i = 0; i < FL_WCACHE; i++) free(c->e[i].w);
}

static void fl_autocorr(const double *x, int n, int lags, double *R) {
   int k, i;
   for (k = 0; k <= lags; k++) {
      double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
#ifdef FL_FLAC_SSE2
      /* lanes (s0, s1) and (s2, s3): the same products and sums in the same order */
      __m128d a01 = _mm_setzero_pd(), a23 = a01;
      double t4[4];
      for (i = k; i+3 < n; i += 4) {
         a01 = _mm_add_pd(a01, _mm_mul_pd(_mm_loadu_pd(x+i), _mm_loadu_pd(x+i-k)));
         a23 = _mm_add_pd(a23, _mm_mul_pd(_mm_loadu_pd(x+i+2), _mm_loadu_pd(x+i+2-k)));
      }
      _mm_storeu_pd(t4, a01);
      _mm_storeu_pd(t4+2, a23);
      s0 = t4[0];
      s1 = t4[1];
      s2 = t4[2];
      s3 = t4[3];
#else
      for (i = k; i+3 < n; i += 4) {
         s0 += x[i]*x[i-k];
         s1 += x[i+1]*x[i+1-k];
         s2 += x[i+2]*x[i+2-k];
         s3 += x[i+3]*x[i+3-k];
      }
#endif
      for (; i < n; i++) s0 += x[i]*x[i-k];
      R[k] = (s0+s1)+(s2+s3);
   }
}

/* Solves the weighted least squares normal equations for one order by Cholesky.
   phi: order x order matrix (row stride FL_FLAC_MAX_ORDER), psi: right side. */
static int fl_cholesky_solve(double phi[][FL_FLAC_MAX_ORDER], const double *psi, int order, double *a) {
   double L[FL_FLAC_MAX_ORDER][FL_FLAC_MAX_ORDER], y[FL_FLAC_MAX_ORDER], ridge = 0;
   int i, j, k;
   for (i = 0; i < order; i++) ridge += phi[i][i];
   ridge = ridge/order*1e-9+1e-9;
   for (i = 0; i < order; i++) {
      for (j = 0; j <= i; j++) {
         double s = phi[i][j]+(i == j ? ridge : 0);
         for (k = 0; k < j; k++) s -= L[i][k]*L[j][k];
         if (i == j) {
            if (!(s > 0)) return 0;
            L[i][i] = sqrt(s);
         } else L[i][j] = s/L[j][j];
      }
   }
   for (i = 0; i < order; i++) {
      double s = psi[i];
      for (k = 0; k < i; k++) s -= L[i][k]*y[k];
      y[i] = s/L[i][i];
   }
   for (i = order-1; i >= 0; i--) {
      double s = y[i];
      for (k = i+1; k < order; k++) s -= L[k][i]*a[k];
      a[i] = s/L[i][i];
   }
   return 1;
}

/* Weighted covariance-method LPC of the given order (weights wt). */
static int fl_lpc_ls(const double *x, const double *wt, int n, int order, double *a) {
   double phi[FL_FLAC_MAX_ORDER][FL_FLAC_MAX_ORDER], psi[FL_FLAC_MAX_ORDER];
   int i, j, t;
   if (n <= order*2) return 0;
   for (i = 0; i < order; i++) {
      psi[i] = 0;
      for (j = 0; j <= i; j++) phi[i][j] = 0;
   }
   for (t = order; t < n; t++) {
      double w = wt[t];
      const double *h = x+t-1;
      for (i = 0; i < order; i++) {
         double wi = w*h[-i];
         psi[i] += wi*x[t];
         for (j = 0; j <= i; j++) phi[i][j] += wi*h[-j];
      }
   }
   for (i = 0; i < order; i++)
      for (j = i+1; j < order; j++) phi[i][j] = phi[j][i];
   return fl_cholesky_solve(phi, psi, order, a);
}

/* Unweighted covariance method for every order up to P: phi_ij = sum x[t-1-i]x[t-1-j],
   t = P..n-1, built from its first row with the shift recursion. a[o-1] gets order o.
   Returns the highest order solved. */
static int fl_lpc_ls_all(const double *x, int n, int P, double a[][FL_FLAC_MAX_ORDER]) {
   double phi[FL_FLAC_MAX_ORDER][FL_FLAC_MAX_ORDER], psi[FL_FLAC_MAX_ORDER];
   int i, j, t, o, got = 0;
   if (n <= P*2) return 0;
   for (j = 0; j < P; j++) {
      double s0 = 0, s1 = 0;
      for (t = P; t < n; t++) {
         s0 += x[t-1]*x[t-1-j];
         s1 += x[t]*x[t-1-j];
      }
      phi[0][j] = s0;
      psi[j] = s1;
   }
   for (i = 1; i < P; i++)
      for (j = i; j < P; j++)
         phi[i][j] = phi[i-1][j-1]+x[P-1-i]*x[P-1-j]-x[n-1-i]*x[n-1-j];
   for (i = 0; i < P; i++)
      for (j = 0; j < i; j++) phi[i][j] = phi[j][i];
   for (o = 1; o <= P; o++)
      if (fl_cholesky_solve(phi, psi, o, a[o-1])) got = o;
      else break;
   return got;
}

/* ---- Subframe search ---- */
typedef struct {
   const fl_flac_cfg *cfg;
   fl_ws *ws;
   fl_wcache *wc;
   int bps, n;
   const fl_i32 *x;
   fl_u64 best_bits;   /* estimated */
   fl_sf best;
   int cap;            /* LPC estimates are only compared with best_bits (see fl_est_limit) */
} fl_search;

/* Highest coefficient precision that keeps 32-bit decoding for <= 17 bits (libFLAC rule). */
static int fl_max_prec(int bps, int order) {
   int p = 15;
   if (bps <= 17) p = FL_MIN(p, 32-bps-fl_bitlen(order)+1);
   return FL_MAX(p, 5);
}

static int fl_default_prec(int n) {
   return n <= 192 ? 7 : n <= 384 ? 8 : n <= 576 ? 9 : n <= 1152 ? 10 : n <= 2304 ? 11 : n <= 4608 ? 12 : 13;
}

/* Evaluates quantized coefficients; keeps them if they beat the best estimate. */
static fl_u64 fl_try_lpc(fl_search *s, const fl_i32 *q, int order, int prec, int shift) {
   fl_ws *w = s->ws;
   fl_u64 bits = 8+(fl_u64)order*s->bps+4+5+(fl_u64)order*prec;
   fl_lpc_sums(s->x, w->xd, w->x16, w->x16l, s->n, q, order, fl_lpc_mode(s->bps, prec, order), w->acc);
   if (!fl_lpc_residual(s->x, s->n, order, shift, fl_lpc_mode(s->bps, prec, order), w->acc, w->u)) return ~(fl_u64)0;
   bits += fl_rice_estimate(&w->rws, w->u, s->n, order, s->cfg->max_porder, s->bps > 16 ? 30 : 14,
                            s->cap ? fl_est_limit(s->best_bits, bits) : ~(fl_u64)0);
   if (bits < s->best_bits) {
      s->best_bits = bits;
      s->best.type = 3;
      s->best.order = order;
      s->best.prec = prec;
      s->best.shift = shift;
      memcpy(s->best.coef, q, sizeof(fl_i32)*order);
   }
   return bits;
}

/* Quantizes at the default precision and nearby ones and evaluates. */
static fl_u64 fl_try_lpc_float(fl_search *s, const double *lp, int order, int range) {
   int p0 = FL_MIN(fl_default_prec(s->n), fl_max_prec(s->bps, order)), lo, hi, prec;
   fl_u64 best = ~(fl_u64)0;
   int pmax = fl_max_prec(s->bps, order);
   lo = range >= 99 ? 5 : FL_MAX(5, p0-range);
   hi = range >= 99 ? pmax : FL_MIN(pmax, p0+range);
   for (prec = lo; prec <= hi; prec++) {
      fl_i32 q[FL_FLAC_MAX_ORDER];
      int shift;
      fl_u64 b;
      if (!fl_quantize(lp, order, prec, q, &shift)) continue;
      b = fl_try_lpc(s, q, order, prec, shift);
      if (b < best) best = b;
   }
   return best;
}

/* Hill climb on the quantized coefficients of the current best LPC candidate. */
static void fl_climb(fl_search *s, int passes) {
   fl_ws *w = s->ws;
   int order = s->best.order, prec = s->best.prec, shift = s->best.shift, n = s->n, pass, j, d;
   int qmax, qmin, kmax = s->bps > 16 ? 30 : 14;
   fl_i32 q[FL_FLAC_MAX_ORDER];
   const fl_i32 *x = s->x;
   fl_u64 cur;
   if (s->best.type != 3) return;
   qmax = (1<<(prec-1))-1;
   qmin = -qmax-1;
   memcpy(q, s->best.coef, sizeof(q[0])*order);
   fl_lpc_sums(x, w->xd, w->x16, w->x16l, n, q, order, fl_lpc_mode(s->bps, prec, order), w->acc);
   cur = s->best_bits;
   for (pass = 0; pass < passes; pass++) {
      int improved = 0;
      for (j = 0; j < order; j++) {
         for (d = -1; d <= 1; d += 2) {
            int i;
            fl_i64 bad = 0;
            fl_u64 bits;
            if (q[j]+d > qmax || q[j]+d < qmin) continue;
            for (i = order; i < n; i++) {
               fl_i64 v = x[i]-((w->acc[i]+(fl_i64)d*x[i-1-j])>>shift);
               bad |= (v+0x7FFFFFFF)>>32;
               w->u[i-order] = fl_zz((fl_i32)v);
            }
            if (bad) continue;
            bits = 8+(fl_u64)order*s->bps+4+5+(fl_u64)order*prec;
            bits += fl_rice_estimate(&w->rws, w->u, n, order, s->cfg->max_porder, kmax, fl_est_limit(cur, bits));
            if (bits < cur) {
               cur = bits;
               q[j] += d;
               for (i = order; i < n; i++) w->acc[i] += (fl_i64)d*x[i-1-j];
               improved = 1;
               break;
            }
         }
      }
      if (!improved) break;
   }
   if (cur < s->best_bits) {
      s->best_bits = cur;
      memcpy(s->best.coef, q, sizeof(q[0])*order);
   }
}

/* Partition-aware IRLS: weights 1/(|e| * mean |e| of its neighbourhood) approximate the
   Rice cost, starting from the given coefficients. */
static void fl_irls(fl_search *s, const double *start, int order, int iters) {
   fl_ws *w = s->ws;
   int n = s->n, it, i, j, seg = FL_MAX(32, n>>4);
   double a[FL_FLAC_MAX_ORDER];
   memcpy(a, start, sizeof(double)*order);
   for (it = 0; it < iters; it++) {
      /* residual of the float predictor */
      for (i = order; i < n; i++) {
         double p = 0;
         for (j = 0; j < order; j++) p += a[j]*w->xd[i-1-j];
         w->e[i] = fabs(w->xd[i]-p);
      }
      for (i = order; i < n; i += seg) {
         int end = FL_MIN(n, i+seg), k;
         double m = 0;
         for (k = i; k < end; k++) m += w->e[k];
         m = m/(end-i)+0.5;
         for (k = i; k < end; k++) w->wt[k] = 1.0/((w->e[k]+0.5)*m);
      }
      if (!fl_lpc_ls(w->xd, w->wt, n, order, a)) return;
      fl_try_lpc_float(s, a, order, s->cfg->prec_range >= 99 ? 99 : 1);
   }
}

/* Full search for one channel of one block. Writes the best subframe to *out (exact bits).
   fu: the segment's fixed residuals of this channel (see fl_flac_sink) or NULL; the block
   starts at segment position off. */
static void fl_search_subframe(const fl_flac_cfg *cfg, fl_ws *ws, fl_wcache *wc, const fl_i32 *x0, int n, int bps,
                               fl_u32 *const *fu, int off, fl_sf *out) {
   fl_search s;
   fl_i32 orv = 0, *x = ws->x;
   int i, wasted = 0, order, max_order;
   fl_u64 bits;
   for (i = 1; i < n && x0[i] == x0[0]; i++);
   if (i == n) {
      out->type = 0;
      out->wasted = 0;
      out->bits = 8+bps;
      out->coef[0] = x0[0];
      return;
   }
   i = 0;
#ifdef FL_FLAC_SSE2
   {
      __m128i vo = _mm_setzero_si128();
      for (; i+3 < n; i += 4) vo = _mm_or_si128(vo, _mm_loadu_si128((const __m128i*)(x0+i)));
      vo = _mm_or_si128(vo, _mm_shuffle_epi32(vo, _MM_SHUFFLE(1, 0, 3, 2)));
      vo = _mm_or_si128(vo, _mm_shuffle_epi32(vo, _MM_SHUFFLE(2, 3, 0, 1)));
      orv = _mm_cvtsi128_si32(vo);
   }
#endif
   for (; i < n; i++) orv |= x0[i];
   while (!(orv&1) && wasted < bps-1) {
      orv >>= 1;
      wasted++;
   }
   i = 0;
#ifdef FL_FLAC_SSE2
   {
      __m128i sh = _mm_cvtsi32_si128(wasted);
      for (; i+3 < n; i += 4) {
         __m128i v = _mm_sra_epi32(_mm_loadu_si128((const __m128i*)(x0+i)), sh);
         _mm_storeu_si128((__m128i*)(x+i), v);
         _mm_storeu_pd(ws->xd+i, _mm_cvtepi32_pd(v));
         _mm_storeu_pd(ws->xd+i+2, _mm_cvtepi32_pd(_mm_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2))));
      }
   }
#endif
   for (; i < n; i++) {
      x[i] = x0[i]>>wasted;
      ws->xd[i] = x[i];
   }
   bps -= wasted;
   fl_fill_x16(ws, x, n, bps);
   memset(&s, 0, sizeof(s));
   s.cfg = cfg;
   s.ws = ws;
   s.wc = wc;
   s.bps = bps;
   s.n = n;
   s.x = x;
   /* the LPC candidates' own estimates matter only for the refinements */
   s.cap = !(cfg->ls || cfg->prec_range > 1 || cfg->irls || cfg->climb);
   /* verbatim */
   s.best.type = 1;
   s.best_bits = 8+(fl_u64)n*bps;
   /* fixed predictors */
   for (order = 0; order <= 4 && order < n; order++) {
      const fl_u32 *u = ws->u;
      if (fu && !wasted) u = fu[order]+off;
      else if (!fl_fixed_residual(x, n, order, bps, ws->res, ws->u)) continue;
      bits = 8+(fl_u64)order*bps;
      bits += fl_rice_estimate(&ws->rws, u, n, order, cfg->max_porder, bps > 16 ? 30 : 14, fl_est_limit(s.best_bits, bits));
      if (bits < s.best_bits) {
         s.best_bits = bits;
         s.best.type = 2;
         s.best.order = order;
      }
   }
   /* LPC */
   max_order = FL_MIN(cfg->max_order, n/2-1);
   if (max_order >= 1) {
      double R[FL_FLAC_MAX_ORDER+1], lpc[FL_FLAC_MAX_ORDER][FL_FLAC_MAX_ORDER], err[FL_FLAC_MAX_ORDER+1];
      double best_lp[FL_FLAC_MAX_ORDER];
      int win, best_order = 0;
      fl_u64 best_lp_bits = ~(fl_u64)0;
      for (win = 0; win < cfg->nwin; win++) {
         const double *wv = fl_window_get(wc, n, win);
         int got, o;
         if (!wv) continue;
         i = 0;
#ifdef FL_FLAC_SSE2
         for (; i+1 < n; i += 2) _mm_storeu_pd(ws->wx+i, _mm_mul_pd(_mm_loadu_pd(ws->xd+i), _mm_loadu_pd(wv+i)));
#endif
         for (; i < n; i++) ws->wx[i] = ws->xd[i]*wv[i];
         fl_autocorr(ws->wx, n, max_order, R);
         R[0] *= 1.0+1e-10;
         got = fl_levinson(R, max_order, lpc, err);
         if (got < 1) continue;
         if (cfg->all_orders) {
            for (o = 1; o <= got; o++) {
               fl_u64 b = fl_try_lpc_float(&s, lpc[o-1], o, FL_MIN(cfg->prec_range, 1));
               if (b < best_lp_bits) {
                  best_lp_bits = b;
                  best_order = o;
                  memcpy(best_lp, lpc[o-1], sizeof(double)*o);
               }
            }
         } else {
            /* the orders with the lowest estimated cost from the prediction error */
            double est[FL_FLAC_MAX_ORDER+1];
            int pick[3] = {0, 0, 0}, k;
            for (o = 1; o <= got; o++) {
               double e = err[o] > 0 ? err[o]/n : 1e-9;
               est[o] = (n-o)*0.5*log(e+1e-9)/log(2.0)+o*(bps+fl_default_prec(n));
            }
            for (k = 0; k < 3; k++) {
               double bv = 1e300;
               for (o = 1; o <= got; o++)
                  if (o != pick[0] && o != pick[1] && est[o] < bv) {
                     bv = est[o];
                     pick[k] = o;
                  }
            }
            for (k = 0; k < 3; k++) {
               fl_u64 b;
               if (!pick[k]) continue;
               b = fl_try_lpc_float(&s, lpc[pick[k]-1], pick[k], FL_MIN(cfg->prec_range, 1));
               if (b < best_lp_bits) {
                  best_lp_bits = b;
                  best_order = pick[k];
                  memcpy(best_lp, lpc[pick[k]-1], sizeof(double)*pick[k]);
               }
            }
         }
      }
      if (cfg->ls && n > 64) {
         /* exact least squares for the best order and its neighbours (all orders if asked) */
         int lo = FL_MAX(1, (best_order ? best_order : max_order)-2), hi = FL_MIN(max_order, (best_order ? best_order : max_order)+2), o, got;
         double als[FL_FLAC_MAX_ORDER][FL_FLAC_MAX_ORDER];
         if (cfg->all_orders) {
            lo = 1;
            hi = max_order;
         }
         got = fl_lpc_ls_all(ws->xd, n, hi, als);
         for (o = lo; o <= got; o++) {
            double *a = als[o-1];
            fl_u64 b;
            b = fl_try_lpc_float(&s, a, o, FL_MIN(cfg->prec_range, 1));
            if (b < best_lp_bits) {
               best_lp_bits = b;
               best_order = o;
               memcpy(best_lp, a, sizeof(double)*o);
            }
         }
      }
      s.cap = 1;
      if (best_order) {
         if (cfg->prec_range > 1) fl_try_lpc_float(&s, best_lp, best_order, cfg->prec_range);
         if (cfg->irls && n > 64) fl_irls(&s, best_lp, best_order, cfg->irls);
         if (cfg->climb) fl_climb(&s, cfg->climb);
      }
   }
   /* exact size of the winner */
   *out = s.best;
   out->wasted = wasted;
   if (cfg->estimate) out->bits = s.best_bits;
   else if (s.best.type == 1) out->bits = 8+(fl_u64)n*bps;
   else {
      int ord = s.best.order;
      if (s.best.type == 2) fl_fixed_residual(x, n, ord, bps, ws->res, ws->u);
      else {
         fl_lpc_sums(x, ws->xd, ws->x16, ws->x16l, n, s.best.coef, ord, fl_lpc_mode(bps, s.best.prec, ord), ws->acc);
         fl_lpc_residual(x, n, ord, s.best.shift, fl_lpc_mode(bps, s.best.prec, ord), ws->acc, ws->u);
      }
      out->bits = 8+(fl_u64)ord*bps+(s.best.type == 3 ? 9+(fl_u64)ord*s.best.prec : 0)+
                  fl_rice_exact(&ws->plan, ws->ktmp, ws->u, n, ord, cfg->max_porder, bps > 14);
      if (out->bits >= 8+(fl_u64)n*bps) {
         out->type = 1;
         out->bits = 8+(fl_u64)n*bps;
      }
   }
   if (wasted) out->bits += wasted;
}

/* Writes a subframe chosen by fl_search_subframe. */
static void fl_write_subframe(fl_bw *w, const fl_flac_cfg *cfg, fl_ws *ws, const fl_i32 *x0, int n, int bps, const fl_sf *sf) {
   int i, wasted = sf->wasted, order = sf->order;
   fl_i32 *x = ws->x;
   if (sf->type == 0) {
      fl_bw_put(w, 0, 8);
      fl_bw_put_signed(w, sf->coef[0], bps);
      return;
   }
   for (i = 0; i < n; i++) x[i] = x0[i]>>wasted;
   bps -= wasted;
   fl_bw_put(w, sf->type == 1 ? 1 : sf->type == 2 ? 8+order : 31+order, 7);
   if (wasted) {
      fl_bw_put(w, 1, 1);
      fl_bw_zeros(w, (fl_u32)wasted-1);
      fl_bw_put(w, 1, 1);
   } else fl_bw_put(w, 0, 1);
   if (sf->type == 1) {
      for (i = 0; i < n; i++) fl_bw_put_signed(w, x[i], bps);
      return;
   }
   for (i = 0; i < order; i++) fl_bw_put_signed(w, x[i], bps);
   if (sf->type == 2) fl_fixed_residual(x, n, order, bps, ws->res, ws->u);
   else {
      fl_bw_put(w, sf->prec-1, 4);
      fl_bw_put(w, sf->shift, 5);
      for (i = 0; i < order; i++) fl_bw_put_signed(w, sf->coef[i], sf->prec);
      for (i = 0; i < n; i++) ws->xd[i] = x[i];
      fl_fill_x16(ws, x, n, bps);
      fl_lpc_sums(x, ws->xd, ws->x16, ws->x16l, n, sf->coef, order, fl_lpc_mode(bps, sf->prec, order), ws->acc);
      fl_lpc_residual(x, n, order, sf->shift, fl_lpc_mode(bps, sf->prec, order), ws->acc, ws->u);
      for (i = order; i < n; i++) ws->res[i] = (fl_i32)(x[i]-(ws->acc[i]>>sf->shift));
   }
   fl_rice_exact(&ws->plan, ws->ktmp, ws->u, n, order, cfg->max_porder, bps > 14);
   fl_rice_write(w, &ws->plan, ws->res, n, order);
}

/* ---- Quick cost estimate for the block size search ----
   Rectangular-window autocorrelations of any block come from prefix sums over the
   segment: P[k][i] = sum over m < i of y[m]*y[m-k], y = x scaled to at most 20 bits. */
#define FL_DP_ORDER 12

typedef struct {
   fl_i64 *P[FL_DP_ORDER+1];
   int cap;
} fl_prefix;

static int fl_prefix_build(fl_prefix *pf, const fl_i32 *x, int len, int bps) {
   int k, i, sh = bps > 20 ? bps-20 : 0;
   if (len+1 > pf->cap) {
      for (k = 0; k <= FL_DP_ORDER; k++) {
         free(pf->P[k]);
         if (!(pf->P[k] = (fl_i64*)malloc(sizeof(fl_i64)*(len+1)))) return -1;
      }
      pf->cap = len+1;
   }
   for (k = 0; k <= FL_DP_ORDER; k++) {
      fl_i64 *P = pf->P[k], acc = 0;
      for (i = 0; i <= k && i <= len; i++) P[i] = 0;
      for (i = k; i < len; i++) {
         acc += (fl_i64)(x[i]>>sh)*(x[i-k]>>sh);
         P[i+1] = acc;
      }
   }
   return 0;
}

static void fl_prefix_free(fl_prefix *pf) {
   int k;
   for (k = 0; k <= FL_DP_ORDER; k++) free(pf->P[k]);
}

static fl_u64 fl_estimate_channel(const fl_flac_cfg *cfg, fl_ws *ws, const fl_prefix *pf, const fl_i32 *seg,
                                  fl_u32 *const *fu, int off, int n, int bps) {
   const fl_i32 *x = seg+off;
   fl_u64 best = 8+(fl_u64)n*bps, b;
   int i, max_order = FL_MIN(FL_MIN(cfg->max_order, FL_DP_ORDER), n/2-1);
   int kmax = bps > 16 ? 30 : 14;
   for (i = 1; i < n && x[i] == x[0]; i++);
   if (i == n) return 8+bps;
   if (fu || fl_fixed_residual(x, n, 2, bps, ws->res, ws->u)) {
      b = 8+2*(fl_u64)bps;
      b += fl_rice_estimate(&ws->rws, fu ? fu[2]+off : ws->u, n, 2, cfg->max_porder, kmax, fl_est_limit(best, b));
      if (b < best) best = b;
   }
   if (max_order >= 1) {
      double R[FL_FLAC_MAX_ORDER+1], lpc[FL_FLAC_MAX_ORDER][FL_FLAC_MAX_ORDER], err[FL_FLAC_MAX_ORDER+1], bv = 1e300;
      int got, o, k, bo = 0;
      for (k = 0; k <= max_order; k++) R[k] = (double)(pf->P[k][off+n]-pf->P[k][off+k]);
      R[0] = R[0]*(1.0+1e-9)+1e-9;
      got = fl_levinson(R, max_order, lpc, err);
      for (o = 1; o <= got; o++) {
         double e = err[o] > 0 ? err[o]/n : 1e-9;
         double est = (n-o)*0.5*log(e+1e-9)/log(2.0)+o*(bps+fl_default_prec(n));
         if (est < bv) {
            bv = est;
            bo = o;
         }
      }
      if (bo) {
         fl_search s;
         memset(&s, 0, sizeof(s));
         s.cfg = cfg;
         s.ws = ws;
         s.bps = bps;
         s.n = n;
         s.x = x;
         s.best_bits = best;
         s.cap = 1;
         for (i = 0; i < n; i++) ws->xd[i] = x[i];
         fl_fill_x16(ws, x, n, bps);
         fl_try_lpc_float(&s, lpc[bo-1], bo, 0);
         best = s.best_bits;
      }
   }
   return best;
}

/* ---- Encoder sink ---- */

/* Per-thread state. */
typedef struct {
   fl_ws ws;
   fl_wcache wc;
   fl_flac_dec dec;   /* verification */
   fl_bw bw;          /* frames encoded by this thread in the current segment */
} fl_worker;

/* A frame of the current segment. */
typedef struct {
   int off, len, worker, err;
   size_t pos, bytes;  /* location in the worker's bit writer */
} fl_frame_job;

typedef struct {
   fl_sink base;
   FILE *f;
   fl_flac_cfg cfg;
   fl_flac_cfg dcfg;    /* search used for block costs when cfg.dp_search > 0 */
   int level, lax, nthreads;
   int C, bits, rate, variable, stereo;
   fl_i32 *buf[8];      /* deinterleaved input not yet encoded */
   int have;            /* samples per channel in buf */
   fl_i32 *side, *mid;
   int *side_bad;     /* 32-bit audio: side samples beyond 32 bits before each position */
   fl_u32 *fu[8][5];    /* zigzag fixed residuals of the segment per source and order: fu[v][o][i-o]
                           for sample i >= o; computed once instead of per block (bits <= 26) */
   fl_i32 *fres;
   int fu_ok;
   fl_u64 sample_pos, frame_no;
   fl_u32 min_frame, max_frame;
   int min_block, max_block, last_block;
   fl_md5 md5;
   long info_pos;
   fl_worker *wk;
   fl_u64 *cost;        /* block costs: cost[i*K+k-1] for k units starting at unit i */
   fl_u64 *dp_cost;
   int *dp_from;
   fl_prefix pf[8];
   fl_frame_job *jobs;
   int njobs, seg_len;
} fl_flac_sink;

static void fl_flac_streaminfo(fl_flac_sink *s, unsigned char *p, fl_u64 total, const unsigned char *md5) {
   int minb = s->min_block ? s->min_block : s->max_block;
   p[0] = (unsigned char)(minb>>8);
   p[1] = (unsigned char)minb;
   p[2] = (unsigned char)(s->max_block>>8);
   p[3] = (unsigned char)s->max_block;
   p[4] = (unsigned char)(s->min_frame>>16);
   p[5] = (unsigned char)(s->min_frame>>8);
   p[6] = (unsigned char)s->min_frame;
   p[7] = (unsigned char)(s->max_frame>>16);
   p[8] = (unsigned char)(s->max_frame>>8);
   p[9] = (unsigned char)s->max_frame;
   p[10] = (unsigned char)(s->rate>>12);
   p[11] = (unsigned char)(s->rate>>4);
   p[12] = (unsigned char)((s->rate&15)<<4|(s->C-1)<<1|((s->bits-1)>>4));
   p[13] = (unsigned char)(((s->bits-1)&15)<<4|(int)((total>>32)&15));
   p[14] = (unsigned char)(total>>24);
   p[15] = (unsigned char)(total>>16);
   p[16] = (unsigned char)(total>>8);
   p[17] = (unsigned char)total;
   memcpy(p+18, md5, 16);
}

static void fl_put_be24(unsigned char *p, fl_u32 v) {
   p[0] = (unsigned char)(v>>16);
   p[1] = (unsigned char)(v>>8);
   p[2] = (unsigned char)v;
}

/* What happens to a source tag in the FLAC file: 0 dropped, 1 comment, 2 PICTURE block.
   Tags describing an Opus encoding (encoder, R128 gains already applied) are dropped. */
static int fl_flac_tag_kind(const fl_format *f, const char *t) {
   if (strncmp(t, "METADATA_BLOCK_PICTURE=", 23) == 0) return 2;
   if (f->opus_family >= 0 && (fl_tag_is(t, "ENCODER") || fl_tag_is(t, "ENCODER_OPTIONS") ||
                               fl_tag_is(t, "R128_TRACK_GAIN") || fl_tag_is(t, "R128_ALBUM_GAIN"))) return 0;
   return 1;
}

/* Writes the metadata blocks: STREAMINFO (patched at the end), VORBIS_COMMENT (only if
   there are tags), PICTURE and other kept blocks. */
static int fl_flac_metadata_out(fl_flac_sink *s, const fl_format *f) {
   unsigned char hdr[4+4+34], zero[16];
   int i, npic = 0, ntag = 0, nblk = f->nblocks, last;
   char mask_tag[64];
   mask_tag[0] = 0;
   if (f->channel_mask) sprintf(mask_tag, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK=0x%04X", (unsigned)f->channel_mask);
   for (i = 0; i < f->ntags; i++) {
      int k = fl_flac_tag_kind(f, f->tags[i]);
      npic += k == 2;
      ntag += k == 1;
   }
   if (mask_tag[0]) ntag++;
   memcpy(hdr, "fLaC", 4);
   last = !ntag && !npic && !nblk;
   hdr[4] = (unsigned char)(last ? 0x80 : 0);
   fl_put_be24(hdr+5, 34);
   memset(zero, 0, 16);
   fl_flac_streaminfo(s, hdr+8, s->f == stdout && fl_exact_frames(f) > 0 ? (fl_u64)f->frames : 0, zero);
   if (fwrite(hdr, 1, sizeof(hdr), s->f) != sizeof(hdr)) return FL_ERR_IO;
   s->info_pos = 8;
   s->base.out_bytes = sizeof(hdr);
   if (ntag) {
      const char *vendor = "flapus";
      unsigned char n4[4];
      fl_u32 len = 4+(fl_u32)strlen(vendor)+4;
      for (i = 0; i < f->ntags; i++)
         if (fl_flac_tag_kind(f, f->tags[i]) == 1) len += 4+(fl_u32)strlen(f->tags[i]);
      if (mask_tag[0]) len += 4+(fl_u32)strlen(mask_tag);
      last = !npic && !nblk;
      n4[0] = (unsigned char)(4|(last ? 0x80 : 0));
      fl_put_be24(n4+1, len);
      s->base.out_bytes += 4+len;
      fwrite(n4, 1, 4, s->f);
      fl_put_le32(n4, (fl_u32)strlen(vendor));
      fwrite(n4, 1, 4, s->f);
      fwrite(vendor, 1, strlen(vendor), s->f);
      fl_put_le32(n4, ntag);
      fwrite(n4, 1, 4, s->f);
      for (i = 0; i < f->ntags; i++)
         if (fl_flac_tag_kind(f, f->tags[i]) == 1) {
            fl_put_le32(n4, (fl_u32)strlen(f->tags[i]));
            fwrite(n4, 1, 4, s->f);
            fwrite(f->tags[i], 1, strlen(f->tags[i]), s->f);
         }
      if (mask_tag[0]) {
         fl_put_le32(n4, (fl_u32)strlen(mask_tag));
         fwrite(n4, 1, 4, s->f);
         fwrite(mask_tag, 1, strlen(mask_tag), s->f);
      }
   }
   for (i = 0; i < f->ntags; i++)
      if (strncmp(f->tags[i], "METADATA_BLOCK_PICTURE=", 23) == 0) {
         const char *b64 = f->tags[i]+23;
         int l = (int)strlen(b64), got;
         unsigned char *pic = (unsigned char*)malloc(l/4*3+3), h4[4];
         if (!pic) return FL_ERR_ALLOC;
         got = fl_base64_decode(b64, l, pic);
         npic--;
         if (got > 0 && got < (1<<24)) {
            h4[0] = (unsigned char)(6|(!npic && !nblk ? 0x80 : 0));
            fl_put_be24(h4+1, (fl_u32)got);
            fwrite(h4, 1, 4, s->f);
            fwrite(pic, 1, got, s->f);
            s->base.out_bytes += 4+got;
         }
         free(pic);
      }
   for (i = 0; i < nblk; i++) {
      unsigned char h4[4];
      h4[0] = (unsigned char)(f->blocks[i].type|(i == nblk-1 ? 0x80 : 0));
      fl_put_be24(h4+1, (fl_u32)f->blocks[i].len);
      fwrite(h4, 1, 4, s->f);
      fwrite(f->blocks[i].data, 1, f->blocks[i].len, s->f);
      s->base.out_bytes += 4+f->blocks[i].len;
   }
   return ferror(s->f) ? FL_ERR_IO : FL_OK;
}

static int fl_flacenc_start(fl_sink *sk, const fl_format *f) {
   fl_flac_sink *s = (fl_flac_sink*)sk;
   int c, t, slen;
   if (f->is_float) return FL_ERR_UNSUPPORTED;  /* FLAC is integer PCM only */
   if (f->channels < 1 || f->channels > 8 || f->bits < 4 || f->bits > 32 || f->rate < 1 || f->rate > FL_MAX_RATE) return FL_ERR_UNSUPPORTED;
   s->C = f->channels;
   s->bits = f->bits;
   s->rate = f->rate;
   s->stereo = s->C == 2;
   fl_flac_level(&s->cfg, s->level, s->lax, s->rate);
   if (s->cfg.dp_search) {
      fl_flac_level(&s->dcfg, 0, s->lax, s->rate);
      s->dcfg.max_porder = s->cfg.max_porder;
      s->dcfg.max_order = s->cfg.max_order;
      s->dcfg.estimate = 1;
   }
   s->variable = s->cfg.unit > 0;
   if (s->cfg.unit) {
      int M = FL_FLAC_SEGMENT/s->cfg.unit+2, K = s->cfg.max_block/s->cfg.unit;
      s->dp_cost = (fl_u64*)malloc(sizeof(fl_u64)*(M+1));
      s->dp_from = (int*)malloc(sizeof(int)*(M+1));
      s->cost = (fl_u64*)malloc(sizeof(fl_u64)*M*K);
      if (!s->dp_cost || !s->dp_from || !s->cost) return FL_ERR_ALLOC;
   }
   s->jobs = (fl_frame_job*)malloc(sizeof(fl_frame_job)*(FL_FLAC_SEGMENT/16+2));
   s->wk = (fl_worker*)calloc(s->nthreads, sizeof(fl_worker));
   if (!s->jobs || !s->wk) return FL_ERR_ALLOC;
   fl_md5_init(&s->md5);
   slen = FL_FLAC_SEGMENT+s->cfg.max_block+FL_SLACK;
   for (c = 0; c < s->C; c++)
      if (!(s->buf[c] = (fl_i32*)calloc(slen, sizeof(fl_i32)))) return FL_ERR_ALLOC;
   s->side = (fl_i32*)calloc(slen, sizeof(fl_i32));
   s->mid = (fl_i32*)calloc(slen, sizeof(fl_i32));
   if (!s->side || !s->mid) return FL_ERR_ALLOC;
   if (s->stereo && s->bits == 32 && !(s->side_bad = (int*)calloc(slen+1, sizeof(int)))) return FL_ERR_ALLOC;
   s->fu_ok = s->bits <= 26;
   if (s->fu_ok) {
      int ns = s->stereo ? 4 : s->C, o;
      if (!(s->fres = (fl_i32*)malloc(sizeof(fl_i32)*slen))) return FL_ERR_ALLOC;
      for (c = 0; c < ns; c++)
         for (o = 0; o < 5; o++)
            if (!(s->fu[c][o] = (fl_u32*)malloc(sizeof(fl_u32)*slen))) return FL_ERR_ALLOC;
   }
   for (t = 0; t < s->nthreads; t++) {
      fl_worker *w = &s->wk[t];
      w->dec.si.channels = s->C;
      w->dec.si.bits = s->bits;
      w->dec.si.rate = s->rate;
      if (fl_ws_alloc(&w->ws, s->cfg.max_block, s->cfg.max_porder) < 0 || fl_flac_dec_alloc(&w->dec, s->cfg.max_block) < 0)
         return FL_ERR_ALLOC;
   }
   if (s->f == stdout) {
      /* stdout cannot seek back: STREAMINFO is final now, with the block size limits
         and unknown frame sizes and MD5 (like flac) */
      s->min_block = s->cfg.unit ? s->cfg.unit : s->cfg.fixed_block;
      s->max_block = s->cfg.unit ? s->cfg.max_block : s->cfg.fixed_block;
   } else s->min_frame = 0xFFFFFF;
   return fl_flac_metadata_out(s, f);
}

/* Frame header for frame/sample number num; returns its length in bytes. */
static int fl_frame_header(fl_flac_sink *s, unsigned char *h, int bs, int assignment, fl_u64 num) {
   static const int rate_codes[12] = {0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000};
   int pos = 4, bcode, rcode = 0, scode, i, extra_b = 0, extra_r = 0;
   if (bs == 192) bcode = 1;
   else if (bs == 576 || bs == 1152 || bs == 2304 || bs == 4608) bcode = 2+fl_ilog(bs/576)-1;
   else if (bs >= 256 && bs <= 32768 && (bs&(bs-1)) == 0) bcode = 8+fl_ilog(bs/256)-1;
   else if (bs <= 256) { bcode = 6; extra_b = 1; }
   else { bcode = 7; extra_b = 2; }
   for (i = 1; i < 12; i++)
      if (rate_codes[i] == s->rate) rcode = i;
   if (!rcode) {
      if (s->rate%1000 == 0 && s->rate/1000 <= 255) { rcode = 12; extra_r = 1; }
      else if (s->rate <= 65535) { rcode = 13; extra_r = 2; }
      else if (s->rate%10 == 0 && s->rate/10 <= 65535) { rcode = 14; extra_r = 2; }
   }
   switch (s->bits) {
   case 8: scode = 1; break;
   case 12: scode = 2; break;
   case 16: scode = 4; break;
   case 20: scode = 5; break;
   case 24: scode = 6; break;
   case 32: scode = 7; break;
   default: scode = 0; break;
   }
   h[0] = 0xFF;
   h[1] = (unsigned char)(0xF8|s->variable);
   h[2] = (unsigned char)(bcode<<4|rcode);
   h[3] = (unsigned char)(assignment<<4|scode<<1);
   if (num < 0x80) h[pos++] = (unsigned char)num;
   else {
      int len = 1;
      while (len < 6 && num >= ((fl_u64)1<<(5*len+6))) len++;
      h[pos++] = (unsigned char)(((0xFF<<(7-len))&0xFF)|(num>>(6*len)));
      for (i = len-1; i >= 0; i--) h[pos++] = (unsigned char)(0x80|((num>>(6*i))&0x3F));
   }
   if (extra_b == 1) h[pos++] = (unsigned char)(bs-1);
   else if (extra_b == 2) {
      h[pos++] = (unsigned char)((bs-1)>>8);
      h[pos++] = (unsigned char)(bs-1);
   }
   if (extra_r == 1) h[pos++] = (unsigned char)(s->rate/1000);
   else if (extra_r == 2) {
      int v = rcode == 13 ? s->rate : s->rate/10;
      h[pos++] = (unsigned char)(v>>8);
      h[pos++] = (unsigned char)v;
   }
   h[pos] = (unsigned char)fl_crc8(h, pos);
   return pos+1;
}

/* Header bits for the block size search (sample number assumed 4 bytes). */
static int fl_frame_header_bits(int bs) {
   int extra = 0;
   if (!(bs == 192 || bs == 576 || bs == 1152 || bs == 2304 || bs == 4608 || (bs >= 256 && bs <= 32768 && (bs&(bs-1)) == 0)))
      extra = bs <= 256 ? 8 : 16;
   return (4+4+1)*8+extra+16+4;
}

#define FL_FU(s, v) ((s)->fu_ok ? (fl_u32 *const*)(s)->fu[v] : NULL)

/* Stereo decorrelation for the block: always below 32 bits; with 32-bit audio only if the
   33-bit side channel fits in 32 bits there. */
static int fl_use_stereo(const fl_flac_sink *s, int off, int bs) {
   return s->stereo && (!s->side_bad || s->side_bad[off+bs] == s->side_bad[off]);
}

/* Encodes one frame into the worker's bit writer and verifies it by decoding. */
static int fl_encode_frame(fl_flac_sink *s, fl_worker *wk, fl_frame_job *job, fl_u64 num) {
   fl_sf sf[8], sfx[4];
   fl_bw *w = &wk->bw;
   const fl_i32 *ch[8];
   int c, i, assignment, hl, n, off = job->off, bs = job->len;
   size_t start, flen;
   unsigned char h[16];
   for (c = 0; c < s->C; c++) ch[c] = s->buf[c]+off;
   assignment = s->C-1;
   if (fl_use_stereo(s, off, bs)) {
      /* compare left/right, left/side, side/right and mid/side */
      const fl_i32 *sd = s->side+off, *md = s->mid+off;
      fl_u64 costs[4];
      static const int asg[4] = {1, 8, 9, 10};
      int m = 0;
      fl_search_subframe(&s->cfg, &wk->ws, &wk->wc, ch[0], bs, s->bits, FL_FU(s, 0), off, &sfx[0]);
      fl_search_subframe(&s->cfg, &wk->ws, &wk->wc, ch[1], bs, s->bits, FL_FU(s, 1), off, &sfx[1]);
      fl_search_subframe(&s->cfg, &wk->ws, &wk->wc, sd, bs, s->bits+1, FL_FU(s, 2), off, &sfx[2]);
      fl_search_subframe(&s->cfg, &wk->ws, &wk->wc, md, bs, s->bits, FL_FU(s, 3), off, &sfx[3]);
      costs[0] = sfx[0].bits+sfx[1].bits;
      costs[1] = sfx[0].bits+sfx[2].bits;
      costs[2] = sfx[2].bits+sfx[1].bits;
      costs[3] = sfx[3].bits+sfx[2].bits;
      for (i = 1; i < 4; i++)
         if (costs[i] < costs[m]) m = i;
      assignment = asg[m];
      sf[0] = m == 2 ? sfx[2] : m == 3 ? sfx[3] : sfx[0];
      sf[1] = m == 0 || m == 2 ? sfx[1] : sfx[2];
      if (m == 2) ch[0] = sd;
      if (m == 3) ch[0] = md;
      if (m == 1 || m == 3) ch[1] = sd;
   } else {
      for (c = 0; c < s->C; c++) fl_search_subframe(&s->cfg, &wk->ws, &wk->wc, ch[c], bs, s->bits, FL_FU(s, c), off, &sf[c]);
   }
   hl = fl_frame_header(s, h, bs, assignment, num);
   {
      size_t bound = 16+2;
      for (c = 0; c < s->C; c++) bound += (size_t)bs*33/8+64;
      if (fl_bw_reserve(w, bound) < 0) return FL_ERR_ALLOC;
   }
   start = w->len;
   memcpy(w->buf+w->len, h, hl);
   w->len += hl;
   for (c = 0; c < s->C; c++) {
      int side = (assignment == 8 && c == 1) || (assignment == 9 && c == 0) || (assignment == 10 && c == 1);
      fl_write_subframe(w, &s->cfg, &wk->ws, ch[c], bs, s->bits+side, &sf[c]);
   }
   fl_bw_align(w);
   {
      unsigned crc = fl_crc16(w->buf+start, w->len-start);
      w->buf[w->len++] = (unsigned char)(crc>>8);
      w->buf[w->len++] = (unsigned char)crc;
   }
   flen = w->len-start;
   /* decode the frame again and compare with the input */
   if (fl_flac_decode_frame(&wk->dec, w->buf+start, flen, &n) != flen || n != bs) return FL_ERR_INTERNAL;
   for (c = 0; c < s->C; c++) {
      const fl_i32 *x = s->buf[c]+off;
      for (i = 0; i < bs; i++)
         if (wk->dec.out[i*s->C+c] != x[i]) return FL_ERR_INTERNAL;
   }
   job->pos = start;
   job->bytes = flen;
   return FL_OK;
}

/* Estimated cost of a block for the block size search. */
static fl_u64 fl_block_cost(fl_flac_sink *s, fl_worker *wk, int off, int bs) {
   fl_u64 cost = fl_frame_header_bits(bs);
   int c;
   if (s->cfg.dp_search) {
      fl_sf sf;
      fl_u64 v[4];
      if (fl_use_stereo(s, off, bs)) {
         const fl_i32 *src[4];
         src[0] = s->buf[0];
         src[1] = s->buf[1];
         src[2] = s->side;
         src[3] = s->mid;
         for (c = 0; c < 4; c++) {
            fl_search_subframe(&s->dcfg, &wk->ws, &wk->wc, src[c]+off, bs, s->bits+(c == 2), FL_FU(s, c), off, &sf);
            v[c] = sf.bits;
         }
         return cost+FL_MIN(FL_MIN(v[0]+v[1], v[0]+v[2]), FL_MIN(v[2]+v[1], v[3]+v[2]));
      }
      for (c = 0; c < s->C; c++) {
         fl_search_subframe(&s->dcfg, &wk->ws, &wk->wc, s->buf[c]+off, bs, s->bits, FL_FU(s, c), off, &sf);
         cost += sf.bits;
      }
      return cost;
   }
   if (fl_use_stereo(s, off, bs)) {
      fl_u64 a = fl_estimate_channel(&s->cfg, &wk->ws, &s->pf[0], s->buf[0], FL_FU(s, 0), off, bs, s->bits);
      fl_u64 b = fl_estimate_channel(&s->cfg, &wk->ws, &s->pf[1], s->buf[1], FL_FU(s, 1), off, bs, s->bits);
      fl_u64 sc = fl_estimate_channel(&s->cfg, &wk->ws, &s->pf[2], s->side, FL_FU(s, 2), off, bs, s->bits+1);
      fl_u64 m = fl_estimate_channel(&s->cfg, &wk->ws, &s->pf[3], s->mid, FL_FU(s, 3), off, bs, s->bits);
      cost += FL_MIN(FL_MIN(a+b, a+sc), FL_MIN(sc+b, m+sc));
   } else {
      for (c = 0; c < s->C; c++) cost += fl_estimate_channel(&s->cfg, &wk->ws, &s->pf[c], s->buf[c], FL_FU(s, c), off, bs, s->bits);
   }
   return cost;
}

/* Block lengths (in units) tried by the search. */
static int fl_dp_len_ok(const fl_flac_cfg *c, int k) {
   int len = k*c->unit;
   return !c->coarse || len <= c->coarse || len%c->coarse_step == 0;
}

/* Thread body: block costs for units t, t+nt, ... */
static void fl_cost_job(void *ctx, int t, int nt) {
   fl_flac_sink *s = (fl_flac_sink*)ctx;
   int u = s->cfg.unit, K = s->cfg.max_block/u, len = s->seg_len, M = (len+u-1)/u, i, k;
   for (i = t; i < M; i += nt)
      for (k = 1; k <= K && i+k <= M; k++) {
         int off = i*u, bs = FL_MIN(k*u, len-off);
         if (fl_dp_len_ok(&s->cfg, k) || i+k == M) s->cost[(size_t)i*K+k-1] = fl_block_cost(s, &s->wk[t], off, bs);
      }
}

/* Thread body: frames t, t+nt, ... */
static void fl_frame_job_run(void *ctx, int t, int nt) {
   fl_flac_sink *s = (fl_flac_sink*)ctx;
   int j;
   for (j = t; j < s->njobs; j += nt) {
      fl_frame_job *job = &s->jobs[j];
      fl_u64 num = s->variable ? s->sample_pos+job->off : s->frame_no+j;
      job->worker = t;
      job->err = fl_encode_frame(s, &s->wk[t], job, num);
   }
}

/* Encodes the first 'len' buffered samples and shifts the rest down. */
static int fl_flac_process(fl_flac_sink *s, int len) {
   int ret = FL_OK, c, i, j, pos = 0;
   if (len <= 0) return FL_OK;
   s->seg_len = len;
   s->njobs = 0;
   if (s->stereo) {
      const fl_i32 *l = s->buf[0], *r = s->buf[1];
      for (i = 0; i < len; i++) {
         fl_i64 d = (fl_i64)l[i]-r[i];
         s->side[i] = (fl_i32)FL_MAX(-0x7FFFFFFF, FL_MIN(0x7FFFFFFF, d));
         s->mid[i] = (fl_i32)(((fl_i64)l[i]+r[i])>>1);
         if (s->side_bad) s->side_bad[i+1] = s->side_bad[i]+(d != s->side[i]);
      }
   }
   if (s->fu_ok) {
      int ns = s->stereo ? 4 : s->C, o;
      for (c = 0; c < ns; c++) {
         const fl_i32 *src = !s->stereo || c < 2 ? s->buf[c] : c == 2 ? s->side : s->mid;
         for (o = 0; o <= 4 && o < len; o++) fl_fixed_residual(src, len, o, s->bits+(s->stereo && c == 2), s->fres, s->fu[c][o]);
      }
   }
   if (!s->cfg.unit) {
      for (pos = 0; pos < len; pos += s->cfg.fixed_block) {
         s->jobs[s->njobs].off = pos;
         s->jobs[s->njobs++].len = FL_MIN(s->cfg.fixed_block, len-pos);
      }
   } else {
      /* block sizes by dynamic programming over a grid of 'unit' samples */
      int u = s->cfg.unit, K = s->cfg.max_block/u, M = (len+u-1)/u, k, ncut = 0;
      fl_u64 *best = s->dp_cost;
      int *from = s->dp_from, *cuts = (int*)malloc(sizeof(int)*(M+1));
      if (!cuts) return FL_ERR_ALLOC;
      if (!s->cfg.dp_search) {
         int fail = 0;
         if (s->stereo) {
            fail |= fl_prefix_build(&s->pf[0], s->buf[0], len, s->bits);
            fail |= fl_prefix_build(&s->pf[1], s->buf[1], len, s->bits);
            fail |= fl_prefix_build(&s->pf[2], s->side, len, s->bits+1);
            fail |= fl_prefix_build(&s->pf[3], s->mid, len, s->bits);
         } else
            for (c = 0; c < s->C; c++) fail |= fl_prefix_build(&s->pf[c], s->buf[c], len, s->bits);
         if (fail) {
            free(cuts);
            return FL_ERR_ALLOC;
         }
      }
      fl_parallel(fl_cost_job, s, FL_MIN(s->nthreads, M));
      best[0] = 0;
      for (i = 1; i <= M; i++) best[i] = ~(fl_u64)0;
      for (i = 0; i < M; i++)
         for (k = 1; k <= K && i+k <= M; k++) {
            fl_u64 cst;
            if (!fl_dp_len_ok(&s->cfg, k) && i+k != M) continue;
            cst = best[i]+s->cost[(size_t)i*K+k-1];
            if (cst < best[i+k]) {
               best[i+k] = cst;
               from[i+k] = i;
            }
         }
      for (i = M; i > 0; i = from[i]) cuts[ncut++] = i;
      for (k = ncut-1; k >= 0; k--) {
         int end = FL_MIN(cuts[k]*u, len);
         s->jobs[s->njobs].off = pos;
         s->jobs[s->njobs++].len = end-pos;
         pos = end;
      }
      free(cuts);
   }
   fl_parallel(fl_frame_job_run, s, FL_MIN(s->nthreads, s->njobs));
   /* write the frames in order */
   for (j = 0; j < s->njobs && ret == FL_OK; j++) {
      fl_frame_job *job = &s->jobs[j];
      fl_u32 flen = (fl_u32)job->bytes;
      if ((ret = job->err) != FL_OK) break;
      if (fwrite(s->wk[job->worker].bw.buf+job->pos, 1, flen, s->f) != flen) ret = FL_ERR_IO;
      s->base.out_bytes += flen;
      if (flen < s->min_frame) s->min_frame = flen;
      if (flen > s->max_frame) s->max_frame = flen;
      if (job->len > s->max_block) s->max_block = job->len;
      if (s->last_block && (s->min_block == 0 || s->last_block < s->min_block)) s->min_block = s->last_block;
      s->last_block = job->len;
   }
   for (i = 0; i < s->nthreads; i++) s->wk[i].bw.len = 0;
   s->sample_pos += len;
   s->frame_no += s->njobs;
   s->base.out_frames = (fl_i64)s->sample_pos;
   for (c = 0; c < s->C; c++) memmove(s->buf[c], s->buf[c]+len, sizeof(fl_i32)*(s->have-len));
   s->have -= len;
   return ret;
}

static int fl_flacenc_write(fl_sink *sk, const fl_i32 *pcm, int n) {
   fl_flac_sink *s = (fl_flac_sink*)sk;
   int C = s->C, i, c, shift = 32-s->bits;
   for (i = 0; i < n*C; i++) {
      /* samples must fit in the declared bits */
      if (shift && (fl_i32)((fl_u32)pcm[i]<<shift)>>shift != pcm[i]) return FL_ERR_BAD_FILE;
   }
   fl_md5_samples(&s->md5, pcm, (size_t)n*C, s->bits);
   while (n > 0) {
      int k = FL_MIN(n, FL_FLAC_SEGMENT-s->have), ret;
      for (c = 0; c < C; c++) {
         fl_i32 *d = s->buf[c]+s->have;
         for (i = 0; i < k; i++) d[i] = pcm[i*C+c];
      }
      s->have += k;
      pcm += (size_t)k*C;
      n -= k;
      if (s->have == FL_FLAC_SEGMENT &&
          (ret = fl_flac_process(s, s->cfg.unit ? FL_FLAC_SEGMENT : FL_FLAC_SEGMENT/s->cfg.fixed_block*s->cfg.fixed_block)) != FL_OK)
         return ret;
   }
   return FL_OK;
}

static int fl_flacenc_finish(fl_sink *sk) {
   fl_flac_sink *s = (fl_flac_sink*)sk;
   unsigned char info[34], md5[16];
   int ret = fl_flac_process(s, s->have);
   if (ret != FL_OK) return ret;
   if (s->f == stdout) goto done;  /* STREAMINFO was written in start() */
   fl_md5_final(&s->md5, md5);
   if (s->frame_no == 0) s->min_frame = 0;
   if (!s->cfg.unit) {
      s->min_block = s->max_block = s->cfg.fixed_block;
   } else {
      if (s->min_block == 0) s->min_block = s->last_block;
      if (s->max_block < 16) s->max_block = 16;
      if (s->min_block < 16) s->min_block = 16;
   }
   fl_flac_streaminfo(s, info, s->sample_pos, md5);
   if (fseek(s->f, s->info_pos, SEEK_SET) != 0 || fwrite(info, 1, 34, s->f) != 34) return FL_ERR_IO;
done:
   ret = fl_fclose(s->f) != 0 ? FL_ERR_IO : FL_OK;
   s->f = NULL;
   return ret;
}

static void fl_flacenc_destroy(fl_sink *sk) {
   fl_flac_sink *s = (fl_flac_sink*)sk;
   int c;
   if (s->f) fl_fclose(s->f);
   for (c = 0; c < 8; c++) {
      int o;
      free(s->buf[c]);
      fl_prefix_free(&s->pf[c]);
      for (o = 0; o < 5; o++) free(s->fu[c][o]);
   }
   free(s->fres);
   free(s->side);
   free(s->side_bad);
   free(s->mid);
   free(s->cost);
   free(s->dp_cost);
   free(s->dp_from);
   free(s->jobs);
   if (s->wk)
      for (c = 0; c < s->nthreads; c++) {
         fl_ws_free(&s->wk[c].ws);
         fl_wcache_free(&s->wk[c].wc);
         fl_flac_dec_free(&s->wk[c].dec);
         free(s->wk[c].bw.buf);
      }
   free(s->wk);
   free(s);
}

static fl_sink *fl_flac_sink_create(const char *path, const fl_options *opt, int *err) {
   fl_flac_sink *s = (fl_flac_sink*)calloc(1, sizeof(*s));
   if (!s) {
      *err = FL_ERR_ALLOC;
      return NULL;
   }
   /* the configuration is completed in start(), when the rate is known */
   s->level = opt->flac_level;
   s->lax = opt->flac_lax;
   s->nthreads = opt->threads > 0 ? opt->threads : opt->threads == 0 ? fl_cpu_count() : 1;
   if (s->nthreads > FL_MAX_THREADS) s->nthreads = FL_MAX_THREADS;
   if (s->nthreads < 1) s->nthreads = 1;
   s->base.start = fl_flacenc_start;
   s->base.write = fl_flacenc_write;
   s->base.finish = fl_flacenc_finish;
   s->base.destroy = fl_flacenc_destroy;
   if (!(s->f = fl_fopen(path, "wb"))) {
      free(s);
      *err = FL_ERR_IO;
      return NULL;
   }
   return &s->base;
}
