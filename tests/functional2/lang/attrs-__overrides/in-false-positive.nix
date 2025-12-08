# This one shouldn't warn, because the `__overrides` is not within a rec set
rec { a.__overrides.a = 1; }
