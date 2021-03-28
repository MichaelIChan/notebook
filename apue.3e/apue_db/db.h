#ifndef _DB_H_
#define _DB_H_

#include "apue.h"
#include "apue_db.h"

#include <fcntl.h>      // for open & db_open flags
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>    // for struct iovec

/*
 * Internal index file constants.
 * These are used to construct records in the
 * index file and data file.
 */
#define IDXLEN_SZ 4     // index record length (ASCII chars)
#define SEP       ':'   // separator char in index record
#define SPACE     ' '   // space character
#define NEWLINE   '\n'  // newline character

/*
 * The following definitions are for hash chains and free
 * list chain in the index file.
 */
#define PTR_SZ    7         // size of ptr field in hash chain
#define PTR_MAX   999999    // max file offset = 10 * PTR_SZ - 1
#define NHASH_DEF 137       // default hash table size
#define FREE_OFF  0         // free list offset in index file
#define HASH_OFF  PTR_SZ    // hash table offset in index file

typedef unsigned long DBHASH;   // hash values
typedef unsigned long COUNT;    // unsigned counter

/*
 * Library's private representation of the database.
 */
typedef struct {
    int    idxfd;           // fd for index file
    int    datfd;           // fd for data file
    char   *idxbuf;         // malloc'ed buffer for index record
    char   *datbuf;         // malloc'ed buffer for data record
    char   *name;           // name db was opened under
    off_t  idxoff;          // offset in index file of index record
                            // key is at (idxoff + PTR_SZ + IDXLEN_SZ)
    size_t idxlen;          // length of index record
                            // excludes IDXLEN_SZ bytes at front of record
                            // includes newline at end of index record
    off_t  datoff;          // offset in data file of data record
    size_t datlen;          // length of data record
                            // includes newline at end
    off_t  ptrval;          // contents of chain ptr in index record
    off_t  ptroff;          // chain ptr offset pointing to this idx record
    off_t  chainoff;        // offset of hash chain for this index record
    off_t  hashoff;         // offset in index file of hash table
    DBHASH nhash;           // current hash table size
    COUNT  cnt_delok;       // delete OK
    COUNT  cnt_delerr;      // delete error
    COUNT  cnt_fetchok;     // fetch OK
    COUNT  cnt_fetcherr;    // fetch error
    COUNT  cnt_nextrec;     // nextrec
    COUNT  cnt_stor1;       // store: DB_INSERT, no empty, appended
    COUNT  cnt_stor2;       // store: DB_INSERT, found empty, reused
    COUNT  cnt_stor3;       // store: DB_REPLACE, diff len, appended
    COUNT  cnt_stor4;       // store: DB_REPLACE, same len, overwrote
    COUNT  cnt_storerr;     // store error
} DB;

/*
 * Internal functions
 */

/*
 * Allocate & initialize a DB structure and its buffers.
 */
static DB *_db_alloc(int);

/*
 * Delete the current record specified by the DB structure.
 * This function is called by db_delete and db_store, after
 * the record has been located by _db_find_and_lock.
 */
static void _db_dodelete(DB *);

/*
 * Find the specified record. Called by db_delete, db_fetch,
 * and db_store. Returns with the hash chain locked.
 */
static int _db_find_and_lock(DB *, const char *, int);

/*
 * Try to find a free index record and accompanying data record
 * of the correct sizes. We're only called by db_store.
 */
static int _db_findfree(DB *, int, int);

/*
 * Free up a DB structure, and all the malloc'ed buffers it
 * may point to. Also close the file descriptors if still open.
 */
static void _db_free(DB *);

/*
 * Calculate the hash value for a key.
 */
static DBHASH _db_hash(DB *, const char *);

/*
 * Read the current data record into the data buffer.
 * Returns a pointer to the null-terminated data buffer.
 */
static char *_db_readdat(DB *);

/*
 * Read the next index record. We start at the specified offset
 * int the index file. We read the index record into db->idxbuf
 * and replace the separators with null bytes, If all is OK we
 * set db->datoff and db->datlen to the offset and length of the
 * corresponding data record in the data file.
 */
static off_t _db_readidx(DB *, off_t);

/*
 * Read a chain ptr field from anywhere in the index file:
 * the free list pointer, a hash table chain ptr, or an
 * index record chain ptr.
 */
static off_t _db_readptr(DB *, off_t);

/*
 * Write a data record. Called by _db_dodelete (to write
 * the record with blanks) and db_store.
 */
static void _db_writedat(DB *, const char *, off_t, int);

/*
 * Write an index record. _db_writedat is called before
 * this function to set the datoff and datlen fields in the
 * DB structure, which we need to write the index record.
 */
static void _db_writeidx(DB *, const char *, off_t, int, off_t);

/*
 * Write a chain ptr field somewhere in the index file:
 * the free list, the hash table, or in an index record.
 */
static void _db_writeptr(DB *, off_t, off_t);

#endif /* _DB_H_ */