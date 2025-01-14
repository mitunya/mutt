/*
 * Copyright (C) 1996-2000,2006-2007,2010 Michael R. Elkins <me@mutt.org>, and others
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "mapping.h"
#include "keymap.h"
#include "mailbox.h"
#include "copy.h"
#include "mime.h"
#include "mutt_menu.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>

#include "mutt_crypt.h"
#include "mutt_curses.h"
#include "group.h"

#ifdef USE_IMAP
#include "mx.h"
#include "imap/imap.h"
#endif

static int eat_regexp (pattern_t *pat, int, BUFFER *, BUFFER *);
static int eat_date (pattern_t *pat, int, BUFFER *, BUFFER *);
static int eat_range (pattern_t *pat, int, BUFFER *, BUFFER *);
static int patmatch (const pattern_t *pat, const char *buf);

/* Values for pattern_flags.eat_arg */
#define EAT_REGEXP	1
#define EAT_DATE	2
#define EAT_RANGE	3

static const struct pattern_flags
{
  int tag;	/* character used to represent this op */
  int op;	/* operation to perform */
  int class;
  int eat_arg;
  char *desc;
}
Flags[] =
{
  { 'A', MUTT_ALL, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~A
    */
    N_("all messages") },
  { 'b', MUTT_BODY,  MUTT_FULL_MSG|MUTT_SEND_MODE_SEARCH, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~b
    */
    N_("messages whose body matches EXPR") },
  { 'B', MUTT_WHOLE_MSG,  MUTT_FULL_MSG|MUTT_SEND_MODE_SEARCH, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~B
    */
    N_("messages whose body or headers match EXPR") },
  { 'c', MUTT_CC, 0, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~c
    */
    N_("messages whose CC header matches EXPR") },
  { 'C', MUTT_RECIPIENT, 0, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~C
    */
    N_("messages whose recipient matches EXPR") },
  { 'd', MUTT_DATE, 0, EAT_DATE,
    /* L10N:
       Pattern Completion Menu description for ~d
    */
    N_("messages sent in DATERANGE") },
  { 'D', MUTT_DELETED, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~D
    */
    N_("deleted messages") },
  { 'e', MUTT_SENDER, 0, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~e
    */
    N_("messages whose Sender header matches EXPR") },
  { 'E', MUTT_EXPIRED, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~E
    */
    N_("expired messages") },
  { 'f', MUTT_FROM, 0, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~f
    */
    N_("messages whose From header matches EXPR") },
  { 'F', MUTT_FLAG, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~F
    */
    N_("flagged messages") },
  { 'g', MUTT_CRYPT_SIGN,  0,  0,
    /* L10N:
       Pattern Completion Menu description for ~g
    */
    N_("cryptographically signed messages") },
  { 'G', MUTT_CRYPT_ENCRYPT, 0,  0,
    /* L10N:
       Pattern Completion Menu description for ~G
    */
    N_("cryptographically encrypted messages") },
  { 'h', MUTT_HEADER,  MUTT_FULL_MSG|MUTT_SEND_MODE_SEARCH, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~h
    */
    N_("messages whose header matches EXPR") },
  { 'H', MUTT_HORMEL, 0, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~H
    */
    N_("messages whose spam tag matches EXPR") },
  { 'i', MUTT_ID, 0, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~i
    */
    N_("messages whose Message-ID matches EXPR") },
  { 'k', MUTT_PGP_KEY,  0,  0,
    /* L10N:
       Pattern Completion Menu description for ~k
    */
    N_("messages which contain PGP key") },
  { 'l', MUTT_LIST, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~l
    */
    N_("messages addressed to known mailing lists") },
  { 'L', MUTT_ADDRESS, 0, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~L
    */
    N_("messages whose From/Sender/To/CC matches EXPR") },
  { 'm', MUTT_MESSAGE, 0, EAT_RANGE,
    /* L10N:
       Pattern Completion Menu description for ~m
    */
    N_("messages whose number is in RANGE") },
  { 'M', MUTT_MIMETYPE, MUTT_FULL_MSG, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~M
    */
    N_("messages with a Content-Type matching EXPR") },
  { 'n', MUTT_SCORE, 0, EAT_RANGE,
    /* L10N:
       Pattern Completion Menu description for ~n
    */
    N_("messages whose score is in RANGE") },
  { 'N', MUTT_NEW, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~N
    */
    N_("new messages") },
  { 'O', MUTT_OLD, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~O
    */
    N_("old messages") },
  { 'p', MUTT_PERSONAL_RECIP, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~p
    */
    N_("messages addressed to you") },
  { 'P', MUTT_PERSONAL_FROM, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~P
    */
    N_("messages from you") },
  { 'Q', MUTT_REPLIED, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~Q
    */
    N_("messages which have been replied to") },
  { 'r', MUTT_DATE_RECEIVED, 0, EAT_DATE,
    /* L10N:
       Pattern Completion Menu description for ~r
    */
    N_("messages received in DATERANGE") },
  { 'R', MUTT_READ, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~R
    */
    N_("already read messages") },
  { 's', MUTT_SUBJECT, 0, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~s
    */
    N_("messages whose Subject header matches EXPR") },
  { 'S', MUTT_SUPERSEDED, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~S

       An email header, Supersedes: or Supercedes:, can specify a
       message-id.  The intent is to say, "the original message with
       this message-id should be considered incorrect or out of date,
       and this email should be the actual email."

       The ~S pattern will select those "out of date/incorrect" emails
       referenced by another email's Supersedes header.
    */
    N_("superseded messages") },
  { 't', MUTT_TO, 0, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~t
    */
    N_("messages whose To header matches EXPR") },
  { 'T', MUTT_TAG, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~T
    */
    N_("tagged messages") },
  { 'u', MUTT_SUBSCRIBED_LIST, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~u
    */
    N_("messages addressed to subscribed mailing lists") },
  { 'U', MUTT_UNREAD, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~U
    */
    N_("unread messages") },
  { 'v', MUTT_COLLAPSED, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~v
    */
    N_("messages in collapsed threads") },
  { 'V', MUTT_CRYPT_VERIFIED, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~V
    */
    N_("cryptographically verified messages") },
  { 'x', MUTT_REFERENCE, 0, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~x
    */
    N_("messages whose References header matches EXPR") },
  { 'X', MUTT_MIMEATTACH, 0, EAT_RANGE,
    /* L10N:
       Pattern Completion Menu description for ~X
    */
    N_("messages with RANGE attachments") },
  { 'y', MUTT_XLABEL, 0, EAT_REGEXP,
    /* L10N:
       Pattern Completion Menu description for ~y
    */
    N_("messages whose X-Label header matches EXPR") },
  { 'z', MUTT_SIZE, 0, EAT_RANGE,
    /* L10N:
       Pattern Completion Menu description for ~z
    */
    N_("messages whose size is in RANGE") },
  { '=', MUTT_DUPLICATED, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~=
    */
    N_("duplicated messages") },
  { '$', MUTT_UNREFERENCED, 0, 0,
    /* L10N:
       Pattern Completion Menu description for ~$
    */
    N_("unreferenced messages") },
  { 0, 0, 0, 0, NULL }
};

static pattern_t *SearchPattern = NULL; /* current search pattern */
static char LastSearch[STRING] = { 0 };	/* last pattern searched for */
static char LastSearchExpn[LONG_STRING] = { 0 }; /* expanded version of
						    LastSearch */

#define MUTT_MAXRANGE -1

/* constants for parse_date_range() */
#define MUTT_PDR_NONE	0x0000
#define MUTT_PDR_MINUS	0x0001
#define MUTT_PDR_PLUS	0x0002
#define MUTT_PDR_WINDOW	0x0004
#define MUTT_PDR_ABSOLUTE	0x0008
#define MUTT_PDR_DONE	0x0010
#define MUTT_PDR_ERROR	0x0100
#define MUTT_PDR_ERRORDONE	(MUTT_PDR_ERROR | MUTT_PDR_DONE)


/* if no uppercase letters are given, do a case-insensitive search */
int mutt_which_case (const char *s)
{
  wchar_t w;
  mbstate_t mb;
  size_t l;

  memset (&mb, 0, sizeof (mb));

  for (; (l = mbrtowc (&w, s, MB_CUR_MAX, &mb)) != 0; s += l)
  {
    if (l == (size_t) -2)
      continue; /* shift sequences */
    if (l == (size_t) -1)
      return 0; /* error; assume case-sensitive */
    if (iswalpha ((wint_t) w) && iswupper ((wint_t) w))
      return 0; /* case-sensitive */
  }

  return REG_ICASE; /* case-insensitive */
}

static int
msg_search (CONTEXT *ctx, pattern_t* pat, int msgno)
{
  BUFFER *tempfile = NULL;
  MESSAGE *msg = NULL;
  STATE s;
  struct stat st;
  FILE *fp = NULL;
  LOFF_T lng = 0;
  int match = 0;
  HEADER *h = ctx->hdrs[msgno];
  char *buf;
  size_t blen;

  /* The third parameter is whether to download only headers.
   * When the user has $message_cachedir set, they likely expect to
   * "take the hit" once and have it be cached than ~h to bypass the
   * message cache completely, since this was the previous behavior.
   */
  if ((msg = mx_open_message (ctx, msgno,
                              (pat->op == MUTT_HEADER
#if defined(USE_IMAP) || defined(USE_POP)
                               && !MessageCachedir
#endif
                                ))) != NULL)
  {
    if (option (OPTTHOROUGHSRC))
    {
      /* decode the header / body */
      memset (&s, 0, sizeof (s));
      s.fpin = msg->fp;
      s.flags = MUTT_CHARCONV;

      tempfile = mutt_buffer_new ();
      mutt_buffer_mktemp (tempfile);
      if ((s.fpout = safe_fopen (mutt_b2s (tempfile), "w+")) == NULL)
      {
	mutt_perror (mutt_b2s (tempfile));
	goto cleanup;
      }

      if (pat->op != MUTT_BODY)
	mutt_copy_header (msg->fp, h, s.fpout, CH_FROM | CH_DECODE, NULL);

      if (pat->op != MUTT_HEADER)
      {
	mutt_parse_mime_message (ctx, h);

	if (WithCrypto && (h->security & ENCRYPT)
            && !crypt_valid_passphrase(h->security))
	{
	  mx_close_message (ctx, &msg);
	  if (s.fpout)
	  {
	    safe_fclose (&s.fpout);
	    unlink (mutt_b2s (tempfile));
	  }
	  goto cleanup;
	}

	fseeko (msg->fp, h->offset, SEEK_SET);
	mutt_body_handler (h->content, &s);
      }

      fp = s.fpout;
      fflush (fp);
      fseek (fp, 0, SEEK_SET);
      fstat (fileno (fp), &st);
      lng = (LOFF_T) st.st_size;
    }
    else
    {
      /* raw header / body */
      fp = msg->fp;
      if (pat->op != MUTT_BODY)
      {
	fseeko (fp, h->offset, SEEK_SET);
	lng = h->content->offset - h->offset;
      }
      if (pat->op != MUTT_HEADER)
      {
	if (pat->op == MUTT_BODY)
	  fseeko (fp, h->content->offset, SEEK_SET);
	lng += h->content->length;
      }
    }

    blen = STRING;
    buf = safe_malloc (blen);

    /* search the file "fp" */
    while (lng > 0)
    {
      if (pat->op == MUTT_HEADER)
      {
	if (*(buf = mutt_read_rfc822_line (fp, buf, &blen)) == '\0')
	  break;
      }
      else if (fgets (buf, blen - 1, fp) == NULL)
	break; /* don't loop forever */
      if (patmatch (pat, buf) == 0)
      {
	match = 1;
	break;
      }
      lng -= mutt_strlen (buf);
    }

    FREE (&buf);

    mx_close_message (ctx, &msg);

    if (option (OPTTHOROUGHSRC))
    {
      safe_fclose (&fp);
      unlink (mutt_b2s (tempfile));
    }
  }

cleanup:
  mutt_buffer_free (&tempfile);
  return match;
}

static int msg_search_sendmode (HEADER *h, pattern_t *pat)
{
  BUFFER *tempfile = NULL;
  int match = 0;
  char *buf = NULL;
  size_t blen = 0;
  FILE *fp = NULL;

  if (pat->op == MUTT_HEADER || pat->op == MUTT_WHOLE_MSG)
  {
    tempfile = mutt_buffer_pool_get ();
    mutt_buffer_mktemp (tempfile);
    if ((fp = safe_fopen (mutt_b2s (tempfile), "w+")) == NULL)
    {
      mutt_perror (mutt_b2s (tempfile));
      mutt_buffer_pool_release (&tempfile);
      return 0;
    }

    mutt_write_rfc822_header (fp, h->env, h->content, NULL,
                              MUTT_WRITE_HEADER_POSTPONE,
                              0, 0);
    fflush (fp);
    fseek (fp, 0, SEEK_SET);

    while ((buf = mutt_read_line (buf, &blen, fp, NULL, 0)) != NULL)
    {
      if (patmatch (pat, buf) == 0)
      {
        match = 1;
        break;
      }
    }

    FREE (&buf);
    safe_fclose (&fp);
    unlink (mutt_b2s (tempfile));
    mutt_buffer_pool_release (&tempfile);

    if (match)
      return match;
  }

  if (pat->op == MUTT_BODY || pat->op == MUTT_WHOLE_MSG)
  {

    if ((fp = safe_fopen (h->content->filename, "r")) == NULL)
    {
      mutt_perror (h->content->filename);
      return 0;
    }

    while ((buf = mutt_read_line (buf, &blen, fp, NULL, 0)) != NULL)
    {
      if (patmatch (pat, buf) == 0)
      {
        match = 1;
        break;
      }
    }

    FREE (&buf);
    safe_fclose (&fp);
  }

  return match;
}

static int eat_regexp (pattern_t *pat, int flags, BUFFER *s, BUFFER *err)
{
  BUFFER buf;
  char errmsg[STRING];
  int r;
  char *pexpr;

  mutt_buffer_init (&buf);
  pexpr = s->dptr;
  if (mutt_extract_token (&buf, s, MUTT_TOKEN_PATTERN | MUTT_TOKEN_COMMENT) != 0 ||
      !buf.data)
  {
    snprintf (err->data, err->dsize, _("Error in expression: %s"), pexpr);
    FREE (&buf.data);
    return (-1);
  }
  if (!*buf.data)
  {
    snprintf (err->data, err->dsize, "%s", _("Empty expression"));
    FREE (&buf.data);
    return (-1);
  }

  if (pat->stringmatch)
  {
    pat->p.str = safe_strdup (buf.data);
    pat->ign_case = mutt_which_case (buf.data) == REG_ICASE;
    FREE (&buf.data);
  }
  else if (pat->groupmatch)
  {
    pat->p.g = mutt_pattern_group (buf.data);
    FREE (&buf.data);
  }
  else
  {
    pat->p.rx = safe_malloc (sizeof (regex_t));
    r = REGCOMP (pat->p.rx, buf.data, REG_NEWLINE | REG_NOSUB | mutt_which_case (buf.data));
    if (r)
    {
      regerror (r, pat->p.rx, errmsg, sizeof (errmsg));
      mutt_buffer_add_printf (err, "'%s': %s", buf.data, errmsg);
      FREE (&buf.data);
      FREE (&pat->p.rx);
      return (-1);
    }
    FREE (&buf.data);
  }

  return 0;
}

static int eat_range (pattern_t *pat, int flags, BUFFER *s, BUFFER *err)
{
  char *tmp;
  int do_exclusive = 0;
  int skip_quote = 0;

  /*
   * If simple_search is set to "~m %s", the range will have double quotes
   * around it...
   */
  if (*s->dptr == '"')
  {
    s->dptr++;
    skip_quote = 1;
  }
  if (*s->dptr == '<')
    do_exclusive = 1;
  if ((*s->dptr != '-') && (*s->dptr != '<'))
  {
    /* range minimum */
    if (*s->dptr == '>')
    {
      pat->max = MUTT_MAXRANGE;
      pat->min = strtol (s->dptr + 1, &tmp, 0) + 1; /* exclusive range */
    }
    else
      pat->min = strtol (s->dptr, &tmp, 0);
    if (toupper ((unsigned char) *tmp) == 'K') /* is there a prefix? */
    {
      pat->min *= 1024;
      tmp++;
    }
    else if (toupper ((unsigned char) *tmp) == 'M')
    {
      pat->min *= 1048576;
      tmp++;
    }
    if (*s->dptr == '>')
    {
      s->dptr = tmp;
      return 0;
    }
    if (*tmp != '-')
    {
      /* exact value */
      pat->max = pat->min;
      s->dptr = tmp;
      return 0;
    }
    tmp++;
  }
  else
  {
    s->dptr++;
    tmp = s->dptr;
  }

  if (isdigit ((unsigned char) *tmp))
  {
    /* range maximum */
    pat->max = strtol (tmp, &tmp, 0);
    if (toupper ((unsigned char) *tmp) == 'K')
    {
      pat->max *= 1024;
      tmp++;
    }
    else if (toupper ((unsigned char) *tmp) == 'M')
    {
      pat->max *= 1048576;
      tmp++;
    }
    if (do_exclusive)
      (pat->max)--;
  }
  else
    pat->max = MUTT_MAXRANGE;

  if (skip_quote && *tmp == '"')
    tmp++;

  SKIPWS (tmp);
  s->dptr = tmp;
  return 0;
}

static const char *getDate (const char *s, struct tm *t, BUFFER *err)
{
  char *p;
  time_t now = time (NULL);
  struct tm *tm = localtime (&now);
  int iso8601=1;
  int v=0;

  for (v=0; v<8; v++)
  {
    if (s[v] && s[v] >= '0' && s[v] <= '9')
    {
      continue;
    }
    iso8601 = 0;
    break;
  }

  if (iso8601)
  {
    int year;
    int month;
    int mday;
    sscanf (s, "%4d%2d%2d", &year, &month, &mday);

    t->tm_year = year;
    if (t->tm_year > 1900)
      t->tm_year -= 1900;
    t->tm_mon = month - 1;
    t->tm_mday = mday;

    if (t->tm_mday < 1 || t->tm_mday > 31)
    {
      snprintf (err->data, err->dsize, _("Invalid day of month: %s"), s);
      return NULL;
    }
    if (t->tm_mon < 0 || t->tm_mon > 11)
    {
      snprintf (err->data, err->dsize, _("Invalid month: %s"), s);
      return NULL;
    }

    return (s+8);
  }

  t->tm_mday = strtol (s, &p, 10);
  if (t->tm_mday < 1 || t->tm_mday > 31)
  {
    snprintf (err->data, err->dsize, _("Invalid day of month: %s"), s);
    return NULL;
  }
  if (*p != '/')
  {
    /* fill in today's month and year */
    t->tm_mon = tm->tm_mon;
    t->tm_year = tm->tm_year;
    return p;
  }
  p++;
  t->tm_mon = strtol (p, &p, 10) - 1;
  if (t->tm_mon < 0 || t->tm_mon > 11)
  {
    snprintf (err->data, err->dsize, _("Invalid month: %s"), p);
    return NULL;
  }
  if (*p != '/')
  {
    t->tm_year = tm->tm_year;
    return p;
  }
  p++;
  t->tm_year = strtol (p, &p, 10);
  if (t->tm_year < 70) /* year 2000+ */
    t->tm_year += 100;
  else if (t->tm_year > 1900)
    t->tm_year -= 1900;
  return p;
}

/* Ny	years
   Nm	months
   Nw	weeks
   Nd	days */
static const char *get_offset (struct tm *tm, const char *s, int sign)
{
  char *ps;
  int offset = strtol (s, &ps, 0);
  if ((sign < 0 && offset > 0) || (sign > 0 && offset < 0))
    offset = -offset;

  switch (*ps)
  {
    case 'y':
      tm->tm_year += offset;
      break;
    case 'm':
      tm->tm_mon += offset;
      break;
    case 'w':
      tm->tm_mday += 7 * offset;
      break;
    case 'd':
      tm->tm_mday += offset;
      break;
    case 'H':
      tm->tm_hour += offset;
      break;
    case 'M':
      tm->tm_min += offset;
      break;
    case 'S':
      tm->tm_sec += offset;
      break;
    default:
      return s;
  }
  mutt_normalize_time (tm);
  return (ps + 1);
}

static void adjust_date_range (struct tm *min, struct tm *max)
{
  if (min->tm_year > max->tm_year
      || (min->tm_year == max->tm_year && min->tm_mon > max->tm_mon)
      || (min->tm_year == max->tm_year && min->tm_mon == max->tm_mon
          && min->tm_mday > max->tm_mday))
  {
    int tmp;

    tmp = min->tm_year;
    min->tm_year = max->tm_year;
    max->tm_year = tmp;

    tmp = min->tm_mon;
    min->tm_mon = max->tm_mon;
    max->tm_mon = tmp;

    tmp = min->tm_mday;
    min->tm_mday = max->tm_mday;
    max->tm_mday = tmp;

    min->tm_hour = min->tm_min = min->tm_sec = 0;
    max->tm_hour = 23;
    max->tm_min = max->tm_sec = 59;
  }
}

static const char * parse_date_range (const char* pc, struct tm *min,
                                      struct tm *max, int haveMin,
                                      struct tm *baseMin, BUFFER *err)
{
  int flag = MUTT_PDR_NONE;
  while (*pc && ((flag & MUTT_PDR_DONE) == 0))
  {
    const char *pt;
    char ch = *pc++;
    SKIPWS (pc);
    switch (ch)
    {
      case '-':
      {
	/* try a range of absolute date minus offset of Ndwmy */
	pt = get_offset (min, pc, -1);
	if (pc == pt)
	{
	  if (flag == MUTT_PDR_NONE)
	  { /* nothing yet and no offset parsed => absolute date? */
	    if (!getDate (pc, max, err))
	      flag |= (MUTT_PDR_ABSOLUTE | MUTT_PDR_ERRORDONE);  /* done bad */
	    else
	    {
	      /* reestablish initial base minimum if not specified */
	      if (!haveMin)
		memcpy (min, baseMin, sizeof(struct tm));
	      flag |= (MUTT_PDR_ABSOLUTE | MUTT_PDR_DONE);  /* done good */
	    }
	  }
	  else
	    flag |= MUTT_PDR_ERRORDONE;
	}
	else
	{
	  pc = pt;
	  if (flag == MUTT_PDR_NONE && !haveMin)
	  { /* the very first "-3d" without a previous absolute date */
	    max->tm_year = min->tm_year;
	    max->tm_mon = min->tm_mon;
	    max->tm_mday = min->tm_mday;
	  }
	  flag |= MUTT_PDR_MINUS;
	}
      }
      break;
      case '+':
      { /* enlarge plusRange */
	pt = get_offset (max, pc, 1);
	if (pc == pt)
	  flag |= MUTT_PDR_ERRORDONE;
	else
	{
	  pc = pt;
	  flag |= MUTT_PDR_PLUS;
	}
      }
      break;
      case '*':
      { /* enlarge window in both directions */
	pt = get_offset (min, pc, -1);
	if (pc == pt)
	  flag |= MUTT_PDR_ERRORDONE;
	else
	{
	  pc = get_offset (max, pc, 1);
	  flag |= MUTT_PDR_WINDOW;
	}
      }
      break;
      default:
	flag |= MUTT_PDR_ERRORDONE;
    }
    SKIPWS (pc);
  }
  if ((flag & MUTT_PDR_ERROR) && !(flag & MUTT_PDR_ABSOLUTE))
  { /* getDate has its own error message, don't overwrite it here */
    snprintf (err->data, err->dsize, _("Invalid relative date: %s"), pc-1);
  }
  return ((flag & MUTT_PDR_ERROR) ? NULL : pc);
}

static int eval_date_minmax (pattern_t *pat, const char *s, BUFFER *err)
{
  struct tm min, max;
  char *offset_type;

  memset (&min, 0, sizeof (min));
  /* the `0' time is Jan 1, 1970 UTC, so in order to prevent a negative time
     when doing timezone conversion, we use Jan 2, 1970 UTC as the base
     here */
  min.tm_mday = 2;
  min.tm_year = 70;

  memset (&max, 0, sizeof (max));

  /* Arbitrary year in the future.  Don't set this too high
     or mutt_mktime() returns something larger than will
     fit in a time_t on some systems */
  max.tm_year = 130;
  max.tm_mon = 11;
  max.tm_mday = 31;
  max.tm_hour = 23;
  max.tm_min = 59;
  max.tm_sec = 59;

  if (strchr ("<>=", s[0]))
  {
    /* offset from current time
       <3d	less than three days ago
       >3d	more than three days ago
       =3d	exactly three days ago */
    time_t now = time (NULL);
    struct tm *tm = localtime (&now);
    int exact = 0;

    if (s[0] == '<')
    {
      memcpy (&min, tm, sizeof (min));
      tm = &min;
    }
    else
    {
      memcpy (&max, tm, sizeof (max));
      tm = &max;

      if (s[0] == '=')
	exact++;
    }

    /* Reset the HMS unless we are relative matching using one of those
     * offsets. */
    strtol (s + 1, &offset_type, 0);
    if (!(*offset_type && strchr ("HMS", *offset_type)))
    {
      tm->tm_hour = 23;
      tm->tm_min = tm->tm_sec = 59;
    }

    /* force negative offset */
    get_offset (tm, s + 1, -1);

    if (exact)
    {
      /* start at the beginning of the day in question */
      memcpy (&min, &max, sizeof (max));
      min.tm_hour = min.tm_sec = min.tm_min = 0;
    }
  }
  else
  {
    const char *pc = s;

    int haveMin = FALSE;
    int untilNow = FALSE;
    if (isdigit ((unsigned char)*pc))
    {
      /* minimum date specified */
      if ((pc = getDate (pc, &min, err)) == NULL)
      {
	return (-1);
      }
      haveMin = TRUE;
      SKIPWS (pc);
      if (*pc == '-')
      {
        const char *pt = pc + 1;
	SKIPWS (pt);
	untilNow = (*pt == '\0');
      }
    }

    if (!untilNow)
    { /* max date or relative range/window */

      struct tm baseMin;

      if (!haveMin)
      { /* save base minimum and set current date, e.g. for "-3d+1d" */
	time_t now = time (NULL);
	struct tm *tm = localtime (&now);
	memcpy (&baseMin, &min, sizeof(baseMin));
	memcpy (&min, tm, sizeof (min));
	min.tm_hour = min.tm_sec = min.tm_min = 0;
      }

      /* preset max date for relative offsets,
	 if nothing follows we search for messages on a specific day */
      max.tm_year = min.tm_year;
      max.tm_mon = min.tm_mon;
      max.tm_mday = min.tm_mday;

      if (!parse_date_range (pc, &min, &max, haveMin, &baseMin, err))
      { /* bail out on any parsing error */
	return (-1);
      }
    }
  }

  /* Since we allow two dates to be specified we'll have to adjust that. */
  adjust_date_range (&min, &max);

  pat->min = mutt_mktime (&min, 1);
  pat->max = mutt_mktime (&max, 1);

  return 0;
}

static int eat_date (pattern_t *pat, int flags, BUFFER *s, BUFFER *err)
{
  BUFFER buffer;
  char *pexpr;
  int rc = -1;

  mutt_buffer_init (&buffer);
  pexpr = s->dptr;
  if (mutt_extract_token (&buffer, s, MUTT_TOKEN_COMMENT | MUTT_TOKEN_PATTERN) != 0
      || !buffer.data)
  {
    snprintf (err->data, err->dsize, _("Error in expression: %s"), pexpr);
    goto out;
  }
  if (!*buffer.data)
  {
    snprintf (err->data, err->dsize, "%s", _("Empty expression"));
    goto out;
  }

  if (flags & MUTT_PATTERN_DYNAMIC)
  {
    pat->dynamic = 1;
    pat->p.str = safe_strdup (buffer.data);
  }

  rc = eval_date_minmax (pat, buffer.data, err);

out:
  FREE (&buffer.data);

  return rc;
}

static int patmatch (const pattern_t* pat, const char* buf)
{
  if (pat->stringmatch)
    return pat->ign_case ? !strcasestr (buf, pat->p.str) :
      !strstr (buf, pat->p.str);
  else if (pat->groupmatch)
    return !mutt_group_match (pat->p.g, buf);
  else
    return regexec (pat->p.rx, buf, 0, NULL, 0);
}

static const struct pattern_flags *lookup_tag (char tag)
{
  int i;

  for (i = 0; Flags[i].tag; i++)
    if (Flags[i].tag == tag)
      return (&Flags[i]);
  return NULL;
}

static const struct pattern_flags *lookup_op (int op)
{
  int i;

  for (i = 0; Flags[i].tag; i++)
    if (Flags[i].op == op)
      return (&Flags[i]);
  return NULL;
}

static void print_crypt_pattern_op_error (int op)
{
  const struct pattern_flags *entry;

  entry = lookup_op (op);
  if (entry)
  {
    /* L10N:
       One of the crypt pattern modifiers: ~g, ~G, ~k, ~V
       was invoked when Mutt was compiled without crypto support.
       %c is the pattern character, i.e. "g".
    */
    mutt_error (_("Pattern modifier '~%c' is disabled."), entry->tag);
  }
  else
  {
    /* L10N:
       An unknown pattern modifier was somehow invoked.  This
       shouldn't be possible unless there is a bug.
    */
    mutt_error (_("error: unknown op %d (report this error)."), op);
  }

}

static /* const */ char *find_matching_paren (/* const */ char *s)
{
  int level = 1;

  for (; *s; s++)
  {
    if (*s == '(')
      level++;
    else if (*s == ')')
    {
      level--;
      if (!level)
	break;
    }
  }
  return s;
}

void mutt_pattern_free (pattern_t **pat)
{
  pattern_t *tmp;

  while (*pat)
  {
    tmp = *pat;
    *pat = (*pat)->next;

    if (tmp->stringmatch || tmp->dynamic)
      FREE (&tmp->p.str);
    else if (tmp->groupmatch)
      tmp->p.g = NULL;
    else if (tmp->p.rx)
    {
      regfree (tmp->p.rx);
      FREE (&tmp->p.rx);
    }

    if (tmp->child)
      mutt_pattern_free (&tmp->child);
    FREE (&tmp);
  }
}

pattern_t *mutt_pattern_comp (/* const */ char *s, int flags, BUFFER *err)
{
  pattern_t *curlist = NULL;
  pattern_t *tmp, *tmp2;
  pattern_t *last = NULL;
  int not = 0;
  int alladdr = 0;
  int or = 0;
  int implicit = 1;	/* used to detect logical AND operator */
  int isalias = 0;
  short thread_op;
  const struct pattern_flags *entry;
  char *p;
  char *buf;
  BUFFER ps;

  if (!s || !*s)
  {
    strfcpy (err->data, _("empty pattern"), err->dsize);
    return NULL;
  }

  mutt_buffer_init (&ps);
  ps.dptr = s;
  ps.dsize = mutt_strlen (s);

  SKIPWS (ps.dptr);
  while (*ps.dptr)
  {
    switch (*ps.dptr)
    {
      case '^':
	ps.dptr++;
	alladdr = !alladdr;
	break;
      case '!':
	ps.dptr++;
	not = !not;
	break;
      case '@':
	ps.dptr++;
	isalias = !isalias;
	break;
      case '|':
	if (!or)
	{
	  if (!curlist)
	  {
	    snprintf (err->data, err->dsize, _("error in pattern at: %s"), ps.dptr);
	    return NULL;
	  }
	  if (curlist->next)
	  {
	    /* A & B | C == (A & B) | C */
	    tmp = new_pattern ();
	    tmp->op = MUTT_AND;
	    tmp->child = curlist;

	    curlist = tmp;
	    last = curlist;
	  }

	  or = 1;
	}
	ps.dptr++;
	implicit = 0;
	not = 0;
	alladdr = 0;
	isalias = 0;
	break;
      case '%':
      case '=':
      case '~':
	if (!*(ps.dptr + 1))
	{
	  snprintf (err->data, err->dsize, _("missing pattern: %s"), ps.dptr);
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
        thread_op = 0;
	if (*(ps.dptr + 1) == '(')
          thread_op = MUTT_THREAD;
        else if ((*(ps.dptr + 1) == '<') && (*(ps.dptr + 2) == '('))
          thread_op = MUTT_PARENT;
        else if ((*(ps.dptr + 1) == '>') && (*(ps.dptr + 2) == '('))
          thread_op = MUTT_CHILDREN;
        if (thread_op)
        {
	  ps.dptr++; /* skip ~ */
          if (thread_op == MUTT_PARENT || thread_op == MUTT_CHILDREN)
            ps.dptr++;
	  p = find_matching_paren (ps.dptr + 1);
	  if (*p != ')')
	  {
	    snprintf (err->data, err->dsize, _("mismatched brackets: %s"), ps.dptr);
	    mutt_pattern_free (&curlist);
	    return NULL;
	  }
	  tmp = new_pattern ();
	  tmp->op = thread_op;
	  if (last)
	    last->next = tmp;
	  else
	    curlist = tmp;
	  last = tmp;
	  tmp->not ^= not;
	  tmp->alladdr |= alladdr;
	  tmp->isalias |= isalias;
	  not = 0;
	  alladdr = 0;
	  isalias = 0;
	  /* compile the sub-expression */
	  buf = mutt_substrdup (ps.dptr + 1, p);
	  if ((tmp2 = mutt_pattern_comp (buf, flags, err)) == NULL)
	  {
	    FREE (&buf);
	    mutt_pattern_free (&curlist);
	    return NULL;
	  }
	  FREE (&buf);
	  tmp->child = tmp2;
	  ps.dptr = p + 1; /* restore location */
	  break;
	}
        if (implicit && or)
	{
	  /* A | B & C == (A | B) & C */
	  tmp = new_pattern ();
	  tmp->op = MUTT_OR;
	  tmp->child = curlist;
	  curlist = tmp;
	  last = tmp;
	  or = 0;
	}

	tmp = new_pattern ();
	tmp->not = not;
	tmp->alladdr = alladdr;
	tmp->isalias = isalias;
        tmp->stringmatch = (*ps.dptr == '=') ? 1 : 0;
        tmp->groupmatch  = (*ps.dptr == '%') ? 1 : 0;
	not = 0;
	alladdr = 0;
	isalias = 0;

	if (last)
	  last->next = tmp;
	else
	  curlist = tmp;
	last = tmp;

	ps.dptr++; /* move past the ~ */
	if ((entry = lookup_tag (*ps.dptr)) == NULL)
	{
	  snprintf (err->data, err->dsize, _("%c: invalid pattern modifier"), *ps.dptr);
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
	if (entry->class && (flags & entry->class) == 0)
	{
	  snprintf (err->data, err->dsize, _("%c: not supported in this mode"), *ps.dptr);
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
        if (flags & MUTT_SEND_MODE_SEARCH)
          tmp->sendmode = 1;

	tmp->op = entry->op;

	ps.dptr++; /* eat the operator and any optional whitespace */
	SKIPWS (ps.dptr);

	if (entry->eat_arg)
	{
	  int eatrv = 0;
	  if (!*ps.dptr)
	  {
	    snprintf (err->data, err->dsize, "%s", _("missing parameter"));
	    mutt_pattern_free (&curlist);
	    return NULL;
	  }
	  switch (entry->eat_arg)
	  {
	    case EAT_REGEXP:
              eatrv = eat_regexp (tmp, flags, &ps, err);
              break;
	    case EAT_DATE:
              eatrv = eat_date (tmp, flags, &ps, err);
              break;
	    case EAT_RANGE:
              eatrv = eat_range (tmp, flags, &ps, err);
              break;
	  }
	  if (eatrv == -1)
	  {
	    mutt_pattern_free (&curlist);
	    return NULL;
	  }
	}
	implicit = 1;
	break;
      case '(':
	p = find_matching_paren (ps.dptr + 1);
	if (*p != ')')
	{
	  snprintf (err->data, err->dsize, _("mismatched parenthesis: %s"), ps.dptr);
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
	/* compile the sub-expression */
	buf = mutt_substrdup (ps.dptr + 1, p);
	if ((tmp = mutt_pattern_comp (buf, flags, err)) == NULL)
	{
	  FREE (&buf);
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
	FREE (&buf);
	if (last)
	  last->next = tmp;
	else
	  curlist = tmp;
	last = tmp;
	tmp->not ^= not;
	tmp->alladdr |= alladdr;
	tmp->isalias |= isalias;
	not = 0;
	alladdr = 0;
	isalias = 0;
	ps.dptr = p + 1; /* restore location */
	break;
      default:
	snprintf (err->data, err->dsize, _("error in pattern at: %s"), ps.dptr);
	mutt_pattern_free (&curlist);
	return NULL;
    }
    SKIPWS (ps.dptr);
  }
  if (!curlist)
  {
    strfcpy (err->data, _("empty pattern"), err->dsize);
    return NULL;
  }
  if (curlist->next)
  {
    tmp = new_pattern ();
    tmp->op = or ? MUTT_OR : MUTT_AND;
    tmp->child = curlist;
    curlist = tmp;
  }
  return (curlist);
}

static int
perform_and (pattern_t *pat, pattern_exec_flag flags, CONTEXT *ctx, HEADER *hdr, pattern_cache_t *cache)
{
  for (; pat; pat = pat->next)
    if (mutt_pattern_exec (pat, flags, ctx, hdr, cache) <= 0)
      return 0;
  return 1;
}

static int
perform_or (struct pattern_t *pat, pattern_exec_flag flags, CONTEXT *ctx, HEADER *hdr, pattern_cache_t *cache)
{
  for (; pat; pat = pat->next)
    if (mutt_pattern_exec (pat, flags, ctx, hdr, cache) > 0)
      return 1;
  return 0;
}

static int match_adrlist (pattern_t *pat, int match_personal, int n, ...)
{
  va_list ap;
  ADDRESS *a;

  va_start (ap, n);
  for ( ; n ; n --)
  {
    for (a = va_arg (ap, ADDRESS *) ; a ; a = a->next)
    {
      if (pat->alladdr ^
          ((!pat->isalias || alias_reverse_lookup (a)) &&
           ((a->mailbox && !patmatch (pat, a->mailbox)) ||
	    (match_personal && a->personal && !patmatch (pat, a->personal) ))))
      {
	va_end (ap);
	return (! pat->alladdr); /* Found match, or non-match if alladdr */
      }
    }
  }
  va_end (ap);
  return pat->alladdr; /* No matches, or all matches if alladdr */
}

static int match_reference (pattern_t *pat, LIST *refs)
{
  for (; refs; refs = refs->next)
    if (patmatch (pat, refs->data) == 0)
      return 1;
  return 0;
}

/*
 * Matches subscribed mailing lists
 */
int mutt_is_list_recipient (int alladdr, ADDRESS *a1, ADDRESS *a2)
{
  for (; a1 ; a1 = a1->next)
    if (alladdr ^ mutt_is_subscribed_list (a1))
      return (! alladdr);
  for (; a2 ; a2 = a2->next)
    if (alladdr ^ mutt_is_subscribed_list (a2))
      return (! alladdr);
  return alladdr;
}

/*
 * Matches known mailing lists
 * The function name may seem a little bit misleading: It checks all
 * recipients in To and Cc for known mailing lists, subscribed or not.
 */
int mutt_is_list_cc (int alladdr, ADDRESS *a1, ADDRESS *a2)
{
  for (; a1 ; a1 = a1->next)
    if (alladdr ^ mutt_is_mail_list (a1))
      return (! alladdr);
  for (; a2 ; a2 = a2->next)
    if (alladdr ^ mutt_is_mail_list (a2))
      return (! alladdr);
  return alladdr;
}

static int match_user (int alladdr, ADDRESS *a1, ADDRESS *a2)
{
  for (; a1 ; a1 = a1->next)
    if (alladdr ^ mutt_addr_is_user (a1))
      return (! alladdr);
  for (; a2 ; a2 = a2->next)
    if (alladdr ^ mutt_addr_is_user (a2))
      return (! alladdr);
  return alladdr;
}

static int match_threadcomplete(struct pattern_t *pat, pattern_exec_flag flags, CONTEXT *ctx, THREAD *t,int left,int up,int right,int down)
{
  int a;
  HEADER *h;

  if (!t)
    return 0;
  h = t->message;
  if (h)
    if (mutt_pattern_exec(pat, flags, ctx, h, NULL))
      return 1;

  if (up && (a=match_threadcomplete(pat, flags, ctx, t->parent,1,1,1,0)))
    return a;
  if (right && t->parent && (a=match_threadcomplete(pat, flags, ctx, t->next,0,0,1,1)))
    return a;
  if (left && t->parent && (a=match_threadcomplete(pat, flags, ctx, t->prev,1,0,0,1)))
    return a;
  if (down && (a=match_threadcomplete(pat, flags, ctx, t->child,1,0,1,1)))
    return a;
  return 0;
}

static int match_threadparent(struct pattern_t *pat, pattern_exec_flag flags, CONTEXT *ctx, THREAD *t)
{
  if (!t || !t->parent || !t->parent->message)
    return 0;

  return mutt_pattern_exec(pat, flags, ctx, t->parent->message, NULL);
}

static int match_threadchildren(struct pattern_t *pat, pattern_exec_flag flags, CONTEXT *ctx, THREAD *t)
{
  if (!t || !t->child)
    return 0;

  for (t = t->child; t; t = t->next)
    if (t->message && mutt_pattern_exec(pat, flags, ctx, t->message, NULL))
      return 1;

  return 0;
}

static int match_content_type(const pattern_t* pat, BODY *b)
{
  char buffer[STRING];
  if (!b)
    return 0;

  snprintf (buffer, STRING, "%s/%s", TYPE (b), b->subtype);

  if (patmatch (pat, buffer) == 0)
    return 1;
  if (match_content_type (pat, b->parts))
    return 1;
  if (match_content_type (pat, b->next))
    return 1;
  return 0;
}

static int match_update_dynamic_date (pattern_t *pat)
{
  BUFFER err;
  int rc;

  mutt_buffer_init (&err);
  rc = eval_date_minmax (pat, pat->p.str, &err);
  FREE (&err.data);

  return rc;
}

static int match_mime_content_type(const pattern_t *pat, CONTEXT *ctx, HEADER *hdr)
{
  mutt_parse_mime_message(ctx, hdr);
  return match_content_type(pat, hdr->content);
}

/* Sets a value in the pattern_cache_t cache entry.
 * Normalizes the "true" value to 2. */
static void set_pattern_cache_value (int *cache_entry, int value)
{
  *cache_entry = value ? 2 : 1;
}

/* Returns 1 if the cache value is set and has a true value.
 * 0 otherwise (even if unset!) */
static int get_pattern_cache_value (int cache_entry)
{
  return cache_entry == 2;
}

static int is_pattern_cache_set (int cache_entry)
{
  return cache_entry != 0;
}


/*
 * flags: MUTT_MATCH_FULL_ADDRESS - match both personal and machine address
 * cache: For repeated matches against the same HEADER, passing in non-NULL will
 *        store some of the cacheable pattern matches in this structure. */
int
mutt_pattern_exec (struct pattern_t *pat, pattern_exec_flag flags, CONTEXT *ctx, HEADER *h,
                   pattern_cache_t *cache)
{
  int result;
  int *cache_entry;

  switch (pat->op)
  {
    case MUTT_AND:
      return (pat->not ^ (perform_and (pat->child, flags, ctx, h, cache) > 0));
    case MUTT_OR:
      return (pat->not ^ (perform_or (pat->child, flags, ctx, h, cache) > 0));
    case MUTT_THREAD:
      return (pat->not ^ match_threadcomplete(pat->child, flags, ctx, h->thread, 1, 1, 1, 1));
    case MUTT_PARENT:
      return (pat->not ^ match_threadparent(pat->child, flags, ctx, h->thread));
    case MUTT_CHILDREN:
      return (pat->not ^ match_threadchildren(pat->child, flags, ctx, h->thread));
    case MUTT_ALL:
      return (!pat->not);
    case MUTT_EXPIRED:
      return (pat->not ^ h->expired);
    case MUTT_SUPERSEDED:
      return (pat->not ^ h->superseded);
    case MUTT_FLAG:
      return (pat->not ^ h->flagged);
    case MUTT_TAG:
      return (pat->not ^ h->tagged);
    case MUTT_NEW:
      return (pat->not ? h->old || h->read : !(h->old || h->read));
    case MUTT_UNREAD:
      return (pat->not ? h->read : !h->read);
    case MUTT_REPLIED:
      return (pat->not ^ h->replied);
    case MUTT_OLD:
      return (pat->not ? (!h->old || h->read) : (h->old && !h->read));
    case MUTT_READ:
      return (pat->not ^ h->read);
    case MUTT_DELETED:
      return (pat->not ^ h->deleted);
    case MUTT_MESSAGE:
      return (pat->not ^ (h->msgno >= pat->min - 1 && (pat->max == MUTT_MAXRANGE ||
                                                       h->msgno <= pat->max - 1)));
    case MUTT_DATE:
      if (pat->dynamic)
        match_update_dynamic_date (pat);
      return (pat->not ^ (h->date_sent >= pat->min && h->date_sent <= pat->max));
    case MUTT_DATE_RECEIVED:
      if (pat->dynamic)
        match_update_dynamic_date (pat);
      return (pat->not ^ (h->received >= pat->min && h->received <= pat->max));
    case MUTT_BODY:
    case MUTT_HEADER:
    case MUTT_WHOLE_MSG:
      if (pat->sendmode)
      {
        if (!h->content || !h->content->filename)
          return 0;
        return (pat->not ^ msg_search_sendmode (h, pat));
      }
      /*
       * ctx can be NULL in certain cases, such as when replying to a message from the attachment menu and
       * the user has a reply-hook using "~h" (bug #2190).
       * This is also the case when message scoring.
       */
      if (!ctx)
        return 0;
#ifdef USE_IMAP
      /* IMAP search sets h->matched at search compile time */
      if (ctx->magic == MUTT_IMAP && pat->stringmatch)
	return (h->matched);
#endif
      return (pat->not ^ msg_search (ctx, pat, h->msgno));
    case MUTT_SENDER:
      return (pat->not ^ match_adrlist (pat, flags & MUTT_MATCH_FULL_ADDRESS, 1,
                                        h->env->sender));
    case MUTT_FROM:
      return (pat->not ^ match_adrlist (pat, flags & MUTT_MATCH_FULL_ADDRESS, 1,
                                        h->env->from));
    case MUTT_TO:
      return (pat->not ^ match_adrlist (pat, flags & MUTT_MATCH_FULL_ADDRESS, 1,
                                        h->env->to));
    case MUTT_CC:
      return (pat->not ^ match_adrlist (pat, flags & MUTT_MATCH_FULL_ADDRESS, 1,
                                        h->env->cc));
    case MUTT_SUBJECT:
      return (pat->not ^ (h->env->subject && patmatch (pat, h->env->subject) == 0));
    case MUTT_ID:
      return (pat->not ^ (h->env->message_id && patmatch (pat, h->env->message_id) == 0));
    case MUTT_SCORE:
      return (pat->not ^ (h->score >= pat->min && (pat->max == MUTT_MAXRANGE ||
						   h->score <= pat->max)));
    case MUTT_SIZE:
      return (pat->not ^ (h->content->length >= pat->min && (pat->max == MUTT_MAXRANGE || h->content->length <= pat->max)));
    case MUTT_REFERENCE:
      return (pat->not ^ (match_reference (pat, h->env->references) ||
			  match_reference (pat, h->env->in_reply_to)));
    case MUTT_ADDRESS:
      return (pat->not ^ match_adrlist (pat, flags & MUTT_MATCH_FULL_ADDRESS, 4,
                                        h->env->from, h->env->sender,
                                        h->env->to, h->env->cc));
    case MUTT_RECIPIENT:
      return (pat->not ^ match_adrlist (pat, flags & MUTT_MATCH_FULL_ADDRESS,
                                        2, h->env->to, h->env->cc));
    case MUTT_LIST:	/* known list, subscribed or not */
      if (cache)
      {
        cache_entry = pat->alladdr ? &cache->list_all : &cache->list_one;
        if (!is_pattern_cache_set (*cache_entry))
          set_pattern_cache_value (cache_entry,
                                   mutt_is_list_cc (pat->alladdr, h->env->to, h->env->cc));
        result = get_pattern_cache_value (*cache_entry);
      }
      else
        result = mutt_is_list_cc (pat->alladdr, h->env->to, h->env->cc);
      return (pat->not ^ result);
    case MUTT_SUBSCRIBED_LIST:
      if (cache)
      {
        cache_entry = pat->alladdr ? &cache->sub_all : &cache->sub_one;
        if (!is_pattern_cache_set (*cache_entry))
          set_pattern_cache_value (cache_entry,
                                   mutt_is_list_recipient (pat->alladdr, h->env->to, h->env->cc));
        result = get_pattern_cache_value (*cache_entry);
      }
      else
        result = mutt_is_list_recipient (pat->alladdr, h->env->to, h->env->cc);
      return (pat->not ^ result);
    case MUTT_PERSONAL_RECIP:
      if (cache)
      {
        cache_entry = pat->alladdr ? &cache->pers_recip_all : &cache->pers_recip_one;
        if (!is_pattern_cache_set (*cache_entry))
          set_pattern_cache_value (cache_entry,
                                   match_user (pat->alladdr, h->env->to, h->env->cc));
        result = get_pattern_cache_value (*cache_entry);
      }
      else
        result = match_user (pat->alladdr, h->env->to, h->env->cc);
      return (pat->not ^ result);
    case MUTT_PERSONAL_FROM:
      if (cache)
      {
        cache_entry = pat->alladdr ? &cache->pers_from_all : &cache->pers_from_one;
        if (!is_pattern_cache_set (*cache_entry))
          set_pattern_cache_value (cache_entry,
                                   match_user (pat->alladdr, h->env->from, NULL));
        result = get_pattern_cache_value (*cache_entry);
      }
      else
        result = match_user (pat->alladdr, h->env->from, NULL);
      return (pat->not ^ result);
    case MUTT_COLLAPSED:
      return (pat->not ^ (h->collapsed && h->num_hidden > 1));
    case MUTT_CRYPT_SIGN:
      if (!WithCrypto)
      {
        print_crypt_pattern_op_error (pat->op);
        return 0;
      }
      return (pat->not ^ ((h->security & SIGN) ? 1 : 0));
    case MUTT_CRYPT_VERIFIED:
      if (!WithCrypto)
      {
        print_crypt_pattern_op_error (pat->op);
        return 0;
      }
      return (pat->not ^ ((h->security & GOODSIGN) ? 1 : 0));
    case MUTT_CRYPT_ENCRYPT:
      if (!WithCrypto)
      {
        print_crypt_pattern_op_error (pat->op);
        return 0;
      }
      return (pat->not ^ ((h->security & ENCRYPT) ? 1 : 0));
    case MUTT_PGP_KEY:
      if (!(WithCrypto & APPLICATION_PGP))
      {
        print_crypt_pattern_op_error (pat->op);
        return 0;
      }
      return (pat->not ^ ((h->security & PGPKEY) == PGPKEY));
    case MUTT_XLABEL:
      return (pat->not ^ (h->env->x_label && patmatch (pat, h->env->x_label) == 0));
    case MUTT_HORMEL:
      return (pat->not ^ (h->env->spam && h->env->spam->data && patmatch (pat, h->env->spam->data) == 0));
    case MUTT_DUPLICATED:
      return (pat->not ^ (h->thread && h->thread->duplicate_thread));
    case MUTT_MIMEATTACH:
      if (!ctx)
        return 0;
      {
        int count = mutt_count_body_parts (ctx, h);
        return (pat->not ^ (count >= pat->min && (pat->max == MUTT_MAXRANGE ||
                                                  count <= pat->max)));
      }
    case MUTT_MIMETYPE:
      if (!ctx)
        return 0;
      return (pat->not ^ match_mime_content_type (pat, ctx, h));
    case MUTT_UNREFERENCED:
      return (pat->not ^ (h->thread && !h->thread->child));
  }
  mutt_error (_("error: unknown op %d (report this error)."), pat->op);
  return (0);
}

static void quote_simple (BUFFER *tmp, const char *p)
{
  mutt_buffer_clear (tmp);
  mutt_buffer_addch (tmp, '"');
  while (*p)
  {
    if (*p == '\\' || *p == '"')
      mutt_buffer_addch (tmp, '\\');
    mutt_buffer_addch (tmp, *p++);
  }
  mutt_buffer_addch (tmp, '"');
}

/* convert a simple search into a real request */
void mutt_check_simple (BUFFER *s, const char *simple)
{
  BUFFER *tmp = NULL;
  int do_simple = 1;
  const char *p;

  for (p = mutt_b2s (s); p && *p; p++)
  {
    if (*p == '\\' && *(p + 1))
      p++;
    else if (*p == '~' || *p == '=' || *p == '%')
    {
      do_simple = 0;
      break;
    }
  }

  /* XXX - is ascii_strcasecmp() right here, or should we use locale's
   * equivalences?
   */

  if (do_simple) /* yup, so spoof a real request */
  {
    /* convert old tokens into the new format */
    if (ascii_strcasecmp ("all", mutt_b2s (s)) == 0 ||
	!mutt_strcmp ("^", mutt_b2s (s)) ||
        !mutt_strcmp (".", mutt_b2s (s))) /* ~A is more efficient */
      mutt_buffer_strcpy (s, "~A");
    else if (ascii_strcasecmp ("del", mutt_b2s (s)) == 0)
      mutt_buffer_strcpy (s, "~D");
    else if (ascii_strcasecmp ("flag", mutt_b2s (s)) == 0)
      mutt_buffer_strcpy (s, "~F");
    else if (ascii_strcasecmp ("new", mutt_b2s (s)) == 0)
      mutt_buffer_strcpy (s, "~N");
    else if (ascii_strcasecmp ("old", mutt_b2s (s)) == 0)
      mutt_buffer_strcpy (s, "~O");
    else if (ascii_strcasecmp ("repl", mutt_b2s (s)) == 0)
      mutt_buffer_strcpy (s, "~Q");
    else if (ascii_strcasecmp ("read", mutt_b2s (s)) == 0)
      mutt_buffer_strcpy (s, "~R");
    else if (ascii_strcasecmp ("tag", mutt_b2s (s)) == 0)
      mutt_buffer_strcpy (s, "~T");
    else if (ascii_strcasecmp ("unread", mutt_b2s (s)) == 0)
      mutt_buffer_strcpy (s, "~U");
    else
    {
      tmp = mutt_buffer_pool_get ();
      quote_simple (tmp, mutt_b2s (s));
      mutt_expand_fmt (s, simple, mutt_b2s (tmp));
      mutt_buffer_pool_release (&tmp);
    }
  }
}

int mutt_pattern_func (int op, char *prompt)
{
  pattern_t *pat = NULL;
  BUFFER *buf = NULL;
  char *simple = NULL;
  BUFFER err;
  int i, rv = -1, padding, interrupted = 0;
  progress_t progress;

  buf = mutt_buffer_pool_get ();

  mutt_buffer_strcpy (buf, NONULL (Context->pattern));
  if ((mutt_buffer_get_field (prompt, buf, MUTT_PATTERN | MUTT_CLEAR) != 0) ||
      !mutt_buffer_len (buf))
  {
    mutt_buffer_pool_release (&buf);
    return (-1);
  }

  mutt_message _("Compiling search pattern...");

  simple = safe_strdup (mutt_b2s (buf));
  mutt_check_simple (buf, NONULL (SimpleSearch));

  mutt_buffer_init (&err);
  err.dsize = STRING;
  err.data = safe_malloc(err.dsize);
  if ((pat = mutt_pattern_comp (buf->data, MUTT_FULL_MSG, &err)) == NULL)
  {
    mutt_error ("%s", err.data);
    goto bail;
  }

#ifdef USE_IMAP
  if (Context->magic == MUTT_IMAP && imap_search (Context, pat) < 0)
    goto bail;
#endif

  mutt_progress_init (&progress, _("Executing command on matching messages..."),
		      MUTT_PROGRESS_MSG, ReadInc,
		      (op == MUTT_LIMIT) ? Context->msgcount : Context->vcount);

  if (op == MUTT_LIMIT)
  {
    Context->vcount    = 0;
    Context->vsize     = 0;
    Context->collapsed = 0;
    padding = mx_msg_padding_size (Context);

    for (i = 0; i < Context->msgcount; i++)
    {
      if (SigInt)
      {
        interrupted = 1;
        SigInt = 0;
        break;
      }
      mutt_progress_update (&progress, i, -1);
      /* new limit pattern implicitly uncollapses all threads */
      Context->hdrs[i]->virtual = -1;
      Context->hdrs[i]->limited = 0;
      Context->hdrs[i]->collapsed = 0;
      Context->hdrs[i]->num_hidden = 0;
      if (mutt_pattern_exec (pat, MUTT_MATCH_FULL_ADDRESS, Context, Context->hdrs[i], NULL))
      {
	BODY *this_body = Context->hdrs[i]->content;

	Context->hdrs[i]->virtual = Context->vcount;
	Context->hdrs[i]->limited = 1;
	Context->v2r[Context->vcount] = i;
	Context->vcount++;
	Context->vsize += this_body->length + this_body->offset -
          this_body->hdr_offset + padding;
      }
    }
  }
  else
  {
    for (i = 0; i < Context->vcount; i++)
    {
      if (SigInt)
      {
        interrupted = 1;
        SigInt = 0;
        break;
      }
      mutt_progress_update (&progress, i, -1);
      if (mutt_pattern_exec (pat, MUTT_MATCH_FULL_ADDRESS, Context, Context->hdrs[Context->v2r[i]], NULL))
      {
	switch (op)
	{
          case MUTT_UNDELETE:
            mutt_set_flag (Context, Context->hdrs[Context->v2r[i]], MUTT_PURGE,
                           0);
            /* fall through */
	  case MUTT_DELETE:
	    mutt_set_flag (Context, Context->hdrs[Context->v2r[i]], MUTT_DELETE,
                           (op == MUTT_DELETE));
	    break;
	  case MUTT_TAG:
	  case MUTT_UNTAG:
	    mutt_set_flag (Context, Context->hdrs[Context->v2r[i]], MUTT_TAG,
			   (op == MUTT_TAG));
	    break;
	}
      }
    }
  }

  mutt_clear_error ();

  if (op == MUTT_LIMIT)
  {
    const char *pbuf;

    /* drop previous limit pattern */
    FREE (&Context->pattern);
    if (Context->limit_pattern)
      mutt_pattern_free (&Context->limit_pattern);

    if (Context->msgcount && !Context->vcount)
      mutt_error _("No messages matched criteria.");

    /* record new limit pattern, unless match all */
    pbuf = mutt_b2s (buf);
    while (*pbuf == ' ')
      pbuf++;
    if (mutt_strcmp (pbuf, "~A") != 0)
    {
      Context->pattern = simple;
      simple = NULL; /* don't clobber it */
      Context->limit_pattern = mutt_pattern_comp (buf->data, MUTT_FULL_MSG, &err);
    }
  }

  if (interrupted)
    mutt_error _("Search interrupted.");

  rv = 0;

bail:
  mutt_buffer_pool_release (&buf);
  FREE (&simple);
  mutt_pattern_free (&pat);
  FREE (&err.data);

  return rv;
}

int mutt_search_command (int cur, int op)
{
  int i, j;
  char buf[STRING];
  int incr;
  HEADER *h;
  progress_t progress;
  const char* msg = NULL;

  if (!*LastSearch || (op != OP_SEARCH_NEXT && op != OP_SEARCH_OPPOSITE))
  {
    BUFFER *temp = NULL;

    strfcpy (buf, *LastSearch ? LastSearch : "", sizeof (buf));
    if (mutt_get_field ((op == OP_SEARCH || op == OP_SEARCH_NEXT) ?
			_("Search for: ") : _("Reverse search for: "),
			buf, sizeof (buf),
                        MUTT_CLEAR | MUTT_PATTERN) != 0 || !buf[0])
      return (-1);

    if (op == OP_SEARCH || op == OP_SEARCH_NEXT)
      unset_option (OPTSEARCHREVERSE);
    else
      set_option (OPTSEARCHREVERSE);

    /* compare the *expanded* version of the search pattern in case
       $simple_search has changed while we were searching */
    temp = mutt_buffer_pool_get ();
    mutt_buffer_strcpy (temp, buf);
    mutt_check_simple (temp, NONULL (SimpleSearch));

    if (!SearchPattern || mutt_strcmp (mutt_b2s (temp), LastSearchExpn))
    {
      BUFFER err;
      mutt_buffer_init (&err);
      set_option (OPTSEARCHINVALID);
      strfcpy (LastSearch, buf, sizeof (LastSearch));
      strfcpy (LastSearchExpn, mutt_b2s (temp), sizeof (LastSearchExpn));
      mutt_message _("Compiling search pattern...");
      mutt_pattern_free (&SearchPattern);
      err.dsize = STRING;
      err.data = safe_malloc (err.dsize);
      if ((SearchPattern = mutt_pattern_comp (temp->data, MUTT_FULL_MSG, &err)) == NULL)
      {
        mutt_buffer_pool_release (&temp);
	mutt_error ("%s", err.data);
	FREE (&err.data);
	LastSearch[0] = '\0';
	LastSearchExpn[0] = '\0';
	return (-1);
      }
      FREE (&err.data);
      mutt_clear_error ();
    }

    mutt_buffer_pool_release (&temp);
  }

  if (option (OPTSEARCHINVALID))
  {
    for (i = 0; i < Context->msgcount; i++)
      Context->hdrs[i]->searched = 0;
#ifdef USE_IMAP
    if (Context->magic == MUTT_IMAP && imap_search (Context, SearchPattern) < 0)
      return -1;
#endif
    unset_option (OPTSEARCHINVALID);
  }

  incr = (option (OPTSEARCHREVERSE)) ? -1 : 1;
  if (op == OP_SEARCH_OPPOSITE)
    incr = -incr;

  mutt_progress_init (&progress, _("Searching..."), MUTT_PROGRESS_MSG,
		      ReadInc, Context->vcount);

  for (i = cur + incr, j = 0 ; j != Context->vcount; j++)
  {
    mutt_progress_update (&progress, j, -1);
    if (i > Context->vcount - 1)
    {
      i = 0;
      if (option (OPTWRAPSEARCH))
        msg = _("Search wrapped to top.");
      else
      {
        mutt_message _("Search hit bottom without finding match");
	return (-1);
      }
    }
    else if (i < 0)
    {
      i = Context->vcount - 1;
      if (option (OPTWRAPSEARCH))
        msg = _("Search wrapped to bottom.");
      else
      {
        mutt_message _("Search hit top without finding match");
	return (-1);
      }
    }

    h = Context->hdrs[Context->v2r[i]];
    if (h->searched)
    {
      /* if we've already evaluated this message, use the cached value */
      if (h->matched)
      {
	mutt_clear_error();
	if (msg && *msg)
	  mutt_message (msg);
	return i;
      }
    }
    else
    {
      /* remember that we've already searched this message */
      h->searched = 1;
      if ((h->matched = (mutt_pattern_exec (SearchPattern, MUTT_MATCH_FULL_ADDRESS, Context, h, NULL) > 0)))
      {
	mutt_clear_error();
	if (msg && *msg)
	  mutt_message (msg);
	return i;
      }
    }

    if (SigInt)
    {
      mutt_error _("Search interrupted.");
      SigInt = 0;
      return (-1);
    }

    i += incr;
  }

  mutt_error _("Not found.");
  return (-1);
}


/* Pattern Completion Menu */

typedef struct entry
{
  int num;
  const char *tag;        /* copied to buffer if selected */
  const char *expr;       /* displayed in the menu */
  const char *descr;      /* description of pattern */
} PATTERN_ENTRY;

static const struct mapping_t PatternHelp[] = {
  { N_("Exit"),   OP_EXIT },
  { N_("Select"), OP_GENERIC_SELECT_ENTRY },
  { N_("Help"),   OP_HELP },
  { NULL,	  0 }
};

static const char *pattern_format_str (char *dest, size_t destlen, size_t col,
                                       int cols, char op, const char *src,
                                       const char *fmt, const char *ifstring,
                                       const char *elsestring,
                                       void *data, format_flag flags)
{
  PATTERN_ENTRY *entry = (PATTERN_ENTRY *)data;
  char tmp[SHORT_STRING];

  switch (op)
  {
    case 'd':
      mutt_format_s (dest, destlen, fmt, NONULL (entry->descr));
      break;
    case 'e':
      mutt_format_s (dest, destlen, fmt, NONULL (entry->expr));
      break;
    case 'n':
      snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
      snprintf (dest, destlen, tmp, entry->num);
      break;
  }

  return src;
}

static void make_pattern_entry (char *s, size_t slen, MUTTMENU *menu, int num)
{
  PATTERN_ENTRY *entry = &((PATTERN_ENTRY *)menu->data)[num];

  mutt_FormatString (s, slen, 0, MuttIndexWindow->cols,
                     NONULL (PatternFormat),
                     pattern_format_str,
		     entry, MUTT_FORMAT_ARROWCURSOR);
}

static MUTTMENU *create_pattern_menu ()
{
  MUTTMENU *menu = NULL;
  PATTERN_ENTRY *entries = NULL;
  int num_entries = 0, i = 0;
  char *helpstr;
  BUFFER *entrybuf = NULL;
  const char *patternstr;

  while (Flags[num_entries].tag)
    num_entries++;
  /* Add three more hard-coded entries */
  num_entries += 3;

  menu = mutt_new_menu (MENU_GENERIC);
  menu->make_entry = make_pattern_entry;

  /* L10N:
     Pattern completion menu title
  */
  menu->title = _("Patterns");
  helpstr = safe_malloc (STRING);
  menu->help = mutt_compile_help (helpstr, STRING, MENU_GENERIC,
                                  PatternHelp);

  menu->data = entries = safe_calloc (num_entries, sizeof(PATTERN_ENTRY));
  menu->max = num_entries;

  entrybuf = mutt_buffer_pool_get ();
  while (Flags[i].tag)
  {
    entries[i].num = i + 1;

    mutt_buffer_printf (entrybuf, "~%c", (char)Flags[i].tag);
    entries[i].tag = safe_strdup (mutt_b2s (entrybuf));

    switch (Flags[i].eat_arg)
    {
      case EAT_REGEXP:
        /* L10N:
           Pattern Completion Menu argument type: a regular expression
        */
        mutt_buffer_add_printf (entrybuf, " %s", _("EXPR"));
        break;
      case EAT_RANGE:
        /* L10N:
           Pattern Completion Menu argument type: a numeric range.
           Used by ~m, ~n, ~X, ~z.
        */
        mutt_buffer_add_printf (entrybuf, " %s", _("RANGE"));
        break;
      case EAT_DATE:
        /* L10N:
           Pattern Completion Menu argument type: a date range
           Used by ~d, ~r.
        */
        mutt_buffer_add_printf (entrybuf, " %s", _("DATERANGE"));
        break;
    }
    entries[i].expr = safe_strdup (mutt_b2s (entrybuf));
    entries[i].descr = safe_strdup (_(Flags[i].desc));

    i++;
  }

  /* Add THREAD patterns manually.
   * Note we allocated 3 extra slots for these above. */

  /* L10N:
     Pattern Completion Menu argument type: a nested pattern.
     Used by ~(), ~<(), ~>().
  */
  patternstr = _("PATTERN");

  entries[i].num = i + 1;
  entries[i].tag = safe_strdup ("~()");
  mutt_buffer_printf (entrybuf, "~(%s)", patternstr);
  entries[i].expr = safe_strdup (mutt_b2s (entrybuf));
  /* L10N:
     Pattern Completion Menu description for ~()
  */
  entries[i].descr = safe_strdup _("messages in threads containing messages matching PATTERN");
  i++;

  entries[i].num = i + 1;
  entries[i].tag = safe_strdup ("~<()");
  mutt_buffer_printf (entrybuf, "~<(%s)", patternstr);
  entries[i].expr = safe_strdup (mutt_b2s (entrybuf));
  /* L10N:
     Pattern Completion Menu description for ~<()
  */
  entries[i].descr = safe_strdup _("messages whose immediate parent matches PATTERN");
  i++;

  entries[i].num = i + 1;
  entries[i].tag = safe_strdup ("~>()");
  mutt_buffer_printf (entrybuf, "~>(%s)", patternstr);
  entries[i].expr = safe_strdup (mutt_b2s (entrybuf));
  /* L10N:
     Pattern Completion Menu description for ~>()
  */
  entries[i].descr = safe_strdup _("messages having an immediate child matching PATTERN");

  mutt_push_current_menu (menu);

  mutt_buffer_pool_release (&entrybuf);

  return menu;
}

static void free_pattern_menu (MUTTMENU **pmenu)
{
  MUTTMENU *menu;
  PATTERN_ENTRY *entries;

  if (!pmenu || !*pmenu)
    return;

  menu = *pmenu;
  mutt_pop_current_menu (menu);

  entries = (PATTERN_ENTRY *) menu->data;
  while (menu->max)
  {
    menu->max--;
    FREE (&entries[menu->max].tag);
    FREE (&entries[menu->max].expr);
    FREE (&entries[menu->max].descr);
  }
  FREE (&menu->data);

  FREE (&menu->help);
  mutt_menuDestroy (pmenu);
}

int mutt_ask_pattern (char *buf, size_t buflen)
{
  MUTTMENU *menu;
  int rv = 0, done = 0;
  PATTERN_ENTRY *entry;

  menu = create_pattern_menu ();

  while (!done)
  {
    switch (mutt_menuLoop (menu))
    {
      case OP_GENERIC_SELECT_ENTRY:
        entry = (PATTERN_ENTRY *) menu->data + menu->current;
        strfcpy (buf, entry->tag, buflen);
        rv = 1;
        done = 1;
        break;

      case OP_EXIT:
        done = 1;
        break;
    }
  }

  free_pattern_menu (&menu);
  return (rv);
}
