---
synopsis: "Use a lock when fetching inputs"
issues: [1122]
cls: [5438]
category: "Fixes"
credits: [lheckemann]
---

Up to now, attempting to fetch the same git input from multiple processes
concurrently when the input is not yet cached presented multiple issues:

- If the input was not already present, it would unnecessarily be fetched
  multiple times;

- Access to the fetcher cache database was contentious, and could lead to
  evaluation or flake locking failing unnecessary because the fetcher cache
  was locked.

We now acquire a lock on a path based on a hash of the input specification
before accessing the fetcher db, reducing contention significantly, and
preventing more than one process from fetching the same path at the same time.
