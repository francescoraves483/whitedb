/*
* $Id:  $
* $Version: $
*
* Copyright (c) Tanel Tammet 2004,2005,2006,2007,2008,2009
*
* Contact: tanel.tammet@gmail.com                 
*
* Command parser written by Priit J�rv, some commands written
* by Enar Reilent.
*
* This file is part of wgandalf
*
* Wgandalf is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* 
* Wgandalf is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with Wgandalf.  If not, see <http://www.gnu.org/licenses/>.
*
*/

 /** @file wgdb.c
 *  wgandalf database tool: command line utility
 */

/* ====== Includes =============== */



#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <conio.h> // for _getch
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#include "../config-w32.h"
#else
#include "../config.h"
#endif
#include "../Db/dbmem.h"
#include "../Db/dballoc.h"
#include "../Db/dbdata.h"
#include "../Db/dbtest.h"
#include "../Db/dbdump.h"
#include "../Db/dblog.h"
#include "../Db/dbquery.h"
#include "../Db/dbutil.h"
#include "../Db/dblock.h"
#ifdef USE_REASONER
#include "../Parser/dbparse.h"
#endif  
#include "wgdb.h"


/* ====== Private defs =========== */

#ifdef _WIN32
#define sscanf sscanf_s  /* XXX: This will break for string parameters */
#endif

#define TESTREC_SIZE 3


/* Helper macros for database lock management */

#define RLOCK(d,i) i = wg_start_read(d); \
    if(!i) { \
        fprintf(stderr, "Failed to get database lock\n"); \
        break; \
    }

#define WLOCK(d,i) i = wg_start_write(d); \
    if(!i) { \
        fprintf(stderr, "Failed to get database lock\n"); \
        break; \
    }

#define RULOCK(d,i) if(i) { \
        wg_end_read(d,i); \
        i = 0; \
    }
#define WULOCK(d,i)  if(i) { \
        wg_end_write(d,i); \
        i = 0; \
    }

/* ======= Private protos ================ */

void query(void *db, char **argv, int argc);
void selectdata(void *db, int howmany, int startingat);
int add_row(void *db, char **argv, int argc);


/* ====== Functions ============== */


/*
how to set 500 meg of shared memory:

su
echo 500000000  > /proc/sys/kernel/shmmax 
*/

/** usage: display command line help.
*
*/

void usage(char *prog) {
  printf("usage: %s [shmname] <command> [command arguments]\n"\
    "Where:\n"\
    "  shmname - shared memory name for database. May be omitted.\n"\
    "  command - required, one of:\n\n"\
    "    help (or \"-h\") - display this text.\n"\
    "    version (or \"-v\") - display libwgdb version.\n"\
    "    free - free shared memory.\n"\
    "    export <filename> - write memory dump to disk.\n"\
    "    import <filename> - read memory dump from disk. Overwrites existing "\
    "memory contents.\n"\
    "    exportcsv <filename> - export data to a CSV file.\n"\
    "    importcsv <filename> - import data from a CSV file.\n", prog);
#ifdef USE_REASONER  
    printf("    importotter <filename> - import facts/rules from otter syntax file.\n"\
    "    importprolog <filename> - import facts/rules from prolog syntax file.\n"\
    "    runreasoner - run the reasoner on facts/rules in the database.\n");
#endif  
#ifdef HAVE_RAPTOR
  printf("    exportrdf <col> <filename> - export data to a RDF/XML file.\n"\
    "    importrdf <pref> <suff> <filename> - import data from a RDF file.\n");
#endif
  printf("    test - run quick database tests.\n"\
    "    fulltest - run in-depth database tests.\n"\
    "    header - print header data.\n"\
    "    fill <nr of rows> [asc | desc | mix] - fill db with integer data.\n"\
    "    add <value1> .. - store data row (only int or str recognized)\n"\
    "    del <column> <key> - delete data row\n"\
    "    select <number of rows> [start from] - print db contents.\n"\
    "    query <col> \"<cond>\" <value> .. - basic query.\n");
#ifdef _WIN32
  printf("    server [size b] - provide persistent shared memory for "\
    "other processes. Will allocate requested amount of memory and sleep; "\
    "Ctrl+C aborts and releases the memory.\n");
#else
  printf("    create [size b] - create empty db of given size.\n");
#endif
  printf("\nCommands may have variable number of arguments. Command names "\
    "may not be used as shared memory name for the database. "\
    "Commands that take values as arguments have limited support "\
    "for parsing various data types (see manual for details).\n");
}

/** top level for the database command line tool
*
*
*/

int main(int argc, char **argv) {
 
  char *shmname = NULL;
  void *shmptr = NULL;
  int i, scan_to, shmsize;
  wg_int rlock = 0;
  wg_int wlock = 0;
  
  /* look for commands in argv[1] or argv[2] */
  if(argc < 3) scan_to = argc;
  else scan_to = 3;
  shmsize = 0; /* 0 size causes default size to be used */
 
  /* 1st loop through, shmname is NULL for default. If
   * the first argument is not a recognizable command, it
   * is assumed to be the shmname and the next argument
   * is checked against known commands.
   */
  for(i=1; i<scan_to;) {
    if (!strcmp(argv[i],"help") || !strcmp(argv[i],"-h")) {
      usage(argv[0]);
      exit(0);
    }
    if (!strcmp(argv[i],"version") || !strcmp(argv[i],"-v")) {
      wg_print_code_version();
      exit(0);
    }
    if (!strcmp(argv[i],"free")) {
      /* free shared memory */
      wg_delete_database(shmname);
      exit(0);
    }
    if(argc>(i+1) && !strcmp(argv[i],"import")){
      wg_int err, minsize, maxsize;
      err = wg_check_dump(NULL, argv[i+1], &minsize, &maxsize);
      if(err) {
        fprintf(stderr, "Import failed.\n");
        break;
      }
      
      shmptr=wg_attach_memsegment(shmname, minsize, maxsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }

      /* Locking is handled internally by the dbdump.c functions */
      err = wg_import_dump(shmptr,argv[i+1]);
      if(!err)
        printf("Database imported.\n");
      else if(err<-1)
        fprintf(stderr, "Fatal error in wg_import_dump, db may have"\
          " become corrupt\n");
      else
        fprintf(stderr, "Import failed.\n");
      break;
    }
    else if(argc>(i+1) && !strcmp(argv[i],"export")){
      wg_int err;

      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }

      /* Locking is handled internally by the dbdump.c functions */
      err = wg_dump(shmptr,argv[i+1]);
      if(err<-1)
        fprintf(stderr, "Fatal error in wg_dump, db may have"\
          " become corrupt\n");
      else if(err)
        fprintf(stderr, "Export failed.\n");
      break;
    }
#if 0
    /* XXX: these functions are broken */
    else if(argc>(i+1) && !strcmp(argv[i],"log")) {
      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }
      wg_print_log(shmptr);
      wg_dump_log(shmptr,argv[i+1]);
      break;
    }
    else if(argc>(i+1) && !strcmp(argv[i],"importlog")) {    
      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }
      wg_import_log(shmptr,argv[i+1]);
      break;
    }
#endif
    else if(argc>(i+1) && !strcmp(argv[i],"exportcsv")){
      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }

      RLOCK(shmptr, wlock);
      wg_export_db_csv(shmptr,argv[i+1]);
      RULOCK(shmptr, wlock);
      break;
    }
    else if(argc>(i+1) && !strcmp(argv[i],"importcsv")){
      wg_int err;
      
      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }

      WLOCK(shmptr, wlock);
      err = wg_import_db_csv(shmptr,argv[i+1]);
      WULOCK(shmptr, wlock);
      if(!err)
        printf("Data imported from file.\n");
      else if(err<-1)
        fprintf(stderr, "Fatal error when importing, data may be partially"\
          " imported\n");
      else
        fprintf(stderr, "Import failed.\n");
      break;
    }
    
#ifdef USE_REASONER    
    else if(argc>(i+1) && !strcmp(argv[i],"importprolog")){
      fprintf(stderr, "Not implemented.\n");
      break;
    }
    else if(argc>(i+1) && !strcmp(argv[i],"importotter")){
      fprintf(stderr, "Not implemented.\n");
      break;
    }
    else if(argc>i && !strcmp(argv[i],"runreasoner")){
#if 0
      /* XXX: depends on code not commited or completed yet */
      wg_int err;
      
      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }
      //printf("about to call wg_run_reasoner\n");
      err = wg_run_reasoner(shmptr,argc,argv);
      if(!err);
        //printf("wg_run_reasoner finished ok.\n");     
      else
        fprintf(stderr, "wg_run_reasoner finished with an error %d.\n",err);
#else
      fprintf(stderr, "Not implemented.\n");
#endif
      break;
    }
#endif 
    
#ifdef HAVE_RAPTOR
    else if(argc>(i+2) && !strcmp(argv[i],"exportrdf")){
      wg_int err;
      int pref_fields = atol(argv[i+1]);

      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }

      printf("Exporting with %d prefix fields.\n", pref_fields);
      RLOCK(shmptr, wlock);
      err = wg_export_raptor_rdfxml_file(shmptr, pref_fields, argv[i+2]);
      RULOCK(shmptr, wlock);
      if(err)
        fprintf(stderr, "Export failed.\n");
      break;
    }
    else if(argc>(i+3) && !strcmp(argv[i],"importrdf")){
      wg_int err;
      int pref_fields = atol(argv[i+1]);
      int suff_fields = atol(argv[i+2]);
      
      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }

      printf("Importing with %d prefix fields, %d suffix fields.\n,",
        pref_fields, suff_fields);
      WLOCK(shmptr, wlock);
      err = wg_import_raptor_file(shmptr, pref_fields, suff_fields,
        wg_rdfparse_default_callback, argv[i+3]);
      WULOCK(shmptr, wlock);
      if(!err)
        printf("Data imported from file.\n");
      else if(err<-1)
        fprintf(stderr, "Fatal error when importing, data may be partially"\
          " imported\n");
      else
        fprintf(stderr, "Import failed.\n");
      break;
    }
#endif
    else if(!strcmp(argv[i],"test")) {
      /* This test function does it's own memory allocation. */
      wg_run_tests(WG_TEST_QUICK, 2);
      break;
    }
    else if(!strcmp(argv[i],"fulltest")) {
      wg_run_tests(WG_TEST_FULL, 2);
      break;
    }
    else if(!strcmp(argv[i], "header")) {
      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }
      RLOCK(shmptr, wlock);
      wg_show_db_memsegment_header(shmptr);
      RULOCK(shmptr, wlock);
      break;
    }
#ifdef _WIN32
    else if(!strcmp(argv[i],"server")) {
      if(argc>(i+1)) {
        shmsize = atol(argv[i+1]);
        if(!shmsize)
          fprintf(stderr, "Failed to parse memory size, using default.\n");
      }
      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }
      printf("Press Ctrl-C to end and release the memory.\n");
      while(_getch() != 3);
      break;
    }
#else
    else if(!strcmp(argv[i],"create")) {
      if(argc>(i+1)) {
        shmsize = atol(argv[i+1]);
        if(!shmsize)
          fprintf(stderr, "Failed to parse memory size, using default.\n");
      }
      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }
      break;
    }
#endif
    else if(argc>(i+1) && !strcmp(argv[i], "fill")) {
      int rows = atol(argv[i+1]);
      if(!rows) {
        fprintf(stderr, "Invalid number of rows.\n");
        exit(1);
      }

      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }

      WLOCK(shmptr, wlock);
      if(argc > (i+2) && !strcmp(argv[i+2], "mix"))
        wg_genintdata_mix(shmptr, rows, TESTREC_SIZE);
      else if(argc > (i+2) && !strcmp(argv[i+2], "desc"))
        wg_genintdata_desc(shmptr, rows, TESTREC_SIZE);
      else
        wg_genintdata_asc(shmptr, rows, TESTREC_SIZE);
      WULOCK(shmptr, wlock);
      printf("Data inserted\n");
      break;
    }
    else if(argc>(i+1) && !strcmp(argv[i],"select")) {
      int rows = atol(argv[i+1]);
      int from = 0;

      if(!rows) {
        fprintf(stderr, "Invalid number of rows.\n");
        exit(1);
      }
      if(argc > (i+2))
        from = atol(argv[i+2]);

      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }
      RLOCK(shmptr, wlock);
      selectdata(shmptr, rows, from);
      RULOCK(shmptr, wlock);
      break;
    }
    else if(argc>(i+1) && !strcmp(argv[i],"add")) {
      int err;
      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }
      WLOCK(shmptr, wlock);
      err = add_row(shmptr, argv+i+1, argc-i-1);
      WULOCK(shmptr, wlock);
      if(!err)
        printf("Row added.\n");
      break;
    }
    else if(argc>(i+2) && !strcmp(argv[i],"del")) {
      fprintf(stderr, "Not implemented.\n");
      break;
    }
    else if(argc>(i+3) && !strcmp(argv[i],"query")) {
      shmptr=wg_attach_database(shmname, shmsize);
      if(!shmptr) {
        fprintf(stderr, "Failed to attach to database.\n");
        exit(1);
      }
      /* Query handles it's own locking */
      query(shmptr, argv+i+1, argc-i-1);
      break;
    }
    
    shmname = argv[1]; /* assuming two loops max */
    i++;
  }

  if(i==scan_to) {
    /* loop completed normally ==> no commands found */
    usage(argv[0]);
  }
  if(shmptr) {
    RULOCK(shmptr, rlock);
    WULOCK(shmptr, wlock);
    wg_detach_database(shmptr);
  }
  exit(0);
}



/** Basic query functionality
 *  argv should point to the part in argument list where
 *  query parameters start.
 */
void query(void *db, char **argv, int argc) {
  int c, i, j, qargc;
  char cond[80];
  void *rec = NULL;
  wg_query *q;
  wg_query_arg *arglist;
  gint encoded;
  gint lock_id;

  qargc = argc / 3;
  arglist = (wg_query_arg *) malloc(qargc * sizeof(wg_query_arg));
  if(!arglist)
    return;

  for(i=0,j=0; i<qargc; i++) {
    arglist[i].value = WG_ILLEGAL;
  }

  for(i=0,j=0; i<qargc; i++) {
    int cnt = 0;
    cnt += sscanf(argv[j++], "%d", &c);
    cnt += sscanf(argv[j++], "%s", cond);
    encoded = wg_parse_and_encode_param(db, argv[j++]);

    if(cnt!=2 || encoded==WG_ILLEGAL) {
      fprintf(stderr, "failed to parse query parameters\n");
      goto abrt1;
    }

    arglist[i].column = c;
    arglist[i].value = encoded;
    if(!strncmp(cond, "=", 1))
        arglist[i].cond = WG_COND_EQUAL;
    else if(!strncmp(cond, "!=", 2))
        arglist[i].cond = WG_COND_NOT_EQUAL;
    else if(!strncmp(cond, "<=", 2))
        arglist[i].cond = WG_COND_LTEQUAL;
    else if(!strncmp(cond, ">=", 2))
        arglist[i].cond = WG_COND_GTEQUAL;
    else if(!strncmp(cond, "<", 1))
        arglist[i].cond = WG_COND_LESSTHAN;
    else if(!strncmp(cond, ">", 1))
        arglist[i].cond = WG_COND_GREATER;
    else {
      fprintf(stderr, "invalid condition %s\n", cond);
      free(arglist);
      return;
    }
  }

  if(!(lock_id = wg_start_read(db))) {
    fprintf(stderr, "failed to get lock on database\n");
    goto abrt1;
  }

  q = wg_make_query(db, NULL, 0, arglist, qargc);
  if(!q)
    goto abrt2;

/*  printf("query col: %d type: %d\n", q->column, q->qtype); */
  rec = wg_fetch(db, q);
  while(rec) {
    wg_print_record(db, (gint *) rec);
    printf("\n");
    rec = wg_fetch(db, q);
  }

  wg_free_query(db, q);
abrt2:
  wg_end_read(db, lock_id);
abrt1:
  for(i=0,j=0; i<qargc; i++) {
    if(arglist[i].value != WG_ILLEGAL) {
      wg_free_query_param(db, arglist[i].value);
    }
  }
  free(arglist);
}

/** Print rows from database
 *
 */
void selectdata(void *db, int howmany, int startingat) {

  void *rec = wg_get_first_record(db);
  int i, count;

  for(i=0;i<startingat;i++){
    if(rec == NULL) return;
    rec=wg_get_next_record(db,rec); 
  }

  count=0;
  while(rec != NULL) {
    wg_print_record(db, (gint *) rec);
    printf("\n");
    count++;
    if(count == howmany) break;
    rec=wg_get_next_record(db,rec);
  }

  return;
}

/** Add one row of data in database.
 *
 */
int add_row(void *db, char **argv, int argc) {
  int i;
  void *rec;
  gint encoded;
  
  rec = wg_create_record(db, argc);
  if (rec==NULL) { 
    fprintf(stderr, "Record creation error\n");
    return -1;
  }
  for(i=0; i<argc; i++) {
    encoded = wg_parse_and_encode(db, argv[i]);
    if(encoded == WG_ILLEGAL) {
      fprintf(stderr, "Parsing or encoding error\n");
      return -1;
    }
    wg_set_field(db, rec, i, encoded);
  }

  return 0;
}

#ifdef __cplusplus
}
#endif
