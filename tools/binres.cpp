/* binres.cpp: extract Palm OS resources from a bfd executable.

   Copyright (c) 1998, 1999 by John Marshall.
   <jmarshall@acm.org>

   This is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program follows in the footsteps of obj-res and build-prc, the
   source code of which contains the following notices:

 * obj-res.c:  Dump out .prc compatible binary resource files from an object
 *
 * (c) 1996, 1997 Dionne & Associates
 * jeff@ryeham.ee.ryerson.ca
 *
 * This is Free Software, under the GNU Public Licence v2 or greater.
 *
 * Relocation added March 1997, Kresten Krab Thorup
 * krab@california.daimi.aau.dk

 * ptst.c:  build a .prc from a pile of files.
 *
 * (c) 1996, Dionne & Associates
 * (c) 1997, The Silver Hammer Group Ltd.
 * This is Free Software, under the GNU Public Licence v2 or greater.
 */

#include "binres.hpp"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "libiberty.h"
#include "bfd.h"

#include "pfd.hpp"
#include "pfdio.hpp"
#include "utils.h"

#if 0
#include "libcoff.h"
#else
/* It doesn't seem worthwhile to play enough autoconf games to make the
   above include file accessible.  The following is all we really need.  */

/* libcoff.h would give us access to howtos and bfd_get_reloc_size, but
   the following should be a good enough approximation.  It's exactly right
   for the only reloc type ever likely to be in a .reloc section.  */
#define RELOC_SIZE 4
#endif


/* A normal code resource: this is just a copy of the corresponding BFD
   section.  */

static Datablock
make_code (bfd* abfd, asection* sec) {
  bfd_size_type size = bfd_section_size (abfd, sec);

  Datablock res (size);

  if (!bfd_get_section_contents (abfd, sec, res.writable_contents (), 0, size))
    einfo (E_FILE, "can't read `%s' section", bfd_section_name (abfd, sec));
  
  return res;
  }


/* A code resource containing an entry point.  If the entry point is not at
   the start of the block, we insert a jump to it.  */

static Datablock
make_main_code (bfd* abfd, asection* sec, bool /*appl*/) {
  Datablock res = make_code (abfd, sec);

  unsigned int entry =
      (bfd_get_start_address (abfd) - bfd_section_vma (abfd, sec));

  if (entry > 32766) {	// ???
    ewhere ("0x%x", entry);
    einfo (E_FILEWHERE, "entry point too distant");
    }
  else if (entry > 126) {
    res = res (-4, res.size () + 4);
    unsigned char* s = res.writable_contents ();
    put_word (s, 0x6000);	// bra.w OFF
    put_word (s, 2 + entry);	// OFF = sizeof(insn)-2 + entry
    }
  else if (entry > 0) {
    res = res (-2, res.size () + 2);
    unsigned char* s = res.writable_contents ();
    put_byte (s, 0x60);		// bra.s OFF
    put_byte (s, entry);	// OFF = sizeof(insn)-2 + entry
    }

#if 0
  /* We used to put this useless instruction at the start of the first
     code resource because CodeWarrior puts one there.  This always seemed
     pointless, and we now have a plausible explanation why it's pointless,
     so it's gone.  Bob Petersen (bpetersen@handspring.com) says in the old
     days on the Mac people would put essentially a NOP there so that the
     code resource could be patched easily.  */

  if (appl) {
    res = res (-4, res.size () + 4);
    unsigned char* s = res.writable_contents ();
    put_word (s, 0x0000);	// ori.b #IMM,%d0
    put_word (s, 0x0001);	// IMM = 1
    }
#endif

  return res;
  }


/* The code #0 resource: as Jeff said "Truth be known, I think it's
   mostly bogus".  Ted Ts'o has a likely-looking theory that it's an
   unadulterated Macintosh code #0 jump table.  I'm not sure why we
   want one of these in Palm OS land :-).  The `(?)'s mark what these
   fields mean if it is indeed a Macintosh resource.

   The early Macintoshes used this jump table to handle multiple code
   resources.  Fortunately, we don't have to support demand loading of
   resources, so our multiple code resource handling is much simpler,
   and doesn't need this jump table.  */

static Datablock
make_code0 (size_t data_size) {
  Datablock res (24);
  unsigned char* s = res.writable_contents ();

  put_long (s, 0x00000028);	// data size above %a5 (?)
  put_long (s, data_size);	// total data size
  put_long (s, 8);		// size of jump table (?)
  put_long (s, 0x00000020);	// jump table's offset from %a5 (?)

  // The one and only jump table entry: (?)

  put_word (s, 0x0000);		// offset (?)
  put_word (s, 0x3f3c);		// move.w #IMM,-(%sp) (?)
  put_word (s, 0x0001);		// IMM = 1 (?)
  put_word (s, 0xa9f0);		// Macintosh SegLoad trap (?!)

  return res;
  }


/* The pref resource contains a SysAppPrefsType, as described in
   System/SystemPrv.h.  However, probably most of these numbers are
   ignored anyway.  */

static Datablock
make_pref (unsigned long stack) {
  Datablock res (10);
  unsigned char* s = res.writable_contents ();

  put_word (s, 30);		// AMX task priority
  put_long (s, stack);		// stack size
  put_long (s, 4096);		// minimum free space in heap

  return res;
  }


/* The (new-style) rloc resource, which contains the head of a reloc chain
   for each resource (data#0, code#1, code#2, ...).  As a by-product,
   updates the (raw) DATA with the links of the reloc chains.  */

/* There is an array of these, indexed by section index.  */
struct resource_info {
  long chain;	/* Which chain to add relocs to (0 = data, -1 = unknown).  */
  long offset;	/* This section's offset within the resource it lies in.  */
  };

static Datablock
make_rloc_and_chains (int nchains, const resource_info* res_from_sec,
		      bfd* abfd, asection* reloc_sec, bfd_size_type reloc_size,
		      bfd_byte* data, bfd_size_type data_size) {
  Datablock res (2 * nchains);
  unsigned char* rloc_res = res.writable_contents ();

  unsigned char* s = rloc_res;
  for (int i = 0; i < nchains; i++)
    put_word (s, 0xffff);

  bfd_byte* reloc = NULL;
  if (reloc_size > 0) {
    reloc = static_cast<bfd_byte*>(xmalloc (reloc_size));
    if (!bfd_get_section_contents (abfd, reloc_sec, reloc, 0, reloc_size)) {
      einfo (E_FILE, "can't read `.reloc' section");
      reloc_size = 0;  // Short circuit the for loop
      }
    }

  for (bfd_byte* rel = reloc; rel < reloc + reloc_size; rel += 12) {
    unsigned int type;
    unsigned long reloffset;
    int relsecndx, symsecndx;
    asection *sec, *relsec, *symsec;
    CONST char *relsecname, *symsecname;
    char relbuffer[32], symbuffer[32];

    type      = bfd_get_16 (abfd, rel);
    relsecndx = bfd_get_16 (abfd, rel+2);
    reloffset = bfd_get_32 (abfd, rel+4);
    symsecndx = bfd_get_16 (abfd, rel+8);

    relsec = symsec = NULL;
    for (sec = abfd->sections; sec; sec = sec->next) {
      if (sec->index == relsecndx)  relsec = sec;
      if (sec->index == symsecndx)  symsec = sec;
      }

    sprintf (relbuffer, "[%d?]", (int) relsecndx);
    relsecname = (relsec)? bfd_section_name (abfd, relsec) : relbuffer;

    sprintf (symbuffer, "[%d?]", (int) symsecndx);
    symsecname = (symsec)? bfd_section_name (abfd, symsec) : symbuffer;

    ewhere ("%s+0x%04lx", relsecname, reloffset);

    if (!relsec || res_from_sec[relsecndx].chain != 0) {
      einfo (E_FILEWHERE | E_WARNING,
	     "reloc in non-data section `%s'", relsecname);
      continue;
      }

    if (reloffset > data_size - RELOC_SIZE) {
      einfo (E_FILEWHERE | E_WARNING, "reloc location out of range");
      continue;
      }

    if (!symsec || res_from_sec[symsecndx].chain == -1) {
      einfo (E_FILEWHERE | E_WARNING,
	     "reloc relative to strange section `%s'", symsecname);
      continue;
      }

    switch (type) {
    case 1: {  /* Absolute 32bit reference */
      unsigned long value =
	  (bfd_get_32 (abfd, data + reloffset) - bfd_section_vma (abfd, symsec)
	   + res_from_sec[symsecndx].offset);
      unsigned char* reshead = rloc_res + 2 * res_from_sec[symsecndx].chain;
      const unsigned char* cs = reshead;
      unsigned int prevoffset = get_word (cs);
      bfd_put_16 (abfd, prevoffset, data+reloffset);
      bfd_put_16 (abfd, value, data+reloffset+2);
      put_word (reshead, reloffset);
      }
      break;

    default:
      einfo (E_FILEWHERE | E_WARNING, "unknown reloc type 0x%x", type);
      continue;
      }
    }

  free (reloc);
  return res;
  }


/* Emit the whole block as a series of literal runs.  */
static unsigned char*
emit_literals (unsigned char* out,
	       const unsigned char* p, const unsigned char* lim) {
  while (p < lim) {
    int len = lim - p;
    if (len > 128)  len = 128;
    *out++ = 0x7f + len;
    memcpy (out, p, len);
    out += len;
    p += len;
    }

  return out;
  }


/* This greedy algorithm isn't always optimal (for example, consider 65 zeros
   followed by 128 random bytes -- and therefore there may be cases where it
   actually makes a significant difference).  But hopefully it's good
   enough.  */
static unsigned char*
compress_runs (unsigned char* out,
	       const unsigned char* in, const unsigned char* inlim) {
  while (in < inlim) {
    const unsigned char* copy_in = in;

    /* Scan until the data runs out or we find a good run.  */
    while (! (in >= inlim
	      || (in+1 < inlim && (*in == 0 || *in == 0xff) && *in == in[1])
	      || (in+2 < inlim && *in == in[1] && *in == in[2])))
      in++;

    out = emit_literals (out, copy_in, in);

    if (in < inlim) {
      int len, maxlen;

      for (len = 0; in + len < inlim && in[len] == *in; len++)
	;

      /* Only emit one run at a time, since the residue beyond maxlen may not
         be long enough to form a worthwhile run on its own.  */
      maxlen = (*in == 0)? 64 : (*in == 0xff)? 16 : 33;
      if (len > maxlen)  len = maxlen;

      if (*in == 0)  *out++ = 0x3f + len;
      else if (*in == 0xff)  *out++ = 0x0f + len;
      else  *out++ = 0x1e + len, *out++ = *in;

      in += len;
      }
    }

  return out;
  }


static void*
mem_A9F000xxxxxx00xx (const void* buf, size_t buflen) {
  const char* s = static_cast<const char*>(buf);
  const char* slim = s + buflen;
  for (;
       (s = (const char*) memmem (s, slim-s - 5, "\xA9\xF0\x00", 3)) != NULL;
       s += 3)
    if (s[6] == 0x00)
      return const_cast<char*>(s);

  return NULL;
  }

static unsigned char*
compress_patterns (unsigned char* out,
		   const unsigned char* in, const unsigned char* inlim) {
  const unsigned char* s;

  if ((s = (const unsigned char*)
		mem_A9F000xxxxxx00xx (in, inlim-in)) != NULL) {
    out = compress_patterns (out, in, s);
    if (s[3] == 0x00)
      *out++ = 0x03, *out++ = s[4], *out++ = s[5], *out++ = s[7];
    else
      *out++ = 0x04, *out++ = s[3], *out++ = s[4], *out++ = s[5], *out++ = s[7];
    out = compress_patterns (out, s + 8, inlim);
    }
  else if ((s = (const unsigned char*)
		     memmem (in, inlim-in - 3,
			     "\x00\x00\x00\x00\xFF", 5)) != NULL) {
    out = compress_patterns (out, in, s);
    if (s[5] == 0xFF)
      *out++ = 0x01, *out++ = s[6], *out++ = s[7];
    else
      *out++ = 0x02, *out++ = s[5], *out++ = s[6], *out++ = s[7];
    out = compress_patterns (out, s + 8, inlim);
    }
  else
    out = compress_runs (out, in, inlim);

  return out;
  }


static unsigned char*
compress_data (unsigned char* datap, const unsigned char* raw,
	       const unsigned char* rawp, const unsigned char* rawlim,
	       unsigned long total_data_size, int compression) {
  put_long (datap, (rawp - raw) - total_data_size);

  switch (compression % 4) {
  case 0:
    datap = emit_literals (datap, rawp, rawlim);
    break;

  case 1:
    datap = compress_runs (datap, rawp, rawlim);
    break;

  default:
    datap = compress_patterns (datap, rawp, rawlim);
    break;
    }

  *datap++ = '\0';
  return datap;
  }


static void
find_maximal_zero_run (const unsigned char** zeropp,
		       const unsigned char** zerolimp,
		       const unsigned char* p, const unsigned char* lim) {
  *zeropp = *zerolimp = lim;

  while (p < lim && (p = (unsigned char*) memchr (p, 0, lim - p)) != NULL) {
    const unsigned char* zero = p;
    while (p < lim && *p == 0)  p++;
    if (p - zero > *zerolimp - *zeropp)
      *zeropp = zero, *zerolimp = p;
    }
  }


/* The data resource contains a compressed version of the raw data section
   followed by relocation tables in an unknown format.  */

static Datablock
make_data (const bfd_byte* raw_data, size_t data_size, size_t total_data_size,
	   int compr, binary_file_stats* stats) {
  Datablock res (128 + 33 * (data_size / 32));
  unsigned char* data = res.writable_contents();
  unsigned char* datap = data + 4;

  if (compr >= 4) {
    const unsigned char *Lp, *Llim, *Mp, *Mlim, *Rp, *Rlim;
    const unsigned char *Lzerop, *Lzerolim, *Rzerop, *Rzerolim;

    Lp = raw_data;
    Rlim = raw_data + data_size;

    /* Ignore leading and trailing runs of zeros.  */

    while (Lp < Rlim && *Lp == 0)  Lp++;
    while (Rlim > Lp && Rlim[-1] == 0)  Rlim--;

    /* Now find the two longest internal runs of zeros.  */

    find_maximal_zero_run (&Llim, &Rp, Lp, Rlim);
    find_maximal_zero_run (&Lzerop, &Lzerolim, Lp, Llim);
    find_maximal_zero_run (&Rzerop, &Rzerolim, Rp, Rlim);

    if (Lzerolim - Lzerop > Rzerolim - Rzerop)
      Mp = Lzerolim, Mlim = Llim, Llim = Lzerop;
    else
      Mp = Rp, Mlim = Rzerop, Rp = Rzerolim;

    datap = compress_data (datap, raw_data, Lp, Llim, total_data_size, compr);
    datap = compress_data (datap, raw_data, Mp, Mlim, total_data_size, compr);
    datap = compress_data (datap, raw_data, Rp, Rlim, total_data_size, compr);

    stats->omitted_zeros = (Lp - raw_data) + (Mp - Llim) + (Rp - Mlim)
			    + (raw_data + data_size - Rlim);
    }
  else {
    datap = compress_data (datap, raw_data, raw_data, raw_data + data_size,
			   total_data_size, compr);
    datap = compress_data (datap, raw_data, raw_data, raw_data,
			   total_data_size, compr);
    datap = compress_data (datap, raw_data, raw_data, raw_data,
			   total_data_size, compr);
    stats->omitted_zeros = 0;
    }

  /* 6 longs of 0 because we don't know the format of the standard Palm OS
     relocation tables.  They wouldn't be much use for our way of handling
     multiple code resources anyway.  */

  put_long (datap, 0);
  put_long (datap, 0);
  put_long (datap, 0);

  unsigned char* s = data;
  put_long (s, datap - data);  // Offset of the CODE 1 xrefs

  put_long (datap, 0);
  put_long (datap, 0);
  put_long (datap, 0);

  return res (0, datap - data);
  }


static bool bfd_inited = false;

ResourceDatabase
process_binary_file (const char* fname, const binary_file_info& info,
		     binary_file_stats* stats) {
  if (!bfd_inited) {
    bfd_init ();
    bfd_inited = true;
    }

  binary_file_stats ignored_stats;
  if (stats == NULL)  stats = &ignored_stats;

  ResourceDatabase db;

  bfd* abfd = bfd_openr (fname, NULL);
  filename = fname;

  if (!abfd || !bfd_check_format (abfd, bfd_object)) {
    einfo (E_NOFILE, "can't open `%s': %s",
	   fname, bfd_errmsg (bfd_get_error ()));
    return db;
    }

  asection* data_sec = bfd_get_section_by_name (abfd, ".data");
  asection* bss_sec  = bfd_get_section_by_name (abfd, ".bss");

  bfd_size_type data_size = bfd_section_size (abfd, data_sec);
  bfd_size_type bss_size  = bfd_section_size (abfd, bss_sec);

  /* Round up to the next longword boundary.  */
  size_t total_data_size = (data_size + bss_size + 3) & ~3;

  if (info.emit_appl_extras) {
    db[ResKey ("code", 0)] = make_code0 (total_data_size);
    db[ResKey ("pref", 0)] = make_pref (info.stack_size);
    }

  unsigned int nsections = bfd_count_sections (abfd);
  resource_info* res_from_sec = new resource_info[nsections];

  for (unsigned int i = 0; i < nsections; i++)
    res_from_sec[i].chain = -1;

  res_from_sec[data_sec->index].chain = 0;
  res_from_sec[data_sec->index].offset = 0;

  res_from_sec[bss_sec->index].chain = 0;
  res_from_sec[bss_sec->index].offset =
      (bfd_section_vma (abfd, bss_sec) - bfd_section_vma (abfd, data_sec));

  asection* text_sec = bfd_get_section_by_name (abfd, ".text");
  res_from_sec[text_sec->index].chain = 1;
  res_from_sec[text_sec->index].offset = 0;

  db[info.maincode] = make_main_code (abfd, text_sec, info.emit_appl_extras);

  for (map<const char*, ResKey>::const_iterator it = info.extracode.begin();
       it != info.extracode.end();
       ++it) {
    asection* sec = bfd_get_section_by_name (abfd, (*it).first);
    if (sec) {
      res_from_sec[sec->index].chain = (*it).second.id;
      res_from_sec[sec->index].offset = 0;
      db[(*it).second] = make_code (abfd, sec);
      }
    else
      einfo (E_FILE, "unknown code section `%s'", (*it).first);
    }

  for (asection* sec = abfd->sections; sec; sec = sec->next)
    if (bfd_get_section_flags (abfd, sec) & SEC_CODE)
      if (res_from_sec[sec->index].chain == -1) {
	ewhere ("%s", bfd_section_name (abfd, sec));
        einfo (E_FILEWHERE | E_WARNING, "spurious code section ignored");
	}

  if (info.emit_data) {
    bfd_byte* data = static_cast<bfd_byte*>(xmalloc (data_size));

    if (bfd_get_section_contents (abfd, data_sec, data, 0, data_size)) {
      asection* reloc_sec = bfd_get_section_by_name (abfd, ".reloc");
      bfd_size_type reloc_size = (reloc_sec)? bfd_section_size (abfd, reloc_sec)
					    : 0;

      if (reloc_size > 0 || info.force_rloc)
	db[ResKey ("rloc", 0)] =
	    make_rloc_and_chains (2 + info.extracode.size(), res_from_sec,
				  abfd, reloc_sec, reloc_size, data, data_size);

      db[ResKey ("data", 0)] = make_data (data, data_size, total_data_size,
					  info.data_compression, stats);
      }
    else
      einfo (E_FILE, "can't read `.data' section");

    free (data);
    }
  else if (total_data_size > 0)
    einfo (E_FILE | E_WARNING, "global data ignored");

  stats->data_size = data_size;

  delete [] res_from_sec;
  bfd_close (abfd);

  return db;
  }
