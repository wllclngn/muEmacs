/*
 * encrypt.c - Modern encryption integration via external tools
 * 
 * Shell out to gpg/age/openssl for proper cryptography
 * No homebrew crypto - use battle-tested system tools
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "string_utils.h"

/* Check if a command exists in PATH */
static int command_exists(const char *cmd) {
    char test_cmd[256];
    snprintf(test_cmd, sizeof(test_cmd), "command -v %s >/dev/null 2>&1", cmd);
    return system(test_cmd) == 0;
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
    char outfile[NFILEN];
    char cmd[NSTRING];
    int status;
    
    if (curbp->b_mode & MDVIEW)
        return rdonly();
    
    if (!command_exists("gpg")) {
        mlwrite("GPG not found. Install gnupg package.");
        return false;
    }
    
    /* Save current buffer first */
    if (filesave(false, 0) != true) {
        mlwrite("Failed to save buffer before encryption");
        return false;
    }
    
    /* Generate output filename */
    if (strlen(curbp->b_fname) > NFILEN - 5) {
        mlwrite("Filename too long for .gpg suffix");
        return false;
    }
    snprintf(outfile, sizeof(outfile), "%s.gpg", curbp->b_fname);
    
    /* Encrypt with gpg symmetric encryption */
    snprintf(cmd, sizeof(cmd), "gpg --symmetric --cipher-algo AES256 -o %s %s",
             outfile, curbp->b_fname);
    
    mlwrite("Encrypting with GPG (you'll be prompted for password)...");
    TTflush();
    TTclose();
    TTkclose();
    
    status = system(cmd);
    
    TTopen();
    TTkopen();
    sgarbf = true;
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        mlwrite("Encrypted: %s", outfile);
        return true;
    } else {
        mlwrite("GPG encryption failed");
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
    status = mlreply("Decrypt file: ", fname, NFILEN);
    if (status != true)
        return status;
    
    if (!command_exists("gpg")) {
        mlwrite("GPG not found. Install gnupg package.");
        return false;
    }
    
    /* Create secure temp file for decrypted content */
    const char *tmpdir = getenv("XDG_RUNTIME_DIR");
    if (!tmpdir || !tmpdir[0]) tmpdir = "/tmp";
    
    snprintf(tmpfile, sizeof(tmpfile), "%s/uemacs_decrypt_XXXXXX", tmpdir);
    int fd = mkstemp(tmpfile);
    if (fd < 0) {
        mlwrite("Failed to create temp file");
        return false;
    }
    close(fd);
    
    /* Decrypt with gpg */
    snprintf(cmd, sizeof(cmd), "gpg --decrypt -o %s %s 2>/dev/null", tmpfile, fname);
    
    mlwrite("Decrypting with GPG...");
    TTflush();
    TTclose();
    TTkclose();
    
    status = system(cmd);
    
    TTopen();
    TTkopen();
    sgarbf = true;
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        /* Open the decrypted temp file */
        status = getfile(tmpfile, true);
        
        /* Set buffer name to original encrypted filename */
        safe_strcpy(curbp->b_fname, fname, NFILEN);
        
        /* Delete temp file */
        unlink(tmpfile);
        
        if (status == true) {
            mlwrite("Decrypted: %s (WARNING: save will overwrite encrypted file!)", fname);
            curbp->b_flag |= BFCHG;  /* Mark as changed */
        }
        return status;
    } else {
        unlink(tmpfile);
        mlwrite("GPG decryption failed (wrong password?)");
        return false;
    }
}

/*
 * Encrypt current buffer with age (modern alternative)
 * M-x encrypt-buffer-age
 */
int encrypt_buffer_age(int f, int n) {
    char outfile[NFILEN];
    char cmd[NSTRING];
    int status;
    
    if (curbp->b_mode & MDVIEW)
        return rdonly();
    
    if (!command_exists("age")) {
        mlwrite("age not found. Install with: cargo install age or your package manager.");
        return false;
    }
    
    if (filesave(false, 0) != true) {
        mlwrite("Failed to save buffer before encryption");
        return false;
    }
    
    if (strlen(curbp->b_fname) > NFILEN - 5) {
        mlwrite("Filename too long for .age suffix");
        return false;
    }
    snprintf(outfile, sizeof(outfile), "%s.age", curbp->b_fname);
    snprintf(cmd, sizeof(cmd), "age -p -o %s %s", outfile, curbp->b_fname);
    
    mlwrite("Encrypting with age (you'll be prompted for passphrase)...");
    TTflush();
    TTclose();
    TTkclose();
    
    status = system(cmd);
    
    TTopen();
    TTkopen();
    sgarbf = true;
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        mlwrite("Encrypted: %s", outfile);
        return true;
    } else {
        mlwrite("age encryption failed");
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
    
    status = mlreply("Decrypt age file: ", fname, NFILEN);
    if (status != true)
        return status;
    
    if (!command_exists("age")) {
        mlwrite("age not found. Install with: cargo install age or your package manager.");
        return false;
    }
    
    const char *tmpdir = getenv("XDG_RUNTIME_DIR");
    if (!tmpdir || !tmpdir[0]) tmpdir = "/tmp";
    
    snprintf(tmpfile, sizeof(tmpfile), "%s/uemacs_decrypt_XXXXXX", tmpdir);
    int fd = mkstemp(tmpfile);
    if (fd < 0) {
        mlwrite("Failed to create temp file");
        return false;
    }
    close(fd);
    
    snprintf(cmd, sizeof(cmd), "age -d -o %s %s 2>/dev/null", tmpfile, fname);
    
    mlwrite("Decrypting with age...");
    TTflush();
    TTclose();
    TTkclose();
    
    status = system(cmd);
    
    TTopen();
    TTkopen();
    sgarbf = true;
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        status = getfile(tmpfile, true);
        safe_strcpy(curbp->b_fname, fname, NFILEN);
        unlink(tmpfile);
        
        if (status == true) {
            mlwrite("Decrypted: %s", fname);
            curbp->b_flag |= BFCHG;
        }
        return status;
    } else {
        unlink(tmpfile);
        mlwrite("age decryption failed");
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
        mlwrite("No encryption tool found. Install gpg, age, or openssl.");
        return false;
    }
    
    if (strcmp(tool, "gpg") == 0) {
        return encrypt_buffer_gpg(f, n);
    } else if (strcmp(tool, "age") == 0) {
        return encrypt_buffer_age(f, n);
    }
    
    mlwrite("Encryption tool detection failed");
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
        mlwrite("No encryption tools found. Install gpg, age, or openssl.");
    } else {
        mlwrite("%s", msg);
    }
    
    return true;
}
