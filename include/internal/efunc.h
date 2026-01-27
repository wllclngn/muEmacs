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

#ifndef EFUNC_H_
#define EFUNC_H_

#include <stddef.h>  /* for size_t */
#include "terminal/input_state.h"  /* for input_key_event_t */
#include "keymap.h"  /* for keymap_key_t */

struct magic;  /* Forward declarations */
struct region;
struct line;
struct while_block;
struct variable_description;

/* External function declarations. */

typedef int (*fn_t)(int f, int n);

/* vim_bindings.c */
extern int evil_mode(int f, int n);
extern void vim_init_keymaps(void);
extern int cmd_focus_cycle(int f, int n);
extern void focus_reset_to_editor(void);

/* Vim motion and command functions - bindable from TOML */
extern int vim_move_left(int f, int n);
extern int vim_move_down(int f, int n);
extern int vim_move_up(int f, int n);
extern int vim_move_right(int f, int n);
extern int vim_word_forward(int f, int n);
extern int vim_word_backward(int f, int n);
extern int vim_word_end(int f, int n);
extern int vim_line_start(int f, int n);
extern int vim_line_end(int f, int n);
extern int vim_first_nonblank(int f, int n);
extern int vim_first_nonblank_wrapped(int f, int n);
extern int vim_end_of_word(int f, int n);
extern int vim_enter_insert_mode(int f, int n);
extern int vim_enter_normal_mode(int f, int n);
extern int vim_enter_normal_mode_external(int f, int n);
extern int vim_append(int f, int n);
extern int vim_append_eol(int f, int n);
extern int vim_insert_bol(int f, int n);
extern int vim_open_line_below(int f, int n);
extern int vim_open_line_above(int f, int n);
extern int vim_delete_char(int f, int n);
extern int vim_delete_operator(int f, int n);
extern int vim_change_operator(int f, int n);
extern int vim_yank_operator(int f, int n);
extern int vim_enter_visual_mode(int f, int n);
extern int vim_enter_visual_line_mode(int f, int n);
extern int vim_enter_visual_block_mode(int f, int n);
extern int vim_exit_visual_mode(int f, int n);
extern int vim_visual_delete(int f, int n);
extern int vim_visual_yank(int f, int n);
extern int vim_visual_change(int f, int n);
extern int vim_find_char_forward(int f, int n);
extern int vim_find_char_backward(int f, int n);
extern int vim_till_char_forward(int f, int n);
extern int vim_till_char_backward(int f, int n);
extern int vim_repeat_find(int f, int n);
extern int vim_repeat_find_reverse(int f, int n);

/* vim_bindings.c - helpers shared with vim modules */
extern int vim_getkey(void);
extern int vim_get_effective_count(void);
extern void vim_clear_pending_state(void);
extern void vim_store_to_register(int is_delete, int linewise);
extern void vim_record_text_object_change(char op, char prefix, char object, int count);

/* vim_text_objects.c - text object selection helpers */
extern int vim_select_word_object(bool inner);
extern int vim_select_quote_object(char quote, bool inner);
extern int vim_select_bracket_object(char open, char close, bool inner);

extern int vim_replace_char(int f, int n);
extern int vim_enter_replace_mode(int f, int n);
extern int vim_toggle_case(int f, int n);
extern int vim_set_mark(int f, int n);
extern int vim_goto_mark_line(int f, int n);
extern int vim_goto_mark_exact(int f, int n);
extern int vim_search_forward(int f, int n);
extern int vim_search_backward(int f, int n);
extern int vim_search_next(int f, int n);
extern int vim_search_prev(int f, int n);
extern int vim_search_word_forward(int f, int n);
extern int vim_search_word_backward(int f, int n);
extern int vim_text_object_inner(int f, int n);
extern int vim_text_object_around(int f, int n);
extern int vim_register_prefix(int f, int n);
extern int vim_put_after(int f, int n);
extern int vim_put_before(int f, int n);
extern int vim_dot_repeat(int f, int n);
extern int vim_goto_line_or_end(int f, int n);
extern int vim_goto_top(int f, int n);
extern int vim_goto_prefix(int f, int n);
extern int vim_digit_0(int f, int n);
extern int vim_digit_1(int f, int n);
extern int vim_digit_2(int f, int n);
extern int vim_digit_3(int f, int n);
extern int vim_digit_4(int f, int n);
extern int vim_digit_5(int f, int n);
extern int vim_digit_6(int f, int n);
extern int vim_digit_7(int f, int n);
extern int vim_digit_8(int f, int n);
extern int vim_digit_9(int f, int n);

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
/* Terminal overlay support */
extern void adjust_windows_for_overlay(int overlay_height);
extern void restore_windows_from_overlay(void);


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

/* undo.c */
#include "undo.h"

/* Minimal, Linux‑only command set */

/* main.c */
extern int uemacs_main_entry(int argc, char *argv[]);
extern void edinit(char *bname);
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
extern int cexit(int status);

/* display.c */
extern void vtinit(void);
extern void vtinit_test(void);  /* Test-only: allocate screens without terminal I/O */
extern void vtfree(void);
extern void vttidy(void);
extern void vtmove(int row, int col);
extern void vtputc(int c);  /* Write character to virtual screen */
extern int upscreen(int f, int n);
extern int update(int force);
extern void updpos(void);
extern void upddex(void);
extern int get_line_number_cached(struct window *wp);
extern void invalidate_line_cache(struct window *wp);
extern void updgar(void);

/* Display dirty tracking (montauk/OUROBOROS pattern) */
extern void mark_buffer_dirty(struct buffer *bp);
extern bool display_needs_update(void);
extern void mark_display_clean(void);
extern int updupd(int force);
extern void upmode(void);
extern void modern_modeline(struct window *wp);  /* modeline.c */
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
extern int copy_to_clipboard(int f, int n);
extern int clipboard_provider_cmd(int f, int n);
extern int lowerregion(int f, int n);
extern int upperregion(int f, int n);
extern int getregion(struct region *rp);

/* posix.c / curses.c */
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
extern void input_reset_parser_state(void);
/* Mouse interactions removed (keyboard-only) */
/* Writing mode helpers */
extern void writing_mode_reset(void);
extern int writing_mode_enable(int f, int n);
extern int writing_mode_disable(int f, int n);

/* User settings (JSON) and friendly commands */
extern int settings_load(int f, int n);
extern int save_settings_cmd(int f, int n);
extern int open_user_config_cmd(int f, int n);
extern int list_settings_cmd(int f, int n);
extern int set_column_width_cmd(int f, int n);
extern int soft_wrap_toggle_cmd(int f, int n);
extern int soft_wrap_column_cmd(int f, int n);


extern void outstring(const char *s);
extern void ostring(const char *s);

/* bind.c */
extern int help(int f, int n);
extern int describe_key_binding(int f, int n);
extern int bindtokey(int f, int n);
extern int unbindkey(int f, int n);
extern int localsetkey(int f, int n);
extern int localunsetkey(int f, int n);
extern int describe_all_bindings(int f, int n);
extern int apropos_command(int f, int n);
extern int buildlist(int type, const char *mstring);
extern int string_contains(const char *source, const char *sub);
extern keymap_key_t read_key(int mflag);      /* Read key as modern keymap_key_t */
extern int startup(const char *sfname);
extern const char *flook(const char *fname, int hflag);
extern void cmdstr_key(keymap_key_t key, char *seq);  /* Modern key to string */
extern fn_t getbind_event(input_key_event_t *evt);    /* Event-based binding lookup */
extern char *getfname(fn_t);
extern fn_t fncmatch(const char *);
extern keymap_key_t stock_key(const char *keyname);   /* String to keymap_key_t */
extern char *transbind(const char *skey);
/* Optional help prefix toggles */
extern int help_prefix_enable(int f, int n);
extern int help_prefix_disable(int f, int n);
/* Keymap statistics */
extern int keymap_stats_cmd(int f, int n);

/* buffer.c */
extern int usebuffer(int f, int n);
extern int nextbuffer(int f, int n);
extern int swbuffer(struct buffer *bp);
extern int killbuffer(int f, int n);
extern int zotbuf(struct buffer *bp);
extern int namebuffer(int f, int n);
extern int listbuffers(int f, int n);
extern int makelist(int iflag);
extern int addline(char *text);
extern int anycb(void);
extern int bclear(struct buffer *bp);
extern int unmark(int f, int n);
extern struct buffer *bfind(char *bname, int cflag, int bflag);
extern void buffer_update_stats_incremental(struct buffer *bp, int line_delta, long byte_delta, int word_delta);
extern void buffer_mark_stats_dirty(struct buffer *bp);
extern void buffer_get_stats_fast(struct buffer *bp, int *line_count, long *byte_count, int *word_count);
extern int buffer_index_insert(struct buffer *bp, int line_idx, struct line *lp);
extern int buffer_index_delete(struct buffer *bp, int line_idx);
extern struct line *buffer_index_get(struct buffer *bp, int line_idx);
extern int buffer_rebuild_index(struct buffer *bp);

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

/* encrypt.c - external tool encryption */
extern int encrypt_buffer_gpg(int f, int n);
extern int decrypt_file_gpg(int f, int n);
extern int encrypt_buffer_age(int f, int n);
extern int decrypt_file_age(int f, int n);
extern int encrypt_buffer_auto(int f, int n);
extern int show_encryption_tools(int f, int n);

/* fileio.c */
extern int ffropen(const char *fn);
extern int ffwopen(const char *fn);
extern int ffclose(void);
extern int ffputline(char *buf, int nbuf);
extern int ffgetline(void);
extern int fexist(const char *fname);

/* exec.c */
extern int namedcmd(int f, int n);
extern int execcmd(int f, int n);
extern int docmd(const char *cline);
extern const char *token(const char *src, char *tok, int size);
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

// C23 unified buffer dispatch - legacy efunc.h compatibility
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
extern int sys(char *cmd);
extern int shellprog(char *cmd);
extern int execprog(char *cmd);

/* term_cmd.c - Terminal integration */
extern int term_open(int f, int n);
extern int term_close(int f, int n);
extern int term_toggle(int f, int n);
extern int term_new_buffer(int f, int n);
extern int term_send_key(int c);
extern int term_handle_input(int c);

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
extern int checknext(char chr, const char *patrn, int dir);
extern int scanmore(char *patrn, int dir);
extern int match_pat(const char *patrn);
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

/* crypt.c - REMOVED, CRYPT disabled */
/* extern int set_encryption_key(int f, int n); */
/* extern void myencrypt(char *bptr, unsigned len); */

/* lock.c */
extern int lockchk(char *fname);
extern int lockrel(void);
extern int lock(char *fname);
extern int unlock(char *fname);
extern void lckerror(char *errstr);

/* pklock.c */
extern char *dolock(char *fname);
extern char *undolock(char *fname);

/* terminal.c */
extern int terminal_buffer_open(int f, int n);
extern int terminal_capture(int f, int n);
extern int terminal_close(int f, int n);
extern int terminal_create(int f, int n);
extern int terminal_list(int f, int n);
extern int terminal_send_buffer(int f, int n);
extern int terminal_send_line(int f, int n);
extern int terminal_send_region(int f, int n);
extern int terminal_switch(int f, int n);
extern int terminal_toggle(int f, int n);

/* uep_format.c - UEP Extension Protocol */
extern int uep_format_buffer(int f, int n);

/* terminal.c - REPL integration */
extern int repl_start(int f, int n);
extern int repl_eval_line(int f, int n);
extern int repl_eval_region(int f, int n);
extern int repl_eval_buffer(int f, int n);

/* terminal.c - Build integration */
extern int build_run(int f, int n);
extern int build_next_error(int f, int n);
extern int build_prev_error(int f, int n);

/* extension.c - Extension system */
extern int extension_load_cmd(int f, int n);
extern int extension_unload_cmd(int f, int n);
extern int extension_list_cmd(int f, int n);
extern void extension_init(void);
extern void extension_cleanup(void);
extern void extension_autoload(void);
extern void extension_set_autoload_dir(const char *dir);
extern const char *extension_get_autoload_dir(void);

/* extension_api.c - Extension API */
typedef int (*uemacs_cmd_fn)(int f, int n);
extern uemacs_cmd_fn extension_find_command(const char *name);

/* uep_scripts.c - Layer 2 Script Extensions */
extern int uep_scripts_list_cmd(int f, int n);
extern int uep_scripts_reload_cmd(int f, int n);
extern void uep_scripts_init(void);
extern void uep_scripts_set_dir(const char *dir);
extern int uep_scripts_load(void);
extern void uep_scripts_cleanup(void);
extern int uep_scripts_try_execute(const char *name);
/* Event bus emit functions (extension_api.c) */
extern void extension_emit_buffer_save(struct buffer *bp);
extern void extension_emit_buffer_load(struct buffer *bp);
extern bool extension_emit_key(int key);  /* Returns true if key was consumed */
extern void extension_emit_idle(void);
extern bool extension_emit_mouse(struct input_key_event *evt);  /* Returns true if consumed */
extern int extension_emit_char_insert(int c, int *out);  /* 0=no transform, 1=use out, -1=delete+insert */
extern void extension_api_init(void);
extern void extension_api_cleanup(void);

#endif  /* EFUNC_H_ */
