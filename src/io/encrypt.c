/*
 * encrypt.c - Modern encryption integration via external tools
 *
 * Shell out to gpg/age/openssl for proper cryptography
 * No homebrew crypto - use battle-tested system tools
 *
 * C23 modernization: Uses posix_spawn() instead of system()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <spawn.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "string_utils.h"

/*
 * run_shell_cmd - Modern replacement for system() using posix_spawn()
 *
 * Spawns /bin/sh -c "command" and waits for completion.
 * Returns the exit status (0 = success, non-zero = failure).
 */
static int run_shell_cmd(const char *cmd)
{
    pid_t pid;
    int status;
    char *argv[] = {"/bin/sh", "-c", (char *)cmd, NULL};

    if (posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, environ) != 0) {
        return -1;  /* spawn failed */
    }

    /* Wait for child to complete */
    if (waitpid(pid, &status, 0) == -1) {
        return -1;
    }

    return status;
}

/* Check if a command exists in PATH */
static int command_exists(const char *cmd) {
    char test_cmd[256];
    snprintf(test_cmd, sizeof(test_cmd), "command -v %s >/dev/null 2>&1", cmd);
    return run_shell_cmd(test_cmd) == 0;
}

/* Detect best available encryption tool */
static const char* detect_crypto_tool(void) {
    if (command_exists("gpg")) return "gpg";
    if (command_exists("age")) return "age";
    if (command_exists("openssl")) return "openssl";
    return nullptr;
}

/*
 * Encrypt current buffer with gpg
 * M-x encrypt-buffer
 */
int encrypt_buffer_gpg(int f, int n) {
    char outfile[NFILEN + 8];  /* Extra space for .gpg suffix */
    char cmd[NSTRING];
    int status;

    if (curbp->b_mode & MDVIEW)
        return rdonly();

    if (!command_exists("gpg")) {
        mlwrite("GPG NOT FOUND. INSTALL GNUPG PACKAGE.");
        return false;
    }
    
    /* Save current buffer first */
    if (filesave(false, 0) != true) {
        mlwrite("FAILED TO SAVE BUFFER BEFORE ENCRYPTION");
        return false;
    }
    
    /* Generate output filename */
    if (strlen(curbp->b_fname) > NFILEN - 5) {
        mlwrite("FILENAME TOO LONG FOR .GPG SUFFIX");
        return false;
    }
    snprintf(outfile, sizeof(outfile), "%s.gpg", curbp->b_fname);
    
    /* Encrypt with gpg symmetric encryption */
    snprintf(cmd, sizeof(cmd), "gpg --symmetric --cipher-algo AES256 -o %s %s",
             outfile, curbp->b_fname);
    
    mlwrite("ENCRYPTING WITH GPG [YOU'LL BE PROMPTED FOR PASSWORD]...");
    TTflush();
    TTclose();
    TTkclose();
    
    status = run_shell_cmd(cmd);
    
    TTopen();
    TTkopen();
    sgarbf = true;
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        mlwrite("ENCRYPTED: %s", outfile);
        return true;
    } else {
        mlwrite("GPG ENCRYPTION FAILED");
        return false;
    }
}

/*
 * Decrypt and open a gpg-encrypted file
 * M-x decrypt-file
 */
int decrypt_file_gpg(int f, int n) {
    char fname[NFILEN];
    char tmpfile[NFILEN];
    char cmd[NSTRING];
    int status;
    FILE *tmpfp;
    
    /* Get filename to decrypt */
    status = minibuf_read("DECRYPT FILE: ", fname, NFILEN);
    if (status != true)
        return status;
    
    if (!command_exists("gpg")) {
        mlwrite("GPG NOT FOUND. INSTALL GNUPG PACKAGE.");
        return false;
    }
    
    /* Create secure temp file for decrypted content */
    const char *tmpdir = getenv("XDG_RUNTIME_DIR");
    if (!tmpdir || !tmpdir[0]) tmpdir = "/tmp";
    
    snprintf(tmpfile, sizeof(tmpfile), "%s/uemacs_decrypt_XXXXXX", tmpdir);
    int fd = mkstemp(tmpfile);
    if (fd < 0) {
        mlwrite("FAILED TO CREATE TEMP FILE");
        return false;
    }
    close(fd);
    
    /* Decrypt with gpg */
    snprintf(cmd, sizeof(cmd), "gpg --decrypt -o %s %s 2>/dev/null", tmpfile, fname);
    
    mlwrite("DECRYPTING WITH GPG...");
    TTflush();
    TTclose();
    TTkclose();
    
    status = run_shell_cmd(cmd);
    
    TTopen();
    TTkopen();
    sgarbf = true;
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        /* Open the decrypted temp file */
        status = getfile(tmpfile, true);
        
        /* Set buffer name to safe plaintext name to prevent overwriting encrypted file */
        char safe_name[NFILEN];
        safe_strcpy(safe_name, fname, NFILEN);
        char *ext = strrchr(safe_name, '.');
        if (ext && strcmp(ext, ".gpg") == 0) {
            *ext = '\0'; /* Strip .gpg */
        } else {
            safe_strcat(safe_name, ".plain", NFILEN);
        }
        
        safe_strcpy(curbp->b_fname, safe_name, NFILEN);
        
        /* Delete temp file */
        unlink(tmpfile);
        
        if (status == true) {
            mlwrite("DECRYPTED to: %s. USE 'M-x encrypt-buffer' TO SAVE!", safe_name);
            curbp->b_flag |= BFCHG;  /* Mark as changed */
        }
        return status;
    } else {
        unlink(tmpfile);
        mlwrite("GPG DECRYPTION FAILED [WRONG PASSWORD?]");
        return false;
    }
}

/*
 * Encrypt current buffer with age (modern alternative)
 * M-x encrypt-buffer-age
 */
int encrypt_buffer_age(int f, int n) {
    char outfile[NFILEN + 8];  /* Extra space for .age suffix */
    char cmd[NSTRING];
    int status;

    if (curbp->b_mode & MDVIEW)
        return rdonly();

    if (!command_exists("age")) {
        mlwrite("AGE NOT FOUND. INSTALL WITH: CARGO INSTALL AGE OR YOUR PACKAGE MANAGER.");
        return false;
    }
    
    if (filesave(false, 0) != true) {
        mlwrite("FAILED TO SAVE BUFFER BEFORE ENCRYPTION");
        return false;
    }
    
    if (strlen(curbp->b_fname) > NFILEN - 5) {
        mlwrite("FILENAME TOO LONG FOR .AGE SUFFIX");
        return false;
    }
    snprintf(outfile, sizeof(outfile), "%s.age", curbp->b_fname);
    snprintf(cmd, sizeof(cmd), "age -p -o %s %s", outfile, curbp->b_fname);
    
    mlwrite("ENCRYPTING WITH AGE [YOU'LL BE PROMPTED FOR PASSPHRASE]...");
    TTflush();
    TTclose();
    TTkclose();
    
    status = run_shell_cmd(cmd);
    
    TTopen();
    TTkopen();
    sgarbf = true;
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        mlwrite("ENCRYPTED: %s", outfile);
        return true;
    } else {
        mlwrite("AGE ENCRYPTION FAILED");
        return false;
    }
}

/*
 * Decrypt age-encrypted file
 * M-x decrypt-file-age
 */
int decrypt_file_age(int f, int n) {
    char fname[NFILEN];
    char tmpfile[NFILEN];
    char cmd[NSTRING];
    int status;
    
    status = minibuf_read("DECRYPT AGE FILE: ", fname, NFILEN);
    if (status != true)
        return status;
    
    if (!command_exists("age")) {
        mlwrite("AGE NOT FOUND. INSTALL WITH: CARGO INSTALL AGE OR YOUR PACKAGE MANAGER.");
        return false;
    }
    
    const char *tmpdir = getenv("XDG_RUNTIME_DIR");
    if (!tmpdir || !tmpdir[0]) tmpdir = "/tmp";
    
    snprintf(tmpfile, sizeof(tmpfile), "%s/uemacs_decrypt_XXXXXX", tmpdir);
    int fd = mkstemp(tmpfile);
    if (fd < 0) {
        mlwrite("FAILED TO CREATE TEMP FILE");
        return false;
    }
    close(fd);
    
    snprintf(cmd, sizeof(cmd), "age -d -o %s %s 2>/dev/null", tmpfile, fname);
    
    mlwrite("DECRYPTING WITH AGE...");
    TTflush();
    TTclose();
    TTkclose();
    
    status = run_shell_cmd(cmd);
    
    TTopen();
    TTkopen();
    sgarbf = true;
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        status = getfile(tmpfile, true);
        
        /* Set buffer name to safe plaintext name */
        char safe_name[NFILEN];
        safe_strcpy(safe_name, fname, NFILEN);
        char *ext = strrchr(safe_name, '.');
        if (ext && strcmp(ext, ".age") == 0) {
            *ext = '\0'; /* Strip .age */
        } else {
            safe_strcat(safe_name, ".plain", NFILEN);
        }
        
        safe_strcpy(curbp->b_fname, safe_name, NFILEN);
        unlink(tmpfile);
        
        if (status == true) {
            mlwrite("DECRYPTED to: %s. USE 'M-x encrypt-buffer-age' TO SAVE!", safe_name);
            curbp->b_flag |= BFCHG;
        }
        return status;
    } else {
        unlink(tmpfile);
        mlwrite("AGE DECRYPTION FAILED");
        return false;
    }
}

/*
 * Auto-detect and use best available encryption tool
 * M-x encrypt-buffer-auto
 */
int encrypt_buffer_auto(int f, int n) {
    const char *tool = detect_crypto_tool();
    
    if (!tool) {
        mlwrite("NO ENCRYPTION TOOL FOUND. INSTALL GPG, AGE, OR OPENSSL.");
        return false;
    }
    
    if (strcmp(tool, "gpg") == 0) {
        return encrypt_buffer_gpg(f, n);
    } else if (strcmp(tool, "age") == 0) {
        return encrypt_buffer_age(f, n);
    }
    
    mlwrite("ENCRYPTION TOOL DETECTION FAILED");
    return false;
}

/*
 * Show available encryption tools
 * M-x show-encryption-tools
 */
int show_encryption_tools(int f, int n) {
    char msg[NSTRING];
    int found = 0;
    
    safe_strcpy(msg, "Available encryption tools: ", NSTRING);
    
    if (command_exists("gpg")) {
        safe_strcat(msg, "gpg ", NSTRING);
        found++;
    }
    if (command_exists("age")) {
        safe_strcat(msg, "age ", NSTRING);
        found++;
    }
    if (command_exists("openssl")) {
        safe_strcat(msg, "openssl ", NSTRING);
        found++;
    }
    
    if (found == 0) {
        mlwrite("NO ENCRYPTION TOOLS FOUND. INSTALL GPG, AGE, OR OPENSSL.");
    } else {
        mlwrite("%s", msg);
    }
    
    return true;
}
