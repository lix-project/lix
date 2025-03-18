---
synopsis: "Remove some gremlins from path garbage collection"
issues: [fj#621, fj#524]
cls: [2465, 2387]
category: "Fixes"
credits: [horrors, raito]
---
Path garbage collection had some known unsoundness issues where it would delete things improperly and cause desynchronization between the filesystem state and the database state.
Now Lix tolerates better if such a condition exists by not failing the entire GC if a path fails to delete.
We also fixed a bug in our file locking implementation that is one possible root cause, but may not be every root cause.
