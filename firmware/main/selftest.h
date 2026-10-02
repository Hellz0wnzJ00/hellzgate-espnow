// same checks the host tests run, but on the real chip
// packing and crc can behave differently on riscv than on the pc so it is worth
// proving them where the code actually ships

#ifndef SELFTEST_H
#define SELFTEST_H

// returns how many checks failed, zero means everything passed
int selftest_run(void);

#endif
