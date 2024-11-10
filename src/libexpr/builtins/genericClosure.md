---
name: genericClosure
args: [attrset]
---
Take an *attrset* with values named `startSet` and `operator` in order to
return a *list of attrsets* by starting with the `startSet` and recursively
applying the `operator` function to each `item`. The *attrsets* in the
`startSet` and the *attrsets* produced by `operator` must contain a value
named `key` which is comparable. The result is produced by calling `operator`
for each `item` with a value for `key` that has not been called yet including
newly produced `item`s. The function terminates when no new `item`s are
produced. The resulting *list of attrsets* contains only *attrsets* with a
unique key. For example,

```
builtins.genericClosure {
  startSet = [ {key = 5;} ];
  operator = item: [{
    key = if (item.key / 2 ) * 2 == item.key
         then item.key / 2
         else 3 * item.key + 1;
  }];
}
```
evaluates to
```
[ { key = 5; } { key = 16; } { key = 8; } { key = 4; } { key = 2; } { key = 1; } ]
```
