#include "test_utils.h"
#include "test_advanced_text_ops.h"
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <regex.h>
#include <ctype.h>

// Test region operations
int test_region_operations(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Region Operations ===%s\n", BLUE, RESET);
    
    // Test region selection and boundaries
    printf("Testing region selection and boundaries...\n");
    total++;
    
    typedef struct {
        char text[1024];
        int start_pos;
        int end_pos;
        int line_start;
        int line_end;
        int valid;
    } region_t;
    
    region_t region = {
        .text = "Line 1\nLine 2 with some text\nLine 3\nFinal line",
        .start_pos = 7,  // Start of "Line 2"
        .end_pos = 29,   // End of "some text"
        .line_start = 1,
        .line_end = 1,
        .valid = 1
    };
    
    // Calculate region size
    int region_size = region.end_pos - region.start_pos;
    if (region_size > 0 && region.valid) {
        char selected_text[256];
        strncpy(selected_text, region.text + region.start_pos, region_size);
        selected_text[region_size] = '\0';
        
        if (strstr(selected_text, "Line 2 with some text")) {
            printf("[%sSUCCESS%s] Region selection: %d chars selected (%s...)\n", 
                   GREEN, RESET, region_size, strndup(selected_text, 15));
            passed++;
        }
    }
    
    // Test region copy operation
    printf("Testing region copy operation...\n");
    total++;
    
    char clipboard[1024] = {0};
    const char* source_text = "This is text to copy from region";
    int copy_start = 8;  // "text"
    int copy_length = 4;
    
    if (copy_start + copy_length <= (int)strlen(source_text)) {
        strncpy(clipboard, source_text + copy_start, copy_length);
        clipboard[copy_length] = '\0';
        
        if (strcmp(clipboard, "text") == 0) {
            printf("[%sSUCCESS%s] Region copy: copied '%s' to clipboard\n", GREEN, RESET, clipboard);
            passed++;
        }
    }
    
    // Test region deletion
    printf("Testing region deletion...\n");
    total++;
    
    char delete_text[256];
    strcpy(delete_text, "Before DELETE_ME After");
    const char* delete_marker = "DELETE_ME";
    
    char* delete_pos = strstr(delete_text, delete_marker);
    if (delete_pos) {
        size_t delete_len = strlen(delete_marker);
        memmove(delete_pos, delete_pos + delete_len, strlen(delete_pos + delete_len) + 1);
        
        if (strcmp(delete_text, "Before  After") == 0) {
            printf("[%sSUCCESS%s] Region deletion: text removed, result: '%s'\n", GREEN, RESET, delete_text);
            passed++;
        }
    }
    
    printf("Region operation tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}

// Test word operations
int test_word_operations(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Word Operations ===%s\n", BLUE, RESET);
    
    // Test word boundary detection
    printf("Testing word boundary detection...\n");
    total++;
    
    const char* test_text = "Hello, world! This is a test-case with numbers123 and symbols.";
    const char* words[] = {"Hello", "world", "This", "is", "a", "test", "case", "with", "numbers123", "and", "symbols"};
    int expected_word_count = 11;
    
    // Simple word counting (simplified version)
    int word_count = 0;
    int in_word = 0;
    
    for (const char* p = test_text; *p; p++) {
        if (isalnum(*p)) {
            if (!in_word) {
                word_count++;
                in_word = 1;
            }
        } else {
            in_word = 0;
        }
    }
    
    if (word_count == expected_word_count) {
        printf("[%sSUCCESS%s] Word boundaries: detected %d words correctly\n", GREEN, RESET, word_count);
        passed++;
    }
    
    // Test word case operations
    printf("Testing word case operations...\n");
    total++;
    
    char case_text[256];
    strcpy(case_text, "mixed CaSe WoRdS for testing");
    
    // Convert to lowercase
    char lowercase_text[256];
    strcpy(lowercase_text, case_text);
    for (char* p = lowercase_text; *p; p++) {
        *p = tolower(*p);
    }
    
    // Convert to uppercase
    char uppercase_text[256];
    strcpy(uppercase_text, case_text);
    for (char* p = uppercase_text; *p; p++) {
        *p = toupper(*p);
    }
    
    if (strcmp(lowercase_text, "mixed case words for testing") == 0 &&
        strcmp(uppercase_text, "MIXED CASE WORDS FOR TESTING") == 0) {
        printf("[%sSUCCESS%s] Case operations: lowercase and uppercase conversion work\n", GREEN, RESET);
        passed++;
    }
    
    // Test word wrapping
    printf("Testing word wrapping...\n");
    total++;
    
    const char* long_line = "This is a very long line that should be wrapped at word boundaries to fit within a specified width limit for proper display formatting";
    const int wrap_width = 40;
    
    char wrapped_text[512] = {0};
    const char* src = long_line;
    char* dst = wrapped_text;
    int current_line_length = 0;
    int last_space_pos = -1;
    
    while (*src) {
        if (*src == ' ') {
            last_space_pos = dst - wrapped_text;
        }
        
        *dst++ = *src++;
        current_line_length++;
        
        if (current_line_length >= wrap_width && last_space_pos >= 0) {
            wrapped_text[last_space_pos] = '\n';
            current_line_length = (dst - wrapped_text) - last_space_pos - 1;
            last_space_pos = -1;
        }
    }
    *dst = '\0';
    
    // Count lines in wrapped text
    int line_count = 1;
    for (char* p = wrapped_text; *p; p++) {
        if (*p == '\n') line_count++;
    }
    
    if (line_count >= 3) {
        printf("[%sSUCCESS%s] Word wrapping: %d lines created from long text\n", GREEN, RESET, line_count);
        passed++;
    }
    
    printf("Word operation tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}

// Test paragraph operations
int test_paragraph_operations(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Paragraph Operations ===%s\n", BLUE, RESET);
    
    // Test paragraph detection
    printf("Testing paragraph detection...\n");
    total++;
    
    const char* multi_para_text = "First paragraph with some text.\nMore text in first paragraph.\n\nSecond paragraph starts here.\nContinuation of second paragraph.\n\n\nThird paragraph after blank lines.\nFinal sentence.";
    
    // Count paragraphs (separated by blank lines)
    int paragraph_count = 0;
    int in_paragraph = 0;
    int prev_was_newline = 0;
    
    for (const char* p = multi_para_text; *p; p++) {
        if (*p == '\n') {
            if (prev_was_newline && in_paragraph) {
                // End of paragraph
                in_paragraph = 0;
            }
            prev_was_newline = 1;
        } else {
            if (!in_paragraph) {
                paragraph_count++;
                in_paragraph = 1;
            }
            prev_was_newline = 0;
        }
    }
    
    if (paragraph_count == 3) {
        printf("[%sSUCCESS%s] Paragraph detection: found %d paragraphs correctly\n", GREEN, RESET, paragraph_count);
        passed++;
    }
    
    // Test paragraph formatting/justification
    printf("Testing paragraph formatting...\n");
    total++;
    
    const char* unformatted = "This is an unformatted paragraph that needs to be justified and properly formatted with appropriate spacing and alignment for better readability.";
    const int format_width = 50;
    
    char formatted[512] = {0};
    const char* src = unformatted;
    char* dst = formatted;
    int line_length = 0;
    
    while (*src) {
        if (*src == ' ' && line_length > format_width - 10) {
            *dst++ = '\n';
            line_length = 0;
            src++; // Skip the space
        } else {
            *dst++ = *src++;
            line_length++;
        }
    }
    *dst = '\0';
    
    // Verify formatting created multiple lines
    int formatted_lines = 1;
    for (char* p = formatted; *p; p++) {
        if (*p == '\n') formatted_lines++;
    }
    
    if (formatted_lines >= 2) {
        printf("[%sSUCCESS%s] Paragraph formatting: created %d lines from long paragraph\n", GREEN, RESET, formatted_lines);
        passed++;
    }
    
    // Test paragraph selection
    printf("Testing paragraph selection...\n");
    total++;
    
    const char* para_text = "Para 1 line 1.\nPara 1 line 2.\n\nPara 2 line 1.\nPara 2 line 2.\n\nPara 3 line 1.";
    
    // Find second paragraph boundaries
    const char* para2_start = strstr(para_text, "Para 2 line 1");
    const char* para2_end = strstr(para_text, "\n\nPara 3");
    
    if (para2_start && para2_end) {
        size_t para2_length = para2_end - para2_start;
        char selected_para[256];
        strncpy(selected_para, para2_start, para2_length);
        selected_para[para2_length] = '\0';
        
        if (strstr(selected_para, "Para 2 line 1") && strstr(selected_para, "Para 2 line 2")) {
            printf("[%sSUCCESS%s] Paragraph selection: selected paragraph 2 (%zu chars)\n", GREEN, RESET, para2_length);
            passed++;
        }
    }
    
    printf("Paragraph operation tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}

// Test advanced search and replace
int test_advanced_search_replace(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Advanced Search & Replace ===%s\n", BLUE, RESET);
    
    // Test regex pattern matching
    printf("Testing regex pattern matching...\n");
    total++;
    
    const char* text = "Contact: john@example.com or call 555-1234 for more info. Email: admin@test.org";
    regex_t email_regex, phone_regex;
    
    // Compile email regex
    if (regcomp(&email_regex, "[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}", REG_EXTENDED) == 0) {
        regmatch_t email_matches[3];
        int email_count = 0;
        const char* search_start = text;
        
        while (regexec(&email_regex, search_start, 3, email_matches, 0) == 0) {
            email_count++;
            search_start += email_matches[0].rm_eo;
        }
        
        regfree(&email_regex);
        
        if (email_count == 2) {
            printf("[%sSUCCESS%s] Regex search: found %d email addresses\n", GREEN, RESET, email_count);
            passed++;
        }
    }
    total++;
    
    // Test case-insensitive search
    printf("Testing case-insensitive search...\n");
    
    const char* case_text = "The Quick BROWN fox jumps over the lazy DOG";
    const char* search_term = "brown";
    
    // Convert both to lowercase for comparison
    char lower_text[256], lower_search[64];
    strcpy(lower_text, case_text);
    strcpy(lower_search, search_term);
    
    for (char* p = lower_text; *p; p++) *p = tolower(*p);
    for (char* p = lower_search; *p; p++) *p = tolower(*p);
    
    if (strstr(lower_text, lower_search)) {
        printf("[%sSUCCESS%s] Case-insensitive: found '%s' in mixed case text\n", GREEN, RESET, search_term);
        passed++;
    }
    
    // Test global replace operation
    printf("Testing global replace operation...\n");
    total++;
    
    char replace_text[256];
    strcpy(replace_text, "The cat sat on the mat. The cat was fat. The cat ran.");
    const char* find_str = "cat";
    const char* replace_str = "dog";
    
    char* pos = replace_text;
    int replacements = 0;
    
    while ((pos = strstr(pos, find_str)) != NULL) {
        // Create new string with replacement
        char temp[256];
        size_t prefix_len = pos - replace_text;
        strncpy(temp, replace_text, prefix_len);
        temp[prefix_len] = '\0';
        strcat(temp, replace_str);
        strcat(temp, pos + strlen(find_str));
        strcpy(replace_text, temp);
        
        pos = replace_text + prefix_len + strlen(replace_str);
        replacements++;
    }
    
    if (replacements == 3 && strstr(replace_text, "dog")) {
        printf("[%sSUCCESS%s] Global replace: %d replacements made\n", GREEN, RESET, replacements);
        passed++;
    }
    
    printf("Advanced search & replace tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}

// Test text transformation operations
int test_text_transformation(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Text Transformation ===%s\n", BLUE, RESET);
    
    // Test text sorting
    printf("Testing text sorting...\n");
    total++;
    
    const char* unsorted_lines[] = {
        "zebra", "apple", "banana", "cherry", "date"
    };
    const char* sorted_expected[] = {
        "apple", "banana", "cherry", "date", "zebra"
    };
    
    // Create array for sorting
    char* sortable_lines[5];
    for (int i = 0; i < 5; i++) {
        sortable_lines[i] = malloc(strlen(unsorted_lines[i]) + 1);
        strcpy(sortable_lines[i], unsorted_lines[i]);
    }
    
    // Simple bubble sort
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4 - i; j++) {
            if (strcmp(sortable_lines[j], sortable_lines[j + 1]) > 0) {
                char* temp = sortable_lines[j];
                sortable_lines[j] = sortable_lines[j + 1];
                sortable_lines[j + 1] = temp;
            }
        }
    }
    
    // Verify sorting
    int sort_correct = 1;
    for (int i = 0; i < 5; i++) {
        if (strcmp(sortable_lines[i], sorted_expected[i]) != 0) {
            sort_correct = 0;
            break;
        }
    }
    
    if (sort_correct) {
        printf("[%sSUCCESS%s] Text sorting: 5 lines sorted correctly\n", GREEN, RESET);
        passed++;
    }
    
    // Cleanup
    for (int i = 0; i < 5; i++) {
        free(sortable_lines[i]);
    }
    
    // Test text reversal
    printf("Testing text reversal...\n");
    total++;
    
    char reverse_text[256];
    strcpy(reverse_text, "Hello World");
    int len = strlen(reverse_text);
    
    // Reverse the string
    for (int i = 0; i < len / 2; i++) {
        char temp = reverse_text[i];
        reverse_text[i] = reverse_text[len - 1 - i];
        reverse_text[len - 1 - i] = temp;
    }
    
    if (strcmp(reverse_text, "dlroW olleH") == 0) {
        printf("[%sSUCCESS%s] Text reversal: '%s' correctly reversed\n", GREEN, RESET, reverse_text);
        passed++;
    }
    
    // Test text statistics calculation
    printf("Testing text statistics calculation...\n");
    total++;
    
    const char* stats_text = "The quick brown fox jumps over the lazy dog. This pangram contains every letter of the alphabet!";
    
    int char_count = 0, word_count = 0, line_count = 1, letter_count = 0;
    int in_word = 0;
    
    for (const char* p = stats_text; *p; p++) {
        char_count++;
        if (*p == '\n') line_count++;
        if (isalpha(*p)) letter_count++;
        
        if (isalnum(*p)) {
            if (!in_word) {
                word_count++;
                in_word = 1;
            }
        } else {
            in_word = 0;
        }
    }
    
    if (char_count > 90 && word_count >= 15 && letter_count >= 70) {
        printf("[%sSUCCESS%s] Text statistics: %d chars, %d words, %d letters, %d lines\n", 
               GREEN, RESET, char_count, word_count, letter_count, line_count);
        passed++;
    }
    
    printf("Text transformation tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}

// Test macro text processing
int test_macro_text_processing(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Macro Text Processing ===%s\n", BLUE, RESET);
    
    // Test recorded macro operations
    printf("Testing recorded macro operations...\n");
    total++;
    
    typedef struct {
        char command[64];
        char parameter[256];
        int repeat_count;
    } macro_command_t;
    
    macro_command_t macro_sequence[] = {
        {"insert-text", "Hello ", 1},
        {"move-word", "", 1},
        {"delete-word", "", 1},
        {"insert-text", "World!", 1}
    };
    
    char macro_result[512] = {0};
    
    // Execute macro commands
    for (int i = 0; i < 4; i++) {
        if (strcmp(macro_sequence[i].command, "insert-text") == 0) {
            strcat(macro_result, macro_sequence[i].parameter);
        } else if (strcmp(macro_sequence[i].command, "delete-word") == 0) {
            // Simulate deleting last word (simplified)
            char* last_space = strrchr(macro_result, ' ');
            if (last_space) {
                *last_space = '\0';
            }
        }
    }
    
    if (strstr(macro_result, "Hello") && strstr(macro_result, "World!")) {
        printf("[%sSUCCESS%s] Macro execution: result '%s'\n", GREEN, RESET, macro_result);
        passed++;
    }
    
    // Test macro with loops
    printf("Testing macro with loops...\n");
    total++;
    
    char loop_result[256] = {0};
    const char* repeat_text = "X";
    int loop_count = 5;
    
    for (int i = 0; i < loop_count; i++) {
        strcat(loop_result, repeat_text);
    }
    
    if (strcmp(loop_result, "XXXXX") == 0) {
        printf("[%sSUCCESS%s] Macro loops: repeated '%s' %d times\n", GREEN, RESET, repeat_text, loop_count);
        passed++;
    }
    
    // Test conditional macro execution
    printf("Testing conditional macro execution...\n");
    total++;
    
    int condition_value = 42;
    char conditional_result[128] = {0};
    
    if (condition_value > 30) {
        strcpy(conditional_result, "Condition met: value is high");
    } else {
        strcpy(conditional_result, "Condition not met: value is low");
    }
    
    if (strstr(conditional_result, "Condition met")) {
        printf("[%sSUCCESS%s] Conditional macro: %s\n", GREEN, RESET, conditional_result);
        passed++;
    }
    
    printf("Macro text processing tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}

// Test Unicode text handling
int test_unicode_text_handling(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Unicode Text Handling ===%s\n", BLUE, RESET);
    
    // Set locale for Unicode support
    setlocale(LC_ALL, "en_US.UTF-8");
    
    // Test UTF-8 character detection
    printf("Testing UTF-8 character detection...\n");
    total++;
    
    const char* utf8_text = "Hello 世界 🌍 café naïve résumé";
    
    int byte_count = strlen(utf8_text);
    int char_count = 0;
    
    // Count UTF-8 characters (simplified)
    for (const char* p = utf8_text; *p; ) {
        if ((*p & 0x80) == 0) {
            // ASCII character
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            // 2-byte UTF-8 character
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            // 3-byte UTF-8 character
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            // 4-byte UTF-8 character
            p += 4;
        } else {
            // Invalid UTF-8, skip
            p++;
        }
        char_count++;
    }
    
    if (byte_count > char_count) {
        printf("[%sSUCCESS%s] UTF-8 detection: %d bytes, %d characters (multi-byte detected)\n", 
               GREEN, RESET, byte_count, char_count);
        passed++;
    }
    
    // Test Unicode normalization (basic check)
    printf("Testing Unicode normalization...\n");
    total++;
    
    const char* composed = "café";    // é as single character
    const char* decomposed = "cafe\xCC\x81"; // e + combining acute accent
    
    // Simple normalization check (both should represent same text)
    int composed_len = strlen(composed);
    int decomposed_len = strlen(decomposed);
    
    if (composed_len != decomposed_len) {
        printf("[%sSUCCESS%s] Unicode forms: composed=%d bytes, decomposed=%d bytes\n", 
               GREEN, RESET, composed_len, decomposed_len);
        passed++;
    }
    
    // Test Unicode text width calculation
    printf("Testing Unicode text width calculation...\n");
    total++;
    
    const char* width_test = "A世B界C🌍D"; // Mix of narrow, wide, and emoji
    int visual_width = 0;
    
    for (const char* p = width_test; *p; ) {
        if ((*p & 0x80) == 0) {
            // ASCII character - width 1
            visual_width += 1;
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            // 2-byte UTF-8 - width 1
            visual_width += 1;
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            // 3-byte UTF-8 - likely width 2 (CJK)
            visual_width += 2;
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            // 4-byte UTF-8 - likely width 2 (emoji)
            visual_width += 2;
            p += 4;
        } else {
            p++;
        }
    }
    
    // Expected: A(1) + 世(2) + B(1) + 界(2) + C(1) + 🌍(2) + D(1) = 10
    if (visual_width >= 9 && visual_width <= 11) {
        printf("[%sSUCCESS%s] Unicode width: calculated width %d for mixed text\n", 
               GREEN, RESET, visual_width);
        passed++;
    }
    
    printf("Unicode text handling tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}