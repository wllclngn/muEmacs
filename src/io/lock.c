/*	LOCK.C
 *
 *	File locking command routines
 *
 *	written by Daniel Lawrence
 */

#include <stdio.h>
#include <stdbool.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "memory.h"
#include "string_utils.h"

#include <errno.h>

static char *lname[NLOCKS];		/* names of all locked files */
static int numlocks;			/* # of current locks active */

/*
 * lockchk:
 *	check a file for locking and add it to the list
 *
 * char *fname;			file to check for a lock
 */
int lockchk(char *fname)
{
    int i;		/* loop indexes */
    int status;	/* return status */

    /* check to see if that file is already locked here */
    if (numlocks > 0)
        for (i = 0; i < numlocks; ++i)
            if (strcmp(fname, lname[i]) == 0)
                return true;

    /* if we have a full locking table, bitch and leave */
    if (numlocks == NLOCKS) {
        mlwrite("LOCK ERROR: LOCK TABLE FULL");
        return ABORT;
    }

    /* next, try to lock it */
    status = lock(fname);
    if (status == ABORT)	/* file is locked, no override */
        return ABORT;
    if (status == false)	/* locked, overriden, dont add to table */
        return true;

    /* we have now locked it, add it to our table */
    lname[++numlocks - 1] = (char*)safe_alloc(strlen(fname) + 1, "lock filename", __FILE__, __LINE__);
    if (lname[numlocks - 1] == nullptr) {	/* malloc failure */
        undolock(fname);	/* free the lock */
        mlwrite("CANNOT LOCK, OUT OF MEMORY");
        --numlocks;
        return ABORT;
    }

    /* everthing is cool, add it to the table */
    safe_strcpy(lname[numlocks - 1], fname, NSTRING);
    return true;
}

/*
 * lockrel:
 *	release all the file locks so others may edit
 */
int lockrel(void)
{
    int i;		/* loop index */
    int status;	/* status of locks */
    int s;		/* status of one unlock */

    status = true;
    if (numlocks > 0)
        for (i = 0; i < numlocks; ++i) {
            if ((s = unlock(lname[i])) != true)
                status = s;
            SAFE_FREE(lname[i]);
        }
    numlocks = 0;
    return status;
}

/*
 * lock:
 *	Check and lock a file from access by others
 *	returns	true = files was not locked and now is
 *		false = file was locked and overridden
 *		ABORT = file was locked, abort command
 *
 * char *fname;		file name to lock
 */
int lock(char *fname)
{
    char *locker;	/* lock error message */
    int status;	/* return status */
    char msg[NSTRING];	/* message string */

    /* attempt to lock the file */
    locker = dolock(fname);
    if (locker == nullptr)	/* we win */
        return true;

    /* file failed...abort */
    if (strncmp(locker, "LOCK", 4) == 0) {
        lckerror(locker);
        return ABORT;
    }

    /* someone else has it....override? */
    safe_strcpy(msg, "File in use by ", NSTRING);
    safe_strcat(msg, locker, NSTRING);
    safe_strcat(msg, ", override?", NSTRING);
    status = mlyesno(msg);	/* ask them */
    if (status == true)
        return false;
    else
        return ABORT;
}

/*
 * unlock:
 *	Unlock a file
 *	this only warns the user if it fails
 *
 * char *fname;		file to unlock
 */
int unlock(char *fname)
{
    char *locker;	/* undolock return string */

    /* unclock and return */
    locker = undolock(fname);
    if (locker == nullptr)
        return true;

    /* report the error and come back */
    lckerror(locker);
    return false;
}

/*
 * report a lock error
 *
 * char *errstr;	lock error string to print out
 */
void lckerror(char *errstr)
{
    char obuf[NSTRING];	/* output buffer for error message */

    safe_strcpy(obuf, errstr, NSTRING);
    safe_strcat(obuf, " - ", NSTRING);
    safe_strcat(obuf, strerror(errno), NSTRING);
    mlwrite(obuf);
}
