#ifndef TEST_TRANSACTION_PERSISTENCE_H
#define TEST_TRANSACTION_PERSISTENCE_H

// Transaction and persistence system test functions
int test_transaction_atomicity(void);
int test_multi_step_operations(void);
int test_crash_recovery(void);
int test_undo_persistence(void);
int test_buffer_state_persistence(void);
int test_concurrent_transactions(void);
int test_transaction_rollback(void);

#endif // TEST_TRANSACTION_PERSISTENCE_H