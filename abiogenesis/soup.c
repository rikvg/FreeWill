// soup.c — a primordial soup of BFF programs, with byte-level provenance.
//
// Reimplements the BFF soup of Agüera y Arcas et al. (2024), "Computational
// Life: How Well-formed, Self-replicating Programs Emerge from Simple
// Interaction" (semantics follow the authors' cubff reference code), and adds
// one thing: every byte carries a 64-bit provenance tag recording where its
// current value came from. Copy instructions copy tags along with values, so
// when a replicator appears we can ask what it was assembled from.
//
// Tag layout:  bits 63..32  epoch the value was born
//              bits 31..30  kind: 0 = initial random byte, 1 = mutation,
//                                 2 = edited by '+' / '-'
//              bits 29..0   tape index where the value was born
//
// Modes:
//   soup run      [options]          evolve a soup, log metrics, write snapshots
//   soup analyze  SNAPSHOT [opts]    dissect the commonest species in a snapshot
//   soup lottery  [options]          estimate P(random tape is a replicator)
//   soup exec     HEX                run one 128-byte tape and print the result
//
// Build: cc -O3 -march=native -fopenmp soup.c -o soup -lbrotlienc -lm

#include <brotli/encode.h>
#include <math.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T 64        // single tape length
#define TT 128      // concatenated pair length
#define KIND_INIT 0u
#define KIND_MUT 1u
#define KIND_EDIT 2u

typedef uint64_t tag_t;

static inline tag_t mktag(uint32_t epoch, uint32_t kind, uint32_t src) {
  return ((uint64_t)epoch << 32) | ((uint64_t)kind << 30) | (src & 0x3FFFFFFFu);
}
static inline uint32_t tag_epoch(tag_t t) { return (uint32_t)(t >> 32); }
static inline uint32_t tag_kind(tag_t t) { return (uint32_t)(t >> 30) & 3u; }
static inline uint32_t tag_src(tag_t t) { return (uint32_t)t & 0x3FFFFFFFu; }

static inline uint64_t splitmix64(uint64_t x) {
  uint64_t z = x + 0x9e3779b97f4a7c15ULL;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

typedef struct { uint64_t s; } rng_t;
static inline uint64_t rnext(rng_t *r) { r->s += 0x9e3779b97f4a7c15ULL; return splitmix64(r->s); }
static inline rng_t rseed(uint64_t a, uint64_t b, uint64_t c) {
  rng_t r = {splitmix64(splitmix64(a) ^ splitmix64(b * 0x632BE59BD9B4E019ULL + c))};
  return r;
}

// ---------------------------------------------------------------------------
// The BFF interpreter. Mirrors cubff's Bff::Evaluate (no-heads variant by
// default: both heads start at 0, pc starts at 0). Unmatched brackets halt.
// If tags != NULL, provenance is tracked. Returns the number of executed
// (non-comment) instructions.
static int g_heads = 0;  // 1: heads start at tape[0], tape[1]; pc at 2

static long evaluate(uint8_t *tape, tag_t *tags, long steps, uint32_t epoch,
                     uint32_t owner) {
  int pos = 0, h0 = 0, h1 = 0;
  if (g_heads) { h0 = tape[0] % TT; h1 = tape[1] % TT; pos = 2; }
  long i = 0, nskip = 0;
  for (; i < steps; i++) {
    h0 &= TT - 1;
    h1 &= TT - 1;
    switch (tape[pos]) {
      case '<': h0--; break;
      case '>': h0++; break;
      case '{': h1--; break;
      case '}': h1++; break;
      case '+':
        tape[h0]++;
        if (tags) tags[h0] = mktag(epoch, KIND_EDIT, owner);
        break;
      case '-':
        tape[h0]--;
        if (tags) tags[h0] = mktag(epoch, KIND_EDIT, owner);
        break;
      case '.':
        tape[h1] = tape[h0];
        if (tags) tags[h1] = tags[h0];
        break;
      case ',':
        tape[h0] = tape[h1];
        if (tags) tags[h0] = tags[h1];
        break;
      case '[':
        if (tape[h0] == 0) {
          int depth = 1;
          pos++;
          for (; pos < TT && depth > 0; pos++) {
            if (tape[pos] == ']') depth--;
            if (tape[pos] == '[') depth++;
          }
          pos--;
          if (depth != 0) pos = TT;
        }
        break;
      case ']':
        if (tape[h0] != 0) {
          int depth = 1;
          pos--;
          for (; pos >= 0 && depth > 0; pos--) {
            if (tape[pos] == ']') depth++;
            if (tape[pos] == '[') depth--;
          }
          pos++;
          if (depth != 0) pos = -1;
        }
        break;
      default:
        nskip++;
    }
    if (pos < 0) { i++; break; }
    pos++;
    if (pos >= TT) { i++; break; }
  }
  return i - nskip;
}

static long g_steps = 8192;

// ---------------------------------------------------------------------------
// Self-replication score, ported from cubff's CheckSelfRep: pair the program
// with random noise, run, then repeatedly move the right half left and refill
// the right half with noise (4 extra generations). Over 13 independent noise
// draws, count positions whose final byte is consistent (> 13/4 agreement),
// requiring left-half positions to also equal the original program. The score
// is min(left count, right count), from 0 to 64.
static int selfrep_score(const uint8_t *prog, uint64_t seed) {
  enum { ITERS = 13, GENS = 4 };
  uint8_t tapes[ITERS][TT];
  rng_t r = rseed(seed, 0x5e1f, 0);
  for (int it = 0; it < ITERS; it++) {
    uint8_t noise[T];
    for (int j = 0; j < T; j++) noise[j] = (uint8_t)rnext(&r);
    uint8_t *tp = tapes[it];
    memcpy(tp, prog, T);
    memcpy(tp + T, noise, T);
    evaluate(tp, NULL, g_steps, 0, 0);
    for (int g = 0; g < GENS; g++) {
      memcpy(tp, tp + T, T);
      memcpy(tp + T, noise, T);
      evaluate(tp, NULL, g_steps, 0, 0);
    }
  }
  int res[2] = {0, 0};
  for (int i = 0; i < TT; i++) {
    for (int a = 0; a < ITERS; a++) {
      if (i < T && tapes[a][i] != prog[i]) continue;
      int count = 1;
      for (int b = a + 1; b < ITERS; b++)
        if (tapes[a][i] == tapes[b][i]) count++;
      if (count > ITERS / 4) { res[i / T]++; break; }
    }
  }
  return res[0] < res[1] ? res[0] : res[1];
}

// ---------------------------------------------------------------------------
// Pretty printing: commands as themselves, 0 as '0', other bytes as a dot.
static void print_prog(FILE *f, const uint8_t *p, int n) {
  for (int i = 0; i < n; i++) {
    uint8_t c = p[i];
    if (c && strchr("<>{}+-.,[]", c)) fputc(c, f);
    else if (c == 0) fputc('0', f);
    else fputs("\xC2\xB7", f);  // U+00B7 middle dot for comment bytes
  }
}
static void print_hex(FILE *f, const uint8_t *p, int n) {
  for (int i = 0; i < n; i++) fprintf(f, "%02x", p[i]);
}

// ---------------------------------------------------------------------------
// Unique-tape census via open-addressing hash of 64-byte rows.
typedef struct { uint32_t idx, count; } slot_t;

static uint64_t row_hash(const uint8_t *p) {
  uint64_t h = 0x12345;
  for (int i = 0; i < T; i += 8) {
    uint64_t w; memcpy(&w, p + i, 8);
    h = splitmix64(h ^ w);
  }
  return h;
}

// Returns number of distinct rows; fills *slots (caller frees) with counts.
static size_t census(const uint8_t *soup, size_t n, slot_t **out, size_t *cap_out) {
  size_t cap = 1; while (cap < 2 * n) cap <<= 1;
  slot_t *s = calloc(cap, sizeof(slot_t));
  size_t uniq = 0;
  for (size_t i = 0; i < n; i++) {
    const uint8_t *p = soup + i * T;
    size_t h = row_hash(p) & (cap - 1);
    for (;;) {
      if (s[h].count == 0) { s[h].idx = (uint32_t)i; s[h].count = 1; uniq++; break; }
      if (memcmp(soup + (size_t)s[h].idx * T, p, T) == 0) { s[h].count++; break; }
      h = (h + 1) & (cap - 1);
    }
  }
  *out = s; *cap_out = cap;
  return uniq;
}

static int cmp_slot_desc(const void *a, const void *b) {
  const slot_t *x = a, *y = b;
  return (x->count < y->count) - (x->count > y->count);
}

// ---------------------------------------------------------------------------
typedef struct {
  size_t n;
  uint64_t seed;
  double mut;        // per-byte mutation probability per interaction
  long epochs;
  long log_every;
  long snap_every;
  const char *snap_dir;
  int sample;        // tapes sampled for replicator check at each log
  int rep_thresh;    // selfrep score counted as a replicator
  long stop_after;   // stop this many epochs after takeover detected (-1: never)
  int solo;          // control: tapes interact with fresh noise, not each other
} params_t;

static void save_snapshot(const params_t *P, long epoch, const uint8_t *soup,
                          const tag_t *tags) {
  char path[1024];
  snprintf(path, sizeof path, "%s/snap_%08ld.bin", P->snap_dir, epoch);
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); return; }
  uint64_t hdr[4] = {0x50554F53464642ULL, P->n, (uint64_t)epoch, P->seed};
  fwrite(hdr, sizeof hdr, 1, f);
  fwrite(soup, 1, P->n * T, f);
  fwrite(tags, sizeof(tag_t), P->n * T, f);
  fclose(f);
}

static int run(params_t *P) {
  size_t n = P->n;
  uint8_t *soup = malloc(n * T);
  tag_t *tags = malloc(n * T * sizeof(tag_t));
  uint32_t *perm = malloc(n * sizeof(uint32_t));
  size_t bcap = BrotliEncoderMaxCompressedSize(n * T);
  uint8_t *bbuf = malloc(bcap);

  for (size_t i = 0; i < n; i++) {
    rng_t r = rseed(P->seed, 1, i);
    for (int j = 0; j < T; j++) {
      soup[i * T + j] = (uint8_t)rnext(&r);
      tags[i * T + j] = mktag(0, KIND_INIT, (uint32_t)i);
    }
  }
  for (size_t i = 0; i < n; i++) perm[i] = (uint32_t)i;
  uint64_t mut_thresh = (uint64_t)(P->mut * 18446744073709551616.0);

  printf("epoch,ops_per_interaction,h0,brotli_bpb,higher_entropy,unique,top_count,"
         "sampled_replicators,mean_score,top_score,top_prog\n");
  fflush(stdout);

  long takeover_epoch = -1;
  long ops_acc = 0, inter_acc = 0;
  for (long epoch = 1; epoch <= P->epochs; epoch++) {
    rng_t sr = rseed(P->seed, 2, (uint64_t)epoch);
    for (size_t i = n - 1; i > 0; i--) {
      size_t j = rnext(&sr) % (i + 1);
      uint32_t t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
    long ops = 0;
#pragma omp parallel for schedule(dynamic, 128) reduction(+ : ops)
    for (size_t k = 0; k < n / 2; k++) {
      uint32_t p1 = perm[2 * k], p2 = perm[2 * k + 1];
      uint8_t tape[TT];
      tag_t tg[TT];
      rng_t r = rseed(P->seed, 3 + (uint64_t)epoch * 0x10000, k);
      memcpy(tape, soup + (size_t)p1 * T, T);
      memcpy(tg, tags + (size_t)p1 * T, T * sizeof(tag_t));
      if (P->solo) {
        // Control: partner is fresh random noise, discarded afterwards.
        for (int j = 0; j < T; j++) {
          tape[T + j] = (uint8_t)rnext(&r);
          tg[T + j] = mktag((uint32_t)epoch, KIND_MUT, p2);
        }
      } else {
        memcpy(tape + T, soup + (size_t)p2 * T, T);
        memcpy(tg + T, tags + (size_t)p2 * T, T * sizeof(tag_t));
      }
      if (mut_thresh) {
        for (int j = 0; j < TT; j++) {
          if (rnext(&r) < mut_thresh) {
            tape[j] = (uint8_t)rnext(&r);
            tg[j] = mktag((uint32_t)epoch, KIND_MUT, j < T ? p1 : p2);
          }
        }
      }
      ops += evaluate(tape, tg, g_steps, (uint32_t)epoch, p1);
      memcpy(soup + (size_t)p1 * T, tape, T);
      memcpy(tags + (size_t)p1 * T, tg, T * sizeof(tag_t));
      if (P->solo) {
        // In solo mode the second program gets its own turn with noise.
        uint8_t t2[TT]; tag_t g2[TT];
        memcpy(t2, soup + (size_t)p2 * T, T);
        memcpy(g2, tags + (size_t)p2 * T, T * sizeof(tag_t));
        for (int j = 0; j < T; j++) {
          t2[T + j] = (uint8_t)rnext(&r);
          g2[T + j] = mktag((uint32_t)epoch, KIND_MUT, p1);
        }
        if (mut_thresh)
          for (int j = 0; j < T; j++)
            if (rnext(&r) < mut_thresh) {
              t2[j] = (uint8_t)rnext(&r);
              g2[j] = mktag((uint32_t)epoch, KIND_MUT, p2);
            }
        ops += evaluate(t2, g2, g_steps, (uint32_t)epoch, p2);
        memcpy(soup + (size_t)p2 * T, t2, T);
        memcpy(tags + (size_t)p2 * T, g2, T * sizeof(tag_t));
      } else {
        memcpy(soup + (size_t)p2 * T, tape + T, T);
        memcpy(tags + (size_t)p2 * T, tg + T, T * sizeof(tag_t));
      }
    }
    ops_acc += ops;
    inter_acc += P->solo ? (long)n : (long)(n / 2);

    int do_log = (epoch % P->log_every == 0) || epoch == 1;
    if (do_log) {
      size_t counts[256] = {0};
      for (size_t i = 0; i < n * T; i++) counts[soup[i]]++;
      double h0 = 0;
      for (int c = 0; c < 256; c++)
        if (counts[c]) { double f = (double)counts[c] / (n * T); h0 -= f * log2(f); }
      size_t bsz = bcap;
      BrotliEncoderCompress(2, 24, BROTLI_MODE_GENERIC, n * T, soup, &bsz, bbuf);
      double bpb = bsz * 8.0 / (n * T);

      slot_t *slots; size_t cap;
      size_t uniq = census(soup, n, &slots, &cap);
      uint32_t top_idx = 0, top_count = 0;
      for (size_t h = 0; h < cap; h++)
        if (slots[h].count > top_count) { top_count = slots[h].count; top_idx = slots[h].idx; }
      free(slots);

      int reps = 0; long score_sum = 0;
      rng_t rr = rseed(P->seed, 4, (uint64_t)epoch);
      int S = P->sample;
      uint32_t *pick = malloc(S * sizeof(uint32_t));
      for (int s = 0; s < S; s++) pick[s] = (uint32_t)(rnext(&rr) % n);
#pragma omp parallel for schedule(dynamic, 8) reduction(+ : reps, score_sum)
      for (int s = 0; s < S; s++) {
        int sc = selfrep_score(soup + (size_t)pick[s] * T, splitmix64(epoch * 7919 + s));
        score_sum += sc;
        if (sc >= P->rep_thresh) reps++;
      }
      free(pick);
      int top_score = selfrep_score(soup + (size_t)top_idx * T, epoch);

      printf("%ld,%.2f,%.4f,%.4f,%.4f,%zu,%u,%.4f,%.2f,%d,", epoch,
             inter_acc ? (double)ops_acc / inter_acc : 0.0, h0, bpb, h0 - bpb, uniq,
             top_count, (double)reps / S, (double)score_sum / S, top_score);
      print_hex(stdout, soup + (size_t)top_idx * T, T);
      printf("\n");
      fflush(stdout);
      ops_acc = inter_acc = 0;

      if (takeover_epoch < 0 && (double)reps / S >= 0.5) {
        takeover_epoch = epoch;
        fprintf(stderr, "takeover at epoch %ld\n", epoch);
      }
    }
    if (P->snap_dir && P->snap_every > 0 && epoch % P->snap_every == 0)
      save_snapshot(P, epoch, soup, tags);
    if (takeover_epoch >= 0 && P->stop_after >= 0 && epoch >= takeover_epoch + P->stop_after) {
      if (P->snap_dir) save_snapshot(P, epoch, soup, tags);
      break;
    }
  }
  free(soup); free(tags); free(perm); free(bbuf);
  return 0;
}

// ---------------------------------------------------------------------------
// analyze: load a snapshot, find the most common species, report self-rep
// score, an essential-position knockout map, and provenance of each byte.
static int g_scan = 0, g_thresh = 32;
static int analyze(const char *path, int top_k, const char *json_out) {
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); return 1; }
  uint64_t hdr[4];
  if (fread(hdr, sizeof hdr, 1, f) != 1) return 1;
  size_t n = hdr[1]; long epoch = (long)hdr[2];
  uint8_t *soup = malloc(n * T);
  tag_t *tags = malloc(n * T * sizeof(tag_t));
  if (fread(soup, 1, n * T, f) != n * T || fread(tags, sizeof(tag_t), n * T, f) != n * T) {
    fprintf(stderr, "short read\n"); return 1;
  }
  fclose(f);

  size_t nrep = n;
  if (g_scan) {
    // Score every tape and keep only replicators (compacted in place).
    int *sc = malloc(n * sizeof(int));
#pragma omp parallel for schedule(dynamic, 64)
    for (size_t i = 0; i < n; i++) sc[i] = selfrep_score(soup + i * T, i);
    long hist[T + 1] = {0};
    nrep = 0;
    for (size_t i = 0; i < n; i++) {
      hist[sc[i]]++;
      if (sc[i] >= g_thresh) {
        memmove(soup + nrep * T, soup + i * T, T);
        memmove(tags + nrep * T, tags + i * T, T * sizeof(tag_t));
        nrep++;
      }
    }
    printf("scan: %zu of %zu tapes score >= %d\nscore_hist", nrep, n, g_thresh);
    for (int i = 0; i <= T; i++) if (hist[i]) printf(" %d:%ld", i, hist[i]);
    printf("\n");
    free(sc);
  }
  slot_t *slots; size_t cap;
  size_t uniq = census(soup, nrep, &slots, &cap);
  qsort(slots, cap, sizeof(slot_t), cmp_slot_desc);

  FILE *js = json_out ? fopen(json_out, "w") : NULL;
  if (js) fprintf(js, "{\"snapshot\":\"%s\",\"epoch\":%ld,\"n\":%zu,\"replicators\":%zu,\"unique\":%zu,\"species\":[",
                  path, epoch, n, g_scan ? nrep : 0, uniq);
  printf("snapshot %s  epoch %ld  n %zu  unique tapes %zu\n", path, epoch, n, uniq);
  for (int k = 0; k < top_k && k < (int)cap && slots[k].count; k++) {
    const uint8_t *p = soup + (size_t)slots[k].idx * T;
    const tag_t *tg = tags + (size_t)slots[k].idx * T;
    int score = selfrep_score(p, 1);
    // Knockout: replace each byte with a comment byte (0x01) and rescore.
    int ko[T];
    for (int j = 0; j < T; j++) {
      uint8_t q[T]; memcpy(q, p, T);
      q[j] = (q[j] == 0x01) ? 0x02 : 0x01;
      ko[j] = selfrep_score(q, 1);
    }
    printf("\n#%d  count %u (%.1f%%)  selfrep %d/64\n  ", k + 1, slots[k].count,
           100.0 * slots[k].count / n, score);
    print_prog(stdout, p, T);
    printf("\n  essential: ");
    for (int j = 0; j < T; j++) putchar(ko[j] < score / 2 ? '^' : ' ');
    printf("\n");
    if (js) {
      fprintf(js, "%s{\"count\":%u,\"score\":%d,\"hex\":\"", k ? "," : "", slots[k].count, score);
      print_hex(js, p, T);
      fprintf(js, "\",\"knockout\":[");
      for (int j = 0; j < T; j++) fprintf(js, "%s%d", j ? "," : "", ko[j]);
      fprintf(js, "],\"tags\":[");
      for (int j = 0; j < T; j++)
        fprintf(js, "%s[%u,%u,%u]", j ? "," : "", tag_epoch(tg[j]), tag_kind(tg[j]), tag_src(tg[j]));
      fprintf(js, "]}");
    }
  }
  if (js) { fprintf(js, "]}\n"); fclose(js); }
  free(slots); free(soup); free(tags);
  return 0;
}

// ---------------------------------------------------------------------------
// lottery: how often is a uniformly random 64-byte tape already a replicator?
static int lottery(uint64_t seed, long samples, int thresh, const char *hits_out) {
  long hits = 0, done = 0;
  long hist[T + 1] = {0};
  FILE *ho = hits_out ? fopen(hits_out, "w") : NULL;
#pragma omp parallel
  {
    long lh[T + 1] = {0};
#pragma omp for schedule(dynamic, 4096) reduction(+ : hits, done)
    for (long s = 0; s < samples; s++) {
      rng_t r = rseed(seed, 9, (uint64_t)s);
      uint8_t p[T];
      for (int j = 0; j < T; j++) p[j] = (uint8_t)rnext(&r);
      // Cheap prefilter: a single run must copy at least 4 of its own bytes
      // into the right half; otherwise the full score is necessarily low.
      uint8_t t[TT];
      memcpy(t, p, T);
      for (int j = 0; j < T; j++) t[T + j] = (uint8_t)rnext(&r);
      uint8_t before[T]; memcpy(before, t + T, T);
      evaluate(t, NULL, g_steps, 0, 0);
      int changed = 0;
      for (int j = 0; j < T; j++) changed += t[T + j] != before[j];
      int sc = 0;
      if (changed >= 4) sc = selfrep_score(p, (uint64_t)s);
      lh[sc]++;
      done++;
      if (sc >= thresh) {
        hits++;
        if (ho) {
#pragma omp critical
          { fprintf(ho, "%d ", sc); print_hex(ho, p, T); fputc('\n', ho); fflush(ho); }
        }
      }
    }
#pragma omp critical
    for (int i = 0; i <= T; i++) hist[i] += lh[i];
  }
  if (ho) fclose(ho);
  printf("samples %ld  replicators(score>=%d) %ld  p=%.3e\nscore_hist", done, thresh, hits,
         (double)hits / done);
  for (int i = 0; i <= T; i++) if (hist[i]) printf(" %d:%ld", i, hist[i]);
  printf("\n");
  return 0;
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: soup run|analyze|lottery|score|exec ...\n");
    return 1;
  }
  const char *mode = argv[1];
  params_t P = {.n = 1 << 17, .seed = 1, .mut = 1.0 / 4096, .epochs = 20000,
                .log_every = 64, .snap_every = 0, .snap_dir = NULL, .sample = 512,
                .rep_thresh = 32, .stop_after = -1, .solo = 0};
  int top_k = 5; long samples = 1000000; const char *json = NULL, *hits = NULL;
  const char *positional = NULL;
  for (int i = 2; i < argc; i++) {
    const char *a = argv[i];
    const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define OPT(name) (strcmp(a, name) == 0 && v && (i++, 1))
    if (OPT("-n")) P.n = strtoull(v, 0, 0);
    else if (OPT("-seed")) P.seed = strtoull(v, 0, 0);
    else if (OPT("-mut")) P.mut = atof(v);
    else if (OPT("-epochs")) P.epochs = atol(v);
    else if (OPT("-log")) P.log_every = atol(v);
    else if (OPT("-snap")) P.snap_every = atol(v);
    else if (OPT("-dir")) P.snap_dir = v;
    else if (OPT("-sample")) P.sample = atoi(v);
    else if (OPT("-thresh")) P.rep_thresh = atoi(v);
    else if (OPT("-stop-after")) P.stop_after = atol(v);
    else if (OPT("-steps")) g_steps = atol(v);
    else if (OPT("-top")) top_k = atoi(v);
    else if (OPT("-samples")) samples = atol(v);
    else if (OPT("-json")) json = v;
    else if (OPT("-hits")) hits = v;
    else if (strcmp(a, "-heads") == 0) g_heads = 1;
    else if (strcmp(a, "-solo") == 0) P.solo = 1;
    else if (strcmp(a, "-scan") == 0) g_scan = 1;
    else positional = a;
#undef OPT
  }
  if (strcmp(mode, "run") == 0) return run(&P);
  if (strcmp(mode, "analyze") == 0) { g_thresh = P.rep_thresh; return analyze(positional, top_k, json); }
  if (strcmp(mode, "lottery") == 0) return lottery(P.seed, samples, P.rep_thresh, hits);
  if (strcmp(mode, "score") == 0 || strcmp(mode, "exec") == 0) {
    uint8_t t[TT] = {0};
    size_t L = positional ? strlen(positional) / 2 : 0;
    for (size_t i = 0; i < L && i < TT; i++)
      t[i] = (uint8_t)(hexval(positional[2 * i]) * 16 + hexval(positional[2 * i + 1]));
    if (mode[0] == 's') { printf("%d\n", selfrep_score(t, P.seed)); return 0; }
    long ops = evaluate(t, NULL, g_steps, 0, 0);
    print_prog(stdout, t, T); printf(" | "); print_prog(stdout, t + T, T);
    printf("\nops %ld\n", ops);
    return 0;
  }
  fprintf(stderr, "unknown mode %s\n", mode);
  return 1;
}
