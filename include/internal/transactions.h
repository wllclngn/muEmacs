/* transactions.h - Atomic edit transactions interface */
#ifndef TRANSACTIONS_H_
#define TRANSACTIONS_H_

void edit_begin(void);
void edit_commit(void);
void edit_abort(void);

#endif /* TRANSACTIONS_H_ */
