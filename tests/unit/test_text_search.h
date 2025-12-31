// test_text_search.h - Unit tests for search functionality
#ifndef TEST_TEXT_SEARCH_H
#define TEST_TEXT_SEARCH_H

int test_text_search(void);

// Individual test functions (for backward compatibility)
int test_bmh_threshold_switching(void);
int test_nfa_edge_cases(void);
int test_cross_line_search(void);
int test_search_performance(void);
int test_case_insensitive_search(void);
int test_naive_reverse_search(void);

#endif // TEST_TEXT_SEARCH_H
