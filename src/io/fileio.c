/*	FILEIO.C
 *
 * The routines in this file read and write ASCII files from the disk. All of
 * the knowledge about files are here.
 *
 *	modified by Petri Kutvonen
 */

#include        <stdio.h>
#include	"estruct.h"
#include        "edef.h"
#include	"efunc.h"
#include	"memory.h"
#include	"error.h"
#include	"file_utils.h"
#include	"util/logger.h"

static FILE *ffp;			/* File pointer, all functions. */
static int eofflag;			/* end-of-file flag */

/*
 * Open a file for reading.
 * Uses raw fopen() to avoid error logging during file searches (e.g., .emacsrc in PATH).
 * Returns FIOFNF if file not found - caller handles this gracefully.
 */
int ffropen(const char *fn)
{
	if ((ffp = fopen(fn, "r")) == nullptr)
		return FIOFNF;
	eofflag = false;
	return FIOSUC;
}

/*
 * Open a file for writing. Return true if all is well, and false on error
 * (cannot create).
 */
int ffwopen(const char *fn)
{
	if ((ffp = safe_fopen(fn, FILE_WRITE)) == nullptr) {
		REPORT_ERROR(ERR_FILE_WRITE, fn);
		return FIOERR;
	}
	return FIOSUC;
}

/*
 * Close a file. Should look at the status in all systems.
 */
int ffclose(void)
{
	/* free this since we do not need it anymore */
	if (fline) {
		SAFE_FREE(fline);
		fline = nullptr;
	}
	eofflag = false;


	if (!safe_fclose(&ffp)) {
		REPORT_ERROR(ERR_FILE_WRITE, "ERROR CLOSING FILE");
		return FIOERR;
	}
	return FIOSUC;
}

/*
 * Write a line to the already opened file. The "buf" points to the buffer,
 * and the "nbuf" is its length, less the free newline. Return the status.
 * Check only at the newline.
 */
int ffputline(char *buf, int nbuf)
{
	/* Bulk write instead of character-by-character (major I/O optimization) */
	if (nbuf > 0) {
		if (fwrite(buf, 1, (size_t)nbuf, ffp) != (size_t)nbuf) {
			REPORT_ERROR(ERR_FILE_WRITE, "WRITE I/O ERROR");
			return FIOERR;
		}
	}

	if (fputc('\n', ffp) == EOF) {
		REPORT_ERROR(ERR_FILE_WRITE, "WRITE I/O ERROR");
		return FIOERR;
	}

	return FIOSUC;
}

/*
 * Read a line from a file, and store the bytes in the supplied buffer. The
 * "nbuf" is the length of the buffer. Complain about long lines and lines
 * at the end of the file that don't have a newline present. Check for I/O
 * errors too. Return status.
 */
int ffgetline(void)
{
	int c;		/* current character read */
	int i;		/* current index into fline */
	char *tmpline;	/* temp storage for expanding line */

	/* if we are at the end...return it */
	if (eofflag)
		return FIOEOF;

	/* dump fline if it ended up too big */
	if (flen > NSTRING) {
		SAFE_FREE(fline);
		fline = nullptr;
	}

	/* if we don't have an fline, allocate one */
	if (fline == nullptr)
		if ((fline = (char*)safe_alloc(flen = NSTRING, "file line buffer", __FILE__, __LINE__)) == nullptr) {
			REPORT_ERROR(ERR_MEMORY, "FAILED TO ALLOCATE FILE LINE BUFFER");
			return FIOMEM;
		}

	/* read the line in */
	if (!nullflag) {
		// Check for EOF first, then try to read line
		if (feof(ffp)) {
			i = 0;
			c = EOF;
		} else {
			size_t read_len = safe_fread_line(fline, NSTRING, ffp);
			if (read_len == 0 && feof(ffp)) {
				// True EOF
				i = 0; 
				c = EOF;
			} else {
				// Valid line (could be empty)
				i = strlen(fline);
				c = '\n';  // safe_fread_line already removed newline, so simulate it
			}
		}
	} else {
		i = 0;
		c = fgetc(ffp);
	}
	while (c != EOF && c != '\n') {
		if (c) {
			fline[i++] = c;
			/* if it's longer, get more room */
			if (i >= flen) {
				if ((tmpline =
				     (char*)safe_alloc(flen + NSTRING, "expanded line buffer", __FILE__, __LINE__)) == nullptr) {
					LOG_ERRORF("FileIO: Line buffer expansion failed (flen=%d)", flen);
					return FIOMEM;
				}
                if (flen > 0) {
                    size_t maxlen = NSTRING - 1;
                    size_t cpy = (size_t)flen;
                    if (cpy > maxlen) cpy = maxlen;
                    memcpy(tmpline, fline, cpy);
                    tmpline[cpy] = '\0';
                } else {
                    tmpline[0] = '\0';
                }
				flen += NSTRING;
				SAFE_FREE(fline);
				fline = tmpline;
			}
		}
		c = fgetc(ffp);
	}

	/* test for any errors that may have occured */
	if (c == EOF) {
		if (ferror(ffp)) {
			REPORT_ERROR(ERR_FILE_READ, "FILE READ ERROR");
			return FIOERR;
		}

		if (i != 0)
			eofflag = true;
		else
			return FIOEOF;
	}

	/* terminate the string */
	fline[i] = 0;

#if UEMACS_DEBUG_LOG
	/* DEBUG: Hex dump first line to trace UTF-8 corruption */
	if (i > 0 && i < 40) {
		static int line_num = 0;
		if (line_num == 0) {
			LOG_DEBUGF("FILEIO_READ: line=%d bytes[0..9] = %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			           line_num,
			           i > 0 ? (unsigned char)fline[0] : 0,
			           i > 1 ? (unsigned char)fline[1] : 0,
			           i > 2 ? (unsigned char)fline[2] : 0,
			           i > 3 ? (unsigned char)fline[3] : 0,
			           i > 4 ? (unsigned char)fline[4] : 0,
			           i > 5 ? (unsigned char)fline[5] : 0,
			           i > 6 ? (unsigned char)fline[6] : 0,
			           i > 7 ? (unsigned char)fline[7] : 0,
			           i > 8 ? (unsigned char)fline[8] : 0,
			           i > 9 ? (unsigned char)fline[9] : 0);
		}
		line_num++;
	}
#endif

	/* Old CRYPT removed - use encrypt.c */
	return FIOSUC;
}

/*
 * does <fname> exist on disk?
 *
 * char *fname;		file to check for existance
 */
int fexist(const char *fname)
{
	return file_exists(fname) ? true : false;
}
