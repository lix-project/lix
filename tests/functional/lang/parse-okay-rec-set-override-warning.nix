[
  # Should warn
  { a = rec {}; a.__overrides = {}; }
  rec { __overrides = {}; }
  # Should not warn: Not recursive
  { __overrides = {}; }
  # Should not warn: Dynamic
  rec { ${"__overrides" + ""} = {}; }
]
