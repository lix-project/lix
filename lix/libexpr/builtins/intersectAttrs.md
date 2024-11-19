---
name: intersectAttrs
args: [e1, e2]
---
Return a set consisting of the attributes in the set *e2* which have the
same name as some attribute in *e1*.

Performs in O(*n* log *m*) where *n* is the size of the smaller set and *m* the larger set's size.
