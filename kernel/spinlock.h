// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
#ifdef LAB_LOCK
  int nts;   // #test-and-set
  int n;     // acquire
  int debug;  // debug for deadlock
#endif
};

