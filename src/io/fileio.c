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

static FILE *ffp;			/* File pointer, all functions. */
static int eofflag;			/* end-of-file flag */

/*
 * Open a file for reading.
 */
int ffropen(const char *fn)
{
	if ((ffp = safe_fopen(fn, FILE_READ)) == NULL)
		return FIOFNF;
	eofflag = FALSE;
	return FIOSUC;
}

/*
 * Open a file for writing. Return TRUE if all is well, and FALSE on error
 * (cannot create).
 */
int ffwopen(const char *fn)
{
	if ((ffp = safe_fopen(fn, FILE_WRITE)) == NULL) {
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
		fline = NULL;
	}
	eofflag = FALSE;


	if (!safe_fclose(&ffp)) {
		REPORT_ERROR(ERR_FILE_WRITE, "Error closing file");
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
	int i;
#if	CRYPT
	char c;			/* character to translate */

	if (cryptflag) {
		for (i = 0; i < nbuf; ++i) {
			c = buf[i] & 0xff;
			myencrypt(&c, 1);
			fputc(c, ffp);
		}
	} else
		for (i = 0; i < nbuf; ++i)
			fputc(buf[i] & 0xFF, ffp);
#else
	for (i = 0; i < nbuf; ++i)
		fputc(buf[i] & 0xFF, ffp);
#endif

	fputc('\n', ffp);

	if (ferror(ffp)) {
		REPORT_ERROR(ERR_FILE_WRITE, "Write I/O error");
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
		fline = NULL;
	}

	/* if we don't have an fline, allocate one */
	if (fline == NULL)
		if ((fline = (char*)safe_alloc(flen = NSTRING, "file line buffer", __FILE__, __LINE__)) == NULL) {
			REPORT_ERROR(ERR_MEMORY, "Failed to allocate file line buffer");
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
				     (char*)safe_alloc(flen + NSTRING, "expanded line buffer", __FILE__, __LINE__)) == NULL)
					return FIOMEM;
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
			REPORT_ERROR(ERR_FILE_READ, "File read error");
			return FIOERR;
		}

		if (i != 0)
			eofflag = TRUE;
		else
			return FIOEOF;
	}

	/* terminate and decrypt the string */
	fline[i] = 0;
#if	CRYPT
	if (cryptflag)
		myencrypt(fline, strlen(fline));
#endif
	return FIOSUC;
}

/*
 * does <fname> exist on disk?
 *
 * char *fname;		file to check for existance
 */
int fexist(const char *fname)
{
	return file_exists(fname) ? TRUE : FALSE;
}
