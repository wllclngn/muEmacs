/*	PKLOCK.C
 *
 *	locking routines as modified by Petri Kutvonen
 */

#include "estruct.h"
#include "edef.h"
#include "efunc.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include "string_utils.h"

#define MAXLOCK 512
#define MAXNAME 128




/* if successful, returns nullptr
 * if file locked, returns username of person locking the file
 * if other error, returns "LOCK ERROR: explanation" */
char *dolock(char *fname)
{
    int fd, n;
    static char lname[MAXLOCK], locker[MAXNAME + 1];
    int mask;
    struct stat sbuf;

    SAFE_STRCPY(lname, fname);
    SAFE_STRCAT(lname, ".lock~");

    /* Atomic create-or-fail with O_EXCL to prevent TOCTOU race */
    mask = (int)umask(0);
    fd = open(lname, O_RDWR | O_CREAT | O_EXCL, 0666);
    umask((mode_t)mask);
    
    if (fd < 0) {
        if (errno == EEXIST) {
            /* Lock already exists - read owner */
            fd = open(lname, O_RDONLY);
            if (fd < 0) {
                if (errno == EACCES)
                    return nullptr;
#ifdef EROFS
                if (errno == EROFS)
                    return nullptr;
#endif
                return "LOCK ERROR: cannot access lock file";
            }
            
            /* Verify it's a regular file (post-open, safe from symlink attacks) */
            if (fstat(fd, &sbuf) == 0) {
#if defined(S_ISREG)
                if (!S_ISREG(sbuf.st_mode)) {
#else
                if (!(((sbuf.st_mode) & 070000) == 0)) {
#endif
                    close(fd);
                    return "LOCK ERROR: lock file is not regular";
                }
            }
            
            n = (int)read(fd, locker, MAXNAME);
            close(fd);
            locker[n > MAXNAME ? MAXNAME : n] = 0;
            return locker;
        }
        
        /* Other errors */
        if (errno == EACCES)
            return nullptr;
#ifdef EROFS
        if (errno == EROFS)
            return nullptr;
#endif
        return "LOCK ERROR: cannot create lock file";
    }
    
    /* Lock created successfully - write owner info */
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name) {
        safe_strcpy(locker, pw->pw_name, MAXNAME);
    } else {
        safe_strcpy(locker, "unknown", MAXNAME);
    }
    
    safe_strcat(locker, "@", MAXNAME);
    size_t off = strlen(locker);
    if (off < MAXNAME - 1) {
        gethostname(locker + off, (size_t)(MAXNAME - off - 1));
        locker[MAXNAME - 1] = '\0';
    }
    
    ssize_t written = write(fd, locker, strlen(locker));
    (void)written;
    close(fd);
    
    return nullptr;
}


/* undolock -- unlock the file fname
 *
 * if successful, returns nullptr
 * if other error, returns "LOCK ERROR: explanation" */

char *undolock(char *fname)
{
    static char lname[MAXLOCK];

    SAFE_STRCPY(lname, fname);
    SAFE_STRCAT(lname, ".lock~");
    if (unlink(lname) != 0) {
        if (errno == EACCES || errno == ENOENT)
            return nullptr;
#ifdef EROFS
        if (errno == EROFS)
            return nullptr;
#endif
        return "LOCK ERROR: cannot remove lock file";
    }
    return nullptr;
}
