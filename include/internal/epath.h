// EPATH.H - System paths for initialization files
#ifndef EPATH_H_
#define EPATH_H_

// Linux paths for configuration and help files
static char *pathname[] = {
	".emacsrc", "emacs.hlp",
#if PKCODE
	"/usr/global/lib/", "/usr/local/bin/", "/usr/local/lib/",
#endif
	"/usr/local/", "/usr/lib/", ""
};

#endif  /* EPATH_H_ */
