/*	efunc.h
 *
 *	Function declarations and names.
 *
 *	This file list all the C code functions used and the names to use
 *      to bind keys to them. To add functions,	declare it here in both the
 *      extern function list and the name binding table.
 *
 *	modified by Petri Kutvonen
 */

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

/* External function declarations. */

/* word.c */
extern int wrapword(int f, int n);
extern int move_word_backward(int f, int n);
extern int move_word_forward(int f, int n);
extern int forwsubword(int f, int n);
extern int backsubword(int f, int n);
extern int upperword(int f, int n);
extern int lowerword(int f, int n);
extern int capword(int f, int n);
extern int delete_word_forward(int f, int n);
extern int delete_word_backward(int f, int n);
extern int inword(void);
extern int at_subword_boundary(void);
extern int fillpara(int f, int n);
extern int justpara(int f, int n);
extern int killpara(int f, int n);
extern int wordcount(int f, int n);

/* window.c */
extern int reposition(int f, int n);
extern int redraw(int f, int n);
extern int window_next(int f, int n);
extern int window_prev(int f, int n);
extern int window_move_down(int f, int n);
extern int window_move_up(int f, int n);
extern int window_only(int f, int n);
extern int window_delete(int f, int n);
extern int window_split(int f, int n);
extern int window_enlarge(int f, int n);
extern int window_shrink(int f, int n);
extern int resize(int f, int n);
extern int scroll_other_up(int f, int n);
extern int scroll_other_down(int f, int n);
extern int window_save(int f, int n);
extern int window_restore(int f, int n);
extern int newsize(int f, int n);
extern int newwidth(int f, int n);
extern int window_get_position(void);
extern struct window *wpopup(void);  /* Pop up window creation. */


/* basic.c */
extern int goto_line_start(int f, int n);
extern int move_char_backward(int f, int n);
extern int goto_line_end(int f, int n);
extern int move_char_forward(int f, int n);
extern int gotoline(int f, int n);
extern int goto_buffer_start(int f, int n);
extern int goto_buffer_end(int f, int n);
extern int cursor_down(int f, int n);
extern int cursor_up(int f, int n);
extern int goto_para_start(int f, int n);
extern int goto_para_end(int f, int n);
extern int move_page_down(int f, int n);
extern int move_page_up(int f, int n);
extern int setmark(int f, int n);
extern int swapmark(int f, int n);

/* random.c */
extern int tabsize;  /* Tab size (0: use real tabs). */
extern int showcpos(int f, int n);
extern int getcline(void);
extern int getccol(int bflg);
extern int setccol(int pos);
extern int twiddle(int f, int n);
extern int quote(int f, int n);
extern int insert_tab(int f, int n);
extern int detab(int f, int n);
extern int entab(int f, int n);
extern int trim(int f, int n);
extern int openline(int f, int n);
extern int insert_newline(int f, int n);
extern int cinsert(void);
extern int insert_brace(int n, int c);
extern int insert_pound(void);
extern int deblank(int f, int n);
extern int indent(int f, int n);
extern int delete_char_forward(int f, int n);
extern int delete_char_backward(int f, int n);
extern int kill_to_eol(int f, int n);
extern int setemode(int f, int n);
extern int delmode(int f, int n);
extern int setgmode(int f, int n);
extern int delgmode(int f, int n);
extern int writing_mode_enable(int f, int n);
extern int writing_mode_disable(int f, int n);
extern int settings_load(int f, int n);
extern int save_settings_cmd(int f, int n);
extern int open_user_config_cmd(int f, int n);
extern int list_settings_cmd(int f, int n);
extern int set_column_width_cmd(int f, int n);
extern int adjustmode(int kind, int global);
extern int clrmes(int f, int n);
extern int writemsg(int f, int n);
extern int getfence(int f, int n);
extern int fence_match(int ch);
extern int istring(int f, int n);
extern int ovstring(int f, int n);
extern int duplicate_line(int f, int n);
extern int transpose_line_up(int f, int n);
extern int transpose_line_down(int f, int n);

/* Minimal, Linux‑only command set */

/* main.c */
extern void edinit(const char *bname);
extern int execute_event(input_key_event_t *evt, int f, int n);  /* Event-based dispatch */
extern int quickexit(int f, int n);
extern int quit(int f, int n);
extern int ctlxlp(int f, int n);
extern int ctlxrp(int f, int n);
extern int ctlxe(int f, int n);
extern int ctrlg(int f, int n);
extern int rdonly(void);
extern int resterr(void);
extern int nullproc(int f, int n);
extern int metafn(int f, int n);
extern int cex(int f, int n);
extern int unarg(int f, int n);
/* display.c */
extern void vtinit(void);
extern void vtfree(void);
extern void vttidy(void);
extern void vtmove(int row, int col);
extern int upscreen(int f, int n);
extern int update(int force);
extern void updpos(void);
extern void upddex(void);
extern void updgar(void);
extern int updupd(int force);
extern void upmode(void);
extern void movecursor(int row, int col);
extern void mlerase(void);
extern void mlwrite(const char *fmt, ...);
extern void mlforce(const char *s);
extern void mlputs(const char *s);
extern void getscreensize(int *widthp, int *heightp);
extern void sizesignal(int signr);

/* region.c */
extern int region_kill(int f, int n);
extern int region_copy(int f, int n);
extern int lowerregion(int f, int n);
extern int upperregion(int f, int n);
extern int getregion(struct region *rp);

/* posix.c / ansi.c */
extern void ttopen(void);
extern void ttclose(void);
extern int ttputc(int c);
extern void ttputs(const char *s);      /* Bulk string write */
extern void ttwrite(const char *data, int len);  /* Bulk binary write */
extern void ttflush(void);
extern int terminal_read_byte(void);
extern int input_pending(void);

/* input.c */
extern int mlyesno(const char *prompt);
extern int minibuf_confirm(const char *prompt);
extern fn_t getname(void);
extern fn_t minibuf_read_command(void);
extern int minibuf_read(const char *prompt, char *buf, int nbuf);
extern int input_read_byte(void);
extern int input_read_event(input_key_event_t *out);
extern void outstring(const char *s);
extern void ostring(const char *s);
/* test helper */
extern void input_reset_parser_state(void);

/* bind.c */
extern int help(int f, int n);
extern int describe_key_binding(int f, int n);
extern int bindtokey(int f, int n);
extern int unbindkey(int f, int n);
extern int describe_all_bindings(int f, int n);
extern int apropos_command(int f, int n);
extern int buildlist(int type, const char *mstring);
extern int string_contains(const char *source, const char *sub);
extern keymap_key_t read_key(int mflag);               /* Read key as modern keymap_key_t */
extern int startup(const char *sfname);
extern const char *flook(const char *fname, int hflag);
extern void cmdstr_key(keymap_key_t key, char *seq);   /* Modern key to string */
extern fn_t getbind_event(input_key_event_t *evt);     /* Event-based binding lookup */
extern char *getfname(fn_t);
extern fn_t fncmatch(const char *);
extern keymap_key_t stock_key(const char *keyname);    /* String to keymap_key_t */
extern char *transbind(const char *skey);

/* buffer.c */
extern int usebuffer(int f, int n);
extern int nextbuffer(int f, int n);
extern int swbuffer(struct buffer *bp);
extern int killbuffer(int f, int n);
extern int zotbuf(struct buffer *bp);
extern int namebuffer(int f, int n);
extern int listbuffers(int f, int n);
extern int makelist(int iflag);
extern int addline(const char *text);
extern int anycb(void);
extern int bclear(struct buffer *bp);
extern int unmark(int f, int n);
/* Lookup a buffer by name. */
extern struct buffer *bfind(const char *bname, int cflag, int bflag);

/* file.c */
extern int fileread(int f, int n);
extern int insfile(int f, int n);
extern int filefind(int f, int n);
extern int viewfile(int f, int n);
extern int getfile(const char *fname, int lockfl);
extern int readin(const char *fname, int lockfl);
extern void makename(char *bname, const char *fname);
extern void unqname(char *name);
extern int filewrite(int f, int n);
extern int filesave(int f, int n);
extern int writeout(const char *fn);
extern int filename(int f, int n);
extern int ifile(const char *fname);

/* fileio.c */
extern int ffropen(const char *fn);
extern int ffwopen(const char *fn);
extern int ffclose(void);
extern int ffputline(const char *buf, int nbuf);
extern int ffgetline(void);
extern int fexist(const char *fname);

/* exec.c */
extern int namedcmd(int f, int n);
extern int execcmd(int f, int n);
extern int docmd(const char *cline);
extern char *token(char *src, char *tok, int size);
extern int macarg(char *tok);
extern int nextarg(const char *prompt, char *buffer, int size);
extern int storemac(int f, int n);
extern int storeproc(int f, int n);
extern int execproc(int f, int n);
extern int execbuf(int f, int n);
extern int dobuf(struct buffer *bp);
extern void freewhile(struct while_block *wp);
extern int execfile(int f, int n);
extern int dofile(const char *fname);
extern int cbuf(int f, int n, int bufnum);

// Modern C23 unified buffer dispatch - replaces 40 identical functions  
#include "cbuf_dispatch.h"
extern int cbuf1(int f, int n); extern int cbuf2(int f, int n); extern int cbuf3(int f, int n); extern int cbuf4(int f, int n); extern int cbuf5(int f, int n);
extern int cbuf6(int f, int n); extern int cbuf7(int f, int n); extern int cbuf8(int f, int n); extern int cbuf9(int f, int n); extern int cbuf10(int f, int n);
extern int cbuf11(int f, int n); extern int cbuf12(int f, int n); extern int cbuf13(int f, int n); extern int cbuf14(int f, int n); extern int cbuf15(int f, int n);
extern int cbuf16(int f, int n); extern int cbuf17(int f, int n); extern int cbuf18(int f, int n); extern int cbuf19(int f, int n); extern int cbuf20(int f, int n);
extern int cbuf21(int f, int n); extern int cbuf22(int f, int n); extern int cbuf23(int f, int n); extern int cbuf24(int f, int n); extern int cbuf25(int f, int n);
extern int cbuf26(int f, int n); extern int cbuf27(int f, int n); extern int cbuf28(int f, int n); extern int cbuf29(int f, int n); extern int cbuf30(int f, int n);
extern int cbuf31(int f, int n); extern int cbuf32(int f, int n); extern int cbuf33(int f, int n); extern int cbuf34(int f, int n); extern int cbuf35(int f, int n);
extern int cbuf36(int f, int n); extern int cbuf37(int f, int n); extern int cbuf38(int f, int n); extern int cbuf39(int f, int n); extern int cbuf40(int f, int n);

/* spawn.c */
extern int spawncli(int f, int n);
extern int bktoshell(int f, int n);
extern void rtfrmshell(void);
extern int spawn(int f, int n);
extern int execprg(int f, int n);
extern int pipecmd(int f, int n);
extern int filter_buffer(int f, int n);
extern int sys(const char *cmd);
extern int shellprog(char *cmd);
extern int execprog(char *cmd);

/* search.c */
extern int forwsearch(int f, int n);
extern int forwhunt(int f, int n);
extern int backsearch(int f, int n);
extern int backhunt(int f, int n);
extern int scanner(const char *patrn, int direct, int beg_or_end);
extern int eq(unsigned char bc, unsigned char pc);
extern void rvstrcpy(char *rvstr, char *str, size_t maxlen);
extern int sreplace(int f, int n);
extern int qreplace(int f, int n);
extern int delins(int dlength, char *instr, int use_meta);
extern int expandp(char *srcstr, char *deststr, int maxlength);
extern int boundry(struct line *curline, int curoff, int dir);
extern void mcclear(void);
extern void rmcclear(void);

/* isearch.c */
extern int risearch(int f, int n);
extern int fisearch(int f, int n);
extern int isearch(int f, int n);
extern int checknext(char chr, char *patrn, int dir);
extern int scanmore(char *patrn, int dir);
extern int match_pat(char *patrn);
extern int promptpattern(char *prompt);
extern int get_char(void);
extern int uneat(void);
extern void reeat(int c);

/* eval.c */
extern void varinit(void);
extern const char *eval_get_function(char *fname);
extern char *eval_get_user_var(char *vname);
extern char *eval_get_env_var(const char *vname);
extern char *getkill(void);
extern int setvar(int f, int n);
extern void findvar(char *var, struct variable_description *vd, int size);
extern int svar(struct variable_description *var, const char *value);
extern char *int_to_string(int i);
extern int token_get_type(char *token);
extern char *getval(char *token, char *result, int size);
extern int string_to_bool(const char *val);
extern char *bool_to_string(int val);
extern char *string_to_upper(char *str);
extern char *string_to_lower(char *str);
extern int abs(int x);
extern int editor_random(void);
extern int string_find_index(const char *source, const char *pattern);
extern char *string_translate(char *source, char *lookup, char *trans);


/* lock.c */
extern int lockchk(char *fname);
extern int lockrel(void);
extern int lock(char *fname);
extern int unlock(char *fname);
extern void lckerror(char *errstr);

/* pklock.c */
extern char *dolock(char *fname);
extern char *undolock(char *fname);

#endif /* FUNCTIONS_H */
