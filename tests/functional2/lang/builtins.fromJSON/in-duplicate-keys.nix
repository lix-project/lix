# Duplicate JSON keys should always drop all but the latest value
builtins.fromJSON ''
  {
    "key": "one",
    "key": "two",
    "key": "three"
  }
''
