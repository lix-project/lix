# Exactly two function calls deep, so should fail with maxCallDepth < 2
let f = x: x + 1; in f (f 0)
