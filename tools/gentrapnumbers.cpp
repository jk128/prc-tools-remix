/* gentrapnumbers.cpp: generate listing of systrap numbers from an SDK.

   Copyright (c) 2001 Palm, Inc. or its subsidiaries.
   All rights reserved.

   This is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.  */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <string>
#include <map>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>

#include "libiberty.h"

#include "utils.h"

const char *progname = "gentrapnumbers";
const char progversion[] = "1.0";


void
dump (FILE *f, char **ap) {
  while (*ap)
    fprintf (f, " %s", *ap++);
  putc ('\n', f);
  }


int
run_preprocessor (char *fname_i, char *fname_c, int argc, char **argv) {
  char cmd[FILENAME_MAX];
  sprintf (cmd, "%s/%s/bin/gcc%s", EXEC_PREFIX, TARGET_ALIAS, EXEEXT);

  bool verbose_seen = false;

  char **args = (char **)(xmalloc ((6 + argc) * sizeof (const char *)));
  char **ap = args;

  *ap++ = "gcc";
  *ap++ = "-E";
  *ap++ = "-dM";
  *ap++ = "-o";
  *ap++ = fname_i;
  *ap++ = fname_c;
  for (int i = 1; i < argc; i++) {
    if (strcmp (argv[i], "-v") == 0)
      verbose_seen = true;
    *ap++ = argv[i];
    }
  *ap = NULL;

  if (verbose_seen) {
    fprintf (stderr, " [%s]", cmd);
    dump (stderr, args);
    }

  char *errmsg_fmt, *errmsg_arg;
  int pid = pexecute (cmd, args, progname,
		      NULL, &errmsg_fmt, &errmsg_arg, PEXECUTE_ONE);

  free (args);

  if (pid < 0) {
    einfo (E_NOFILE|E_PERROR, errmsg_fmt, errmsg_arg);
    return EXIT_FAILURE;
    }

  int status;
  pwait (pid, &status, 0);

  if (WIFSIGNALED (status))
    return EXIT_FAILURE;
  else if (WIFEXITED (status))
    return WEXITSTATUS (status);
  else
    return EXIT_SUCCESS;
  }


void
parse_definitions (FILE *fout, FILE *fin) {
  map<long, string> traps;

  char line[2048];
  int maxkeylen = 0;
  while (fgets (line, sizeof line, fin)) {
    char key[120];
    long value;
    if (sscanf (line, "#define sysTrap%s %li", key, &value) == 2
	&& strcmp (key, "Base") != 0 && strcmp (key, "LastTrapNumber") != 0) {
      traps[value] = key;
      if (maxkeylen < strlen (key))  maxkeylen = strlen (key);
      }
    }
  
  fprintf (fout,
    "Total number of traps present, and minimum and maximum trap vectors:\n");
  fprintf (fout, "* %ld 0x%lx 0x%lx\n\n",
	   long (traps.size ()),
	   (*(traps.begin ())).first, (*(traps.rbegin ())).first);

  for (map<long, string>::const_iterator it = traps.begin ();
       it != traps.end ();
       ++it)
    fprintf (fout, "- %-*s  0x%lx\n",
	     maxkeylen, (*it).second.c_str (), (*it).first);
  }


enum { NEW, I, C, BASE };

int
process (char fname[][FILENAME_MAX], FILE *f[], int argc, char **argv) {
  if ((f[NEW] = fopen (fname[NEW], "w")) == NULL) {
    einfo (E_NOFILE|E_PERROR, "can't create %s", fname[NEW]);
    return EXIT_FAILURE;
    }

  fprintf (f[NEW], "Generated by %s v%s", progname, progversion);
  if (argc > 1) {
    fprintf (f[NEW], " with flags");
    dump (f[NEW], argv+1);
    }
  else
    putc ('\n', f[NEW]);
  putc ('\n', f[NEW]);

  if ((f[C] = fopen (fname[C], "w")) == NULL) {
    einfo (E_NOFILE|E_PERROR, "can't create %s", fname[C]);
    return EXIT_FAILURE;
    }

  /* ErrorMgr.h is a header which is in all versions of the SDK, and which
     always pulls in the list of systraps.  */

  fprintf (f[C], "#include <ErrorMgr.h>\n");

  fclose (f[C]);
  f[C] = NULL;

  int rc = run_preprocessor (fname[I], fname[C], argc, argv);
  if (rc != EXIT_SUCCESS)
    return rc;

  if ((f[I] = fopen (fname[I], "r")) == NULL) {
    einfo (E_NOFILE|E_PERROR, "can't open %s", fname[I]);
    return EXIT_FAILURE;
    }

  parse_definitions (f[NEW], f[I]);

  fclose (f[I]);
  f[I] = NULL;

  fclose (f[NEW]);
  f[NEW] = NULL;

  return (copy_file (fname[BASE], fname[NEW], ""))? EXIT_SUCCESS : EXIT_FAILURE;
  }


int
main (int argc, char **argv) {
  char fname[4][FILENAME_MAX];
  FILE *f[3];
  int rc;

  xmalloc_set_program_name (progname);

  sprintf (fname[BASE], "%s/lib/trapnumbers", PALMDEV_PREFIX);
  sprintf (fname[NEW], "%s.new", fname[BASE]);
  sprintf (fname[I], "%s.i", fname[BASE]);
  sprintf (fname[C], "%s.c", fname[BASE]);

  f[NEW] = f[I] = f[C] = NULL;

  rc = process (fname, f, argc, argv);

  if (f[NEW])  fclose (f[NEW]);
  if (f[I])  fclose (f[I]);
  if (f[C])  fclose (f[C]);

  remove (fname[NEW]);
  remove (fname[I]);
  remove (fname[C]);

  return rc;
  }
