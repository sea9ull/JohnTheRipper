/*
 * Implementation in John the Ripper Copyright (c) 2015, magnum
 * This software is hereby released to the general public under
 * the following terms: Redistribution and use in source and binary
 * forms, with or without modification, are permitted.
 *
 * The MIT License (MIT)
 * Copyright (c) 2015 Jens Steube
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if AC_BUILT
#include "autoconfig.h"
#else
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#define __USE_MINGW_ANSI_STDIO 1
#ifdef __SIZEOF_INT128__
#define HAVE___INT128 1
#endif
#endif

#if HAVE_INT128 || HAVE___INT128 || HAVE___UINT128_T

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#if !AC_BUILT
#include <string.h>
#ifndef _MSC_VER
#include <strings.h>
#endif
#else
#if STRING_WITH_STRINGS
#include <string.h>
#include <strings.h>
#elif HAVE_STRING_H
#include <string.h>
#elif HAVE_STRINGS_H
#include <strings.h>
#endif
#endif
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>

/**
 * Name........: princeprocessor (pp)
 * Description.: Standalone password candidate generator using the PRINCE algorithm
 * Version.....: 0.19
 * Autor.......: Jens Steube <jens.steube@gmail.com>
 * License.....: MIT
 */

#ifdef JTR_MODE

#include "os.h"

#if (!AC_BUILT || HAVE_UNISTD_H) && !_MSC_VER
#include <unistd.h>
#endif

#include "arch.h"
#include "jumbo.h"
#include "misc.h"
#include "config.h"
#include "math.h"
#include "params.h"
#include "common.h"
#include "path.h"
#include "signals.h"
#include "loader.h"
#include "logger.h"
#include "status.h"
#include "recovery.h"
#include "options.h"
#include "external.h"
#include "cracker.h"
#include "john.h"
#include "memory.h"
#include "unicode.h"
#include "prince.h"
#include "memdbg.h"

#define _STR_VALUE(arg) #arg
#define STR_MACRO(n)    _STR_VALUE(n)
#endif

#define IN_LEN_MIN    1
#define IN_LEN_MAX    16
#define PW_MIN        IN_LEN_MIN
#define PW_MAX        IN_LEN_MAX
#define ELEM_CNT_MIN  1
#define ELEM_CNT_MAX  8
#define WL_DIST_LEN   0

#define VERSION_BIN   19

#define ALLOC_NEW_ELEMS  0x40000
#define ALLOC_NEW_CHAINS 0x10

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#if HAVE_INT128
typedef unsigned int128 u128;
#elif HAVE___INT128
typedef unsigned __int128 u128;
#else
typedef __uint128_t u128;
#endif

typedef struct
{
  int len;
  u64 cnt;

} pw_order_t;

typedef struct
{
  u8    buf[IN_LEN_MAX];

} elem_t;

typedef struct
{
  u8    buf[IN_LEN_MAX];
  int   cnt;

  u128 ks_cnt;
  u128 ks_pos;

} chain_t;

typedef struct
{
  elem_t  *elems_buf;
  u64      elems_cnt;
  u64      elems_alloc;

  chain_t *chains_buf;
  int      chains_cnt;
  int      chains_pos;
  int      chains_alloc;

} db_entry_t;

#ifndef JTR_MODE
typedef struct
{
  FILE *fp;

  char  buf[BUFSIZ];
  int   len;

} out_t;
#endif

/**
 * Default word-length distribution, calculated out of first 1,000,000 entries of rockyou.txt
 */

#define DEF_WORDLEN_DIST_CNT 25

static u64 DEF_WORDLEN_DIST[DEF_WORDLEN_DIST_CNT] =
{
  0,
  15,
  56,
  350,
  3315,
  43721,
  276252,
  201748,
  226412,
  119885,
  75075,
  26323,
  13373,
  6353,
  3540,
  1877,
  972,
  311,
  151,
  81,
  66,
  21,
  16,
  13,
  13
};

#ifndef JTR_MODE
static const char *USAGE_MINI[] =
{
  "Usage: %s [options] < wordlist",
  "",
  "Try --help for more help.",
  NULL
};

static const char *USAGE_BIG[] =
{
  "pp by atom, High-Performance word-generator",
  "",
  "Usage: %s [options] < wordlist",
  "",
  "* Startup:",
  "",
  "  -V,  --version             Print version",
  "  -h,  --help                Print help",
  "",
  "* Misc:",
  "",
  "       --keyspace            Calculate number of combinations",
  "",
  "* Optimization:",
  "",
  "       --pw-min=NUM          Print candidate if length is greater than NUM",
  "       --pw-max=NUM          Print candidate if length is smaller than NUM",
  "       --elem-cnt-min=NUM    Minimum number of elements per chain",
  "       --elem-cnt-max=NUM    Maximum number of elements per chain",
  "       --wl-dist-len         Calculate output length distribution from wordlist",
  "",
  "* Resources:",
  "",
  "  -s,  --skip=NUM            Skip NUM passwords from start (for distributed)",
  "  -l,  --limit=NUM           Limit output to NUM passwords (for distributed)",
  "",
  "* Files:",
  "",
  "  -o,  --output-file=FILE    Output-file",
  "",
  NULL
};

static void usage_mini_print (const char *progname)
{
  int i;

  for (i = 0; USAGE_MINI[i] != NULL; i++)
  {
    printf (USAGE_MINI[i], progname);

    #ifdef OSX
    putchar ('\n');
    #endif

    #ifdef LINUX
    putchar ('\n');
    #endif

    #ifdef WINDOWS
    putchar ('\r');
    putchar ('\n');
    #endif
  }
}

static void usage_big_print (const char *progname)
{
  int i;

  for (i = 0; USAGE_BIG[i] != NULL; i++)
  {
    printf (USAGE_BIG[i], progname);

    #ifdef OSX
    putchar ('\n');
    #endif

    #ifdef LINUX
    putchar ('\n');
    #endif

    #ifdef WINDOWS
    putchar ('\r');
    putchar ('\n');
    #endif
  }
}
#endif

static void check_realloc_elems (db_entry_t *db_entry)
{
  if (db_entry->elems_cnt == db_entry->elems_alloc)
  {
    const u64 elems_alloc = db_entry->elems_alloc;

    const u64 elems_alloc_new = elems_alloc + ALLOC_NEW_ELEMS;

    db_entry->elems_buf = (elem_t *) realloc (db_entry->elems_buf, elems_alloc_new * sizeof (elem_t));

    if (db_entry->elems_buf == NULL)
    {
#ifndef JTR_MODE
      fprintf (stderr, "Out of memory!\n");

      exit (-1);
#else
      fprintf (stderr, "Out of memory trying to allocate %zu bytes!\n",
               (size_t)elems_alloc_new * sizeof (elem_t));
      error();
#endif
    }

    memset (&db_entry->elems_buf[elems_alloc], 0, ALLOC_NEW_ELEMS * sizeof (elem_t));

    db_entry->elems_alloc = elems_alloc_new;
  }
}

static void check_realloc_chains (db_entry_t *db_entry)
{
  if (db_entry->chains_cnt == db_entry->chains_alloc)
  {
    const u64 chains_alloc = db_entry->chains_alloc;

    const u64 chains_alloc_new = chains_alloc + ALLOC_NEW_CHAINS;

    db_entry->chains_buf = (chain_t *) realloc (db_entry->chains_buf, chains_alloc_new * sizeof (chain_t));

    if (db_entry->chains_buf == NULL)
    {
#ifndef JTR_MODE
      fprintf (stderr, "Out of memory!\n");

      exit (-1);
#else
      fprintf (stderr, "Out of memory trying to allocate %zu bytes!\n",
               (size_t)chains_alloc_new * sizeof (chain_t));
      error();
#endif
    }

    memset (&db_entry->chains_buf[chains_alloc], 0, ALLOC_NEW_CHAINS * sizeof (chain_t));

    db_entry->chains_alloc = chains_alloc_new;
  }
}

static int in_superchop (char *buf)
{
  int len = strlen (buf);

  while (len)
  {
    if (buf[len - 1] == '\n')
    {
      len--;

      continue;
    }

    if (buf[len - 1] == '\r')
    {
      len--;

      continue;
    }

    break;
  }

  buf[len] = 0;

  return len;
}

#ifndef JTR_MODE
static void out_flush (out_t *out)
{
  fwrite (out->buf, 1, out->len, out->fp);

  out->len = 0;
}

static void out_push (out_t *out, const char *pw_buf, const int pw_len)
{
  memcpy (out->buf + out->len, pw_buf, pw_len);

  out->len += pw_len;

  if (out->len >= BUFSIZ - 100)
  {
    out_flush (out);
  }
}

#endif
static int sort_by_cnt (const void *p1, const void *p2)
{
  const pw_order_t *o1 = (const pw_order_t *) p1;
  const pw_order_t *o2 = (const pw_order_t *) p2;

  // Descending order
  if (o1->cnt > o2->cnt) return -1;
  if (o1->cnt < o2->cnt) return  1;

  return 0;
}

static int sort_by_ks (const void *p1, const void *p2)
{
  const chain_t *f1 = (const chain_t *) p1;
  const chain_t *f2 = (const chain_t *) p2;

  if (f1->ks_cnt > f2->ks_cnt)
    return 1;
  else if (f1->ks_cnt < f2->ks_cnt)
    return -1;
  else
    return 0;
}

static int chain_valid_with_db (const chain_t *chain_buf, const db_entry_t *db_entries)
{
  const u8 *buf = chain_buf->buf;
  const int cnt = chain_buf->cnt;

  for (int idx = 0; idx < cnt; idx++)
  {
    const u8 db_key = buf[idx];

    const db_entry_t *db_entry = &db_entries[db_key];

    if (db_entry->elems_cnt == 0) return 0;
  }

  return 1;
}

static int chain_valid_with_cnt_min (const chain_t *chain_buf, const int elem_cnt_min)
{
  const int cnt = chain_buf->cnt;

  if (cnt < elem_cnt_min) return 0;

  return 1;
}

static int chain_valid_with_cnt_max (const chain_t *chain_buf, const int elem_cnt_max)
{
  const int cnt = chain_buf->cnt;

  if (cnt > elem_cnt_max) return 0;

  return 1;
}

static void chain_ks (const chain_t *chain_buf, const db_entry_t *db_entries, u128 *ks_cnt)
{
  const u8 *buf = chain_buf->buf;
  const int cnt = chain_buf->cnt;

  *ks_cnt = 1;

  for (int idx = 0; idx < cnt; idx++)
  {
    const u8 db_key = buf[idx];

    const db_entry_t *db_entry = &db_entries[db_key];

    const u64 elems_cnt = db_entry->elems_cnt;

    *ks_cnt *= elems_cnt;
  }
}

static void chain_set_pwbuf (const chain_t *chain_buf, const db_entry_t *db_entries, u128 *tmp, char *pw_buf)
{
  const u8 *buf = chain_buf->buf;

  const u32 cnt = chain_buf->cnt;

  for (u32 idx = 0; idx < cnt; idx++)
  {
    const u8 db_key = buf[idx];

    const db_entry_t *db_entry = &db_entries[db_key];

    const u64 elems_cnt = db_entry->elems_cnt;

    const u64 elems_idx = *tmp % elems_cnt;

    memcpy (pw_buf, &db_entry->elems_buf[elems_idx], db_key);

    pw_buf += db_key;

    *tmp /= elems_cnt;
  }
}

static void chain_gen_with_idx (chain_t *chain_buf, const int len1, const int chains_idx)
{
  chain_buf->cnt = 0;

  u8 db_key = 1;

  for (int chains_shr = 0; chains_shr < len1; chains_shr++)
  {
    if ((chains_idx >> chains_shr) & 1)
    {
      chain_buf->buf[chain_buf->cnt] = db_key;

      chain_buf->cnt++;

      db_key = 1;
    }
    else
    {
      db_key++;
    }
  }

  chain_buf->buf[chain_buf->cnt] = db_key;

  chain_buf->cnt++;
}

#ifdef JTR_MODE
static FILE *word_file;
static u128 count = 1;
static u128 pos;
static u128 rec_pos;

static void save_state(FILE *file)
{
  fprintf(file, "%llu\n", (unsigned long long)rec_pos);
  fprintf(file, "%llu\n", (unsigned long long)(rec_pos >> 64));
}

static int restore_state(FILE *file)
{
  unsigned long long temp;
  int ret = !fscanf(file, "%llu\n", &temp);
  rec_pos = temp;
  if (fscanf(file, "%llu\n", &temp))
	  rec_pos |= (u128)temp << 64;
  return ret;
}

static void fix_state(void)
{
  rec_pos = pos;
}

static double get_progress(void)
{
  return 100.0 * (double)rec_pos / (double)count;
}

void do_prince_crack(struct db_main *db, char *filename)
#else
int main (int argc, char *argv[])
#endif
{
  u128 iter_max = 0;
  u128 total_ks_cnt = 0;
  u128 total_ks_pos = 0;
  u128 total_ks_left = 0;
  u128 skip = 0;
  u128 limit = 0;
  u128 tmp = 0;

#ifndef JTR_MODE
  int     version       = 0;
  int     usage         = 0;
  int     keyspace      = 0;
#endif
  int     pw_min        = PW_MIN;
  int     pw_max        = PW_MAX;
  int     elem_cnt_min  = ELEM_CNT_MIN;
  int     elem_cnt_max  = ELEM_CNT_MAX;
  int     wl_dist_len   = WL_DIST_LEN;
#ifndef JTR_MODE
  char   *output_file   = NULL;
#endif

  #define IDX_VERSION       'V'
  #define IDX_USAGE         'h'
  #define IDX_PW_MIN        0x1000
  #define IDX_PW_MAX        0x2000
  #define IDX_ELEM_CNT_MIN  0x3000
  #define IDX_ELEM_CNT_MAX  0x4000
  #define IDX_KEYSPACE      0x5000
  #define IDX_WL_DIST_LEN   0x6000
  #define IDX_SKIP          's'
  #define IDX_LIMIT         'l'
  #define IDX_OUTPUT_FILE   'o'

#ifndef JTR_MODE
  struct option long_options[] =
  {
    {"version",       no_argument,       0, IDX_VERSION},
    {"help",          no_argument,       0, IDX_USAGE},
    {"keyspace",      no_argument,       0, IDX_KEYSPACE},
    {"pw-min",        required_argument, 0, IDX_PW_MIN},
    {"pw-max",        required_argument, 0, IDX_PW_MAX},
    {"elem-cnt-min",  required_argument, 0, IDX_ELEM_CNT_MIN},
    {"elem-cnt-max",  required_argument, 0, IDX_ELEM_CNT_MAX},
    {"wl-dist-len",   no_argument,       0, IDX_WL_DIST_LEN},
    {"skip",          required_argument, 0, IDX_SKIP},
    {"limit",         required_argument, 0, IDX_LIMIT},
    {"output-file",   required_argument, 0, IDX_OUTPUT_FILE},
    {0, 0, 0, 0}
  };

  int option_index = 0;

  int c;

  while ((c = getopt_long (argc, argv, "Vhs:l:o:", long_options, &option_index)) != -1)
  {
    switch (c)
    {
      case IDX_VERSION:       version         = 1;              break;
      case IDX_USAGE:         usage           = 1;              break;
      case IDX_KEYSPACE:      keyspace        = 1;              break;
      case IDX_PW_MIN:        pw_min          = atoi (optarg);  break;
      case IDX_PW_MAX:        pw_max          = atoi (optarg);  break;
      case IDX_ELEM_CNT_MIN:  elem_cnt_min    = atoi (optarg);  break;
      case IDX_ELEM_CNT_MAX:  elem_cnt_max    = atoi (optarg);  break;
      case IDX_WL_DIST_LEN:   wl_dist_len     = 1;              break;
      case IDX_SKIP:          skip     = strtod (optarg, NULL); break;
      case IDX_LIMIT:         limit    = strtod (optarg, NULL); break;
      case IDX_OUTPUT_FILE:   output_file     = optarg;         break;

      default: return (-1);
    }
  }

  if (usage)
  {
    usage_big_print (argv[0]);

    return (-1);
  }

  if (version)
  {
    printf ("v%4.02f\n", (double) VERSION_BIN / 100);

    return (-1);
  }

  if (optind != argc)
  {
    usage_mini_print (argv[0]);

    return (-1);
  }

  if (pw_min <= 0)
  {
    fprintf (stderr, "Value of --pw-min (%d) must be greater than %d\n", pw_min, 0);

    return (-1);
  }

  if (pw_max <= 0)
  {
    fprintf (stderr, "Value of --pw-max (%d) must be greater than %d\n", pw_max, 0);

    return (-1);
  }

  if (elem_cnt_min <= 0)
  {
    fprintf (stderr, "Value of --elem-cnt-min (%d) must be greater than %d\n", elem_cnt_min, 0);

    return (-1);
  }

  if (elem_cnt_max <= 0)
  {
    fprintf (stderr, "Value of --elem-cnt-max (%d) must be greater than %d\n", elem_cnt_max, 0);

    return (-1);
  }

  if (pw_min > pw_max)
  {
    fprintf (stderr, "Value of --pw-min (%d) must be smaller or equal than value of --pw-max (%d)\n", pw_min, pw_max);

    return (-1);
  }

  if (elem_cnt_min > elem_cnt_max)
  {
    fprintf (stderr, "Value of --elem-cnt-min (%d) must be smaller or equal than value of --elem-cnt-max (%d)\n", elem_cnt_min, elem_cnt_max);

    return (-1);
  }

  if (pw_min < IN_LEN_MIN)
  {
    fprintf (stderr, "Value of --pw-min (%d) must be greater or equal than %d\n", pw_min, IN_LEN_MIN);

    return (-1);
  }

  if (pw_max > IN_LEN_MAX)
  {
    fprintf (stderr, "Value of --pw-max (%d) must be smaller or equal than %d\n", pw_max, IN_LEN_MAX);

    return (-1);
  }

  if (elem_cnt_max > pw_max)
  {
    fprintf (stderr, "Value of --elem-cnt-max (%d) must be smaller or equal than value of --pw-max (%d)\n", elem_cnt_max, pw_max);

    return (-1);
  }

  /**
   * OS specific settings
   */

  #ifdef WINDOWS
  setmode (fileno (stdout), O_BINARY);
  #endif
#else
  log_event("Proceeding with PRINCE mode");

  pw_min = MAX(IN_LEN_MIN, options.force_minlength);
  pw_max = MIN(IN_LEN_MAX, db->format->params.plaintext_length);

  if (options.force_maxlength && options.force_maxlength < pw_max)
    pw_max = MIN(IN_LEN_MAX, options.force_maxlength);

  if (prince_elem_cnt_min)
    elem_cnt_min = prince_elem_cnt_min;
  if (prince_elem_cnt_max)
    elem_cnt_max = prince_elem_cnt_max;
  wl_dist_len = prince_wl_dist_len;

  if (elem_cnt_min > elem_cnt_max)
  {
    if (john_main_process)
    fprintf (stderr, "Error: --prince-elem-cnt-min (%d) must be smaller than or equal to\n--prince-elem-cnt-max (%d)\n", elem_cnt_min, elem_cnt_max);

    error();
  }
  if (elem_cnt_min > pw_max)
  {
    if (john_main_process)
    fprintf (stderr, "Error: --prince-elem-cnt-max (%d) must be smaller than or equal to\nmax. plaintext length (%d)\n", elem_cnt_min, pw_max);

    error();
  }

  /* If we did not give a name for wordlist mode, we use one from john.conf */
  if (!filename)
  if (!(filename = cfg_get_param(SECTION_PRINCE, NULL, "Wordlist")))
  if (!(filename = cfg_get_param(SECTION_OPTIONS, NULL, "Wordlist")))
    filename = options.wordlist = WORDLIST_NAME;

  log_event("- Wordlist file: %.100s", path_expand(filename));
  log_event("- Will generate candidates of length %d - %d", pw_min, pw_max);
  log_event("- using chains with %d - %d elements.", elem_cnt_min, elem_cnt_max);
  if (wl_dist_len)
    log_event("- Calculating length distribution from wordlist");
  else
    log_event("- Using default length distribution");
#endif

  /**
   * alloc some space
   */

#ifndef JTR_MODE
  db_entry_t *db_entries   = (db_entry_t *) calloc (IN_LEN_MAX + 1, sizeof (db_entry_t));
  pw_order_t *pw_orders    = (pw_order_t *) calloc (IN_LEN_MAX + 1, sizeof (pw_order_t));
  u64        *wordlen_dist = (u64 *)        calloc (IN_LEN_MAX + 1, sizeof (u64));

  out_t *out = (out_t *) malloc (sizeof (out_t));

  out->fp  = stdout;
  out->len = 0;
#else
  db_entry_t *db_entries   = (db_entry_t *) mem_calloc ((IN_LEN_MAX + 1) * sizeof (db_entry_t));
  pw_order_t *pw_orders    = (pw_order_t *) mem_calloc ((IN_LEN_MAX + 1) * sizeof (pw_order_t));
  u64        *wordlen_dist = (u64 *)        mem_calloc ((IN_LEN_MAX + 1) * sizeof (u64));
#endif

  /**
   * files
   */

#ifndef JTR_MODE
  if (output_file)
  {
    out->fp = fopen (output_file, "ab");

    if (out->fp == NULL)
    {
      fprintf (stderr, "%s: %s\n", output_file, strerror (errno));

      return (-1);
    }
  }

  /**
   * load elems from stdin
   */

  while (!feof (stdin))
  {
    char buf[BUFSIZ];

    char *input_buf = fgets (buf, sizeof (buf), stdin);
#else
    if (!(word_file = jtr_fopen(path_expand(filename), "rb")))
      pexit(STR_MACRO(jtr_fopen)": %s", path_expand(filename));
    log_event("- Input file: %.100s", path_expand(filename));

  log_event("Loading elements from wordlist");

  while (!feof (word_file))
  {
    char buf[BUFSIZ];
    char *input_buf = fgets (buf, sizeof (buf), word_file);
#endif

    if (input_buf == NULL) continue;

#ifdef JTR_MODE
    if (!strncmp(input_buf, "#!comment", 9))
      continue;
#endif
    const int input_len = in_superchop (input_buf);

    if (input_len < IN_LEN_MIN) continue;
    if (input_len > IN_LEN_MAX) continue;

    db_entry_t *db_entry = &db_entries[input_len];

    check_realloc_elems (db_entry);

    elem_t *elem_buf = &db_entry->elems_buf[db_entry->elems_cnt];

    memcpy (elem_buf->buf, input_buf, input_len);

    db_entry->elems_cnt++;
  }

  /**
   * init chains
   */

#ifdef JTR_MODE
  log_event("Initializing chains");
#endif
  for (int pw_len = pw_min; pw_len <= pw_max; pw_len++)
  {
    db_entry_t *db_entry = &db_entries[pw_len];

    const int pw_len1 = pw_len - 1;

    const int chains_cnt = 1 << pw_len1;

    chain_t chain_buf_new;

    for (int chains_idx = 0; chains_idx < chains_cnt; chains_idx++)
    {
      chain_gen_with_idx (&chain_buf_new, pw_len1, chains_idx);

      // make sure all the elements really exist

      int valid1 = chain_valid_with_db (&chain_buf_new, db_entries);

      if (valid1 == 0) continue;

      // boost by verify element count to be inside a specific range

      int valid2 = chain_valid_with_cnt_min (&chain_buf_new, elem_cnt_min);

      if (valid2 == 0) continue;

      int valid3 = chain_valid_with_cnt_max (&chain_buf_new, elem_cnt_max);

      if (valid3 == 0) continue;

      // add chain to database

      check_realloc_chains (db_entry);

      chain_t *chain_buf = &db_entry->chains_buf[db_entry->chains_cnt];

      memcpy (chain_buf, &chain_buf_new, sizeof (chain_t));

      chain_buf->ks_cnt = 0;
      chain_buf->ks_pos = 0;

      db_entry->chains_cnt++;
    }
  }

  /**
   * calculate password candidate output length distribution
   */

  if (wl_dist_len)
  {
#ifdef JTR_MODE
  log_event("Calculating output length distribution from wordlist file");
#endif
    for (int pw_len = IN_LEN_MIN; pw_len <= IN_LEN_MAX; pw_len++)
    {
      db_entry_t *db_entry = &db_entries[pw_len];

      wordlen_dist[pw_len] = db_entry->elems_cnt;
    }
  }
  else
  {
#ifdef JTR_MODE
  log_event("Using default output length distribution");
#endif
    for (int pw_len = IN_LEN_MIN; pw_len <= IN_LEN_MAX; pw_len++)
    {
      if (pw_len < DEF_WORDLEN_DIST_CNT)
      {
        wordlen_dist[pw_len] = DEF_WORDLEN_DIST[pw_len];
      }
      else
      {
        wordlen_dist[pw_len] = 1;
      }
    }
  }

  /**
   * Calculate keyspace stuff
   */

#ifdef JTR_MODE
  log_event("Calculating keyspace");
#endif
  for (int pw_len = pw_min; pw_len <= pw_max; pw_len++)
  {
    db_entry_t *db_entry = &db_entries[pw_len];

    int      chains_cnt = db_entry->chains_cnt;
    chain_t *chains_buf = db_entry->chains_buf;

    for (int chains_idx = 0; chains_idx < chains_cnt; chains_idx++)
    {
      chain_t *chain_buf = &chains_buf[chains_idx];

      chain_ks (chain_buf, db_entries, &chain_buf->ks_cnt);

      total_ks_cnt += chain_buf->ks_cnt;
    }
  }

#ifndef JTR_MODE
  if (keyspace)
  {
    printf ("%.0f (give or take)\n", (double)total_ks_cnt);

    return 0;
  }
#else
  log_event("- Keyspace size 0x%llx%08llx",
            (unsigned long long)(total_ks_cnt >> 64),
            (unsigned long long)total_ks_cnt);
#endif

  /**
   * sort chains by ks
   */

#ifdef JTR_MODE
  log_event("Sorting chains by keyspace");
#endif
  for (int pw_len = pw_min; pw_len <= pw_max; pw_len++)
  {
    db_entry_t *db_entry = &db_entries[pw_len];

    chain_t *chains_buf = db_entry->chains_buf;

    const int chains_cnt = db_entry->chains_cnt;

    qsort (chains_buf, chains_cnt, sizeof (chain_t), sort_by_ks);
  }
#ifdef JTR_MODE
  count = total_ks_cnt;

  status_init(get_progress, 0);

  rec_restore_mode(restore_state);
  rec_init(db, save_state);
  skip = pos = rec_pos;

  crk_init(db, fix_state, NULL);

  log_event("Sorting global order by password length counts");

#endif

  /**
   * sort global order by password length counts
   */

  for (int pw_len = pw_min, order_pos = 0; pw_len <= pw_max; pw_len++, order_pos++)
  {
    db_entry_t *db_entry = &db_entries[pw_len];

    const u64 elems_cnt = db_entry->elems_cnt;

    pw_order_t *pw_order = &pw_orders[order_pos];

    pw_order->len = pw_len;
    pw_order->cnt = elems_cnt;
  }

  const int order_cnt = pw_max + 1 - pw_min;

  qsort (pw_orders, order_cnt, sizeof (pw_order_t), sort_by_cnt);

  /**
   * seek to some starting point
   */

  if (skip)
  {
    if (skip > total_ks_cnt)
    {
      fprintf (stderr, "Value of --skip must be smaller than total keyspace\n");

#ifndef JTR_MODE
      return (-1);
#else
      error();
#endif
    }
  }

  if (limit)
  {
    if (limit > total_ks_cnt)
    {
      fprintf (stderr, "Value of --limit must be smaller than total keyspace\n");

#ifndef JTR_MODE
      return (-1);
#else
      error();
#endif
    }

    tmp = skip + limit;

    if (tmp > total_ks_cnt)
    {
      fprintf (stderr, "Value of --skip + --limit must be smaller than total keyspace\n");

#ifndef JTR_MODE
      return (-1);
#else
      error();
#endif
    }

    total_ks_cnt = tmp;
  }

  /**
   * loop
   */

#ifdef JTR_MODE
  log_event("Starting candidate generation");

  int jtr_done = 0;
  int node_dist = 0;
#endif
  while (total_ks_pos < total_ks_cnt)
  {
    for (int order_pos = 0; order_pos < order_cnt; order_pos++)
    {
      pw_order_t *pw_order = &pw_orders[order_pos];

      const int pw_len = pw_order->len;

      char pw_buf[BUFSIZ];

#ifndef JTR_MODE
      pw_buf[pw_len] = '\n';
#else
      pw_buf[pw_len] = '\0';
#endif

      db_entry_t *db_entry = &db_entries[pw_len];

      const u64 outs_cnt = wordlen_dist[pw_len];

      u64 outs_pos = 0;

      while (outs_pos < outs_cnt)
      {
        const int chains_cnt = db_entry->chains_cnt;
        const int chains_pos = db_entry->chains_pos;

        if (chains_pos == chains_cnt) break;

        chain_t *chains_buf = db_entry->chains_buf;

        chain_t *chain_buf = &chains_buf[chains_pos];

        total_ks_left = total_ks_cnt - total_ks_pos;

        iter_max = chain_buf->ks_cnt - chain_buf->ks_pos;

        if (total_ks_left < iter_max)
        {
          iter_max = total_ks_left;
        }

        const u64 outs_left = outs_cnt - outs_pos;

        tmp = outs_left;

        if (tmp < iter_max)
        {
          iter_max = tmp;
        }

        const u64 iter_max_u64 = iter_max;

        tmp = total_ks_pos + iter_max;

#ifdef JTR_MODE
        int for_node, node_skip = 0;
        if (options.node_count) {
          for_node = node_dist++ % options.node_count + 1;
          node_skip = for_node < options.node_min ||
                      for_node > options.node_max;
        }
        if (!node_skip)
#endif
        if (tmp > skip)
        {
          u64 iter_pos_u64 = 0;

          if (total_ks_pos < skip)
          {
            tmp = skip - total_ks_pos;

            iter_pos_u64 = tmp;
          }

          while (iter_pos_u64 < iter_max_u64)
          {
            tmp = chain_buf->ks_pos + iter_pos_u64;

            chain_set_pwbuf (chain_buf, db_entries, &tmp, pw_buf);

#ifndef JTR_MODE
            out_push (out, pw_buf, pw_len + 1);
#else
            //pos = total_ks_pos + iter_pos_u64;

            if (ext_filter(pw_buf))
            if ((jtr_done = crk_process_key(pw_buf)))
              break;
#endif

            iter_pos_u64++;
          }
        }

        outs_pos += iter_max_u64;

        total_ks_pos += iter_max;

#ifdef JTR_MODE
        pos = total_ks_pos;

        if (jtr_done || event_abort)
          break;
#endif
        chain_buf->ks_pos += iter_max;

        if (chain_buf->ks_pos == chain_buf->ks_cnt)
        {
          db_entry->chains_pos++;
        }

        if (total_ks_pos == total_ks_cnt) break;
      }

      if (total_ks_pos == total_ks_cnt) break;
#ifdef JTR_MODE
      if (jtr_done || event_abort)
        break;
#endif
    }
#ifdef JTR_MODE
    if (jtr_done || event_abort)
      break;
#endif
  }

#ifndef JTR_MODE
  out_flush (out);
#endif

  /**
   * cleanup
   */
#ifdef JTR_MODE
  log_event("PRINCE done. Cleaning up.");

  if (!event_abort)
      rec_pos = total_ks_cnt;
#endif

  for (int pw_len = pw_min; pw_len <= pw_max; pw_len++)
  {
    db_entry_t *db_entry = &db_entries[pw_len];

    if (db_entry->chains_buf)
    {
      free (db_entry->chains_buf);
    }

    if (db_entry->elems_buf)  free (db_entry->elems_buf);
  }

#ifndef JTR_MODE
  free (out);
#endif
  free (wordlen_dist);
  free (pw_orders);
  free (db_entries);

#ifndef JTR_MODE
  return 0;
#else
  crk_done();
  rec_done(event_abort || (status.pass && db->salts));
#endif
}

#endif /* HAVE_LIBGMP */