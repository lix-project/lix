let
  f1 = x: x;
  f2 = x: x;

  inf = let f = n: if n == 0 then 1.0 else 10 * f (n - 1); in f 1000;
  nan = inf * 0;

  # all of these sets and lists are assumed to have different *addresses* as long as CSE does not occur
  sets = rec {
    set1 = {
      f = f1;
    };
    set1' = {
      f = f2;
    };
    set2 = {
      f = f1;
    };
    set2' = {
      f = f2;
    };
    bigSet1 = {
      f = f1;
      meow = true;
    };
    bigSet1' = {
      f = f2;
      meow = true;
    };
    bigSet2 = bigSet1 // {
      meow = true;
    };
    bigSet2' = bigSet1' // {
      meow = true;
    };
    bigSet3 = {
      f = f1;
      meow = true;
    };
    bigSet3' = {
      f = f2;
      meow = true;
    };
  };

  lists = rec {
    list1 = [ f1 ];
    list1' = [ f2 ];
    list2 = [ f1 ];
    list2' = [ f2 ];
    bigList1 = [ f1 true ];
    bigList1' = [ f2 true ];
    bigList2 = map (e: if builtins.isBool e then true else e) bigList1;
    bigList2'= map (e: if builtins.isBool e then true else e) bigList1';
    bigList3 = [ f1 true ];
    bigList3' = [ f2 true ];
  };

  nans = rec {
    plain = nan;
    list = [ nan ];
    set = { inherit nan; };
  };

  compareAllPairs = cases:
    let
      names = builtins.attrNames cases;
    in
      builtins.listToAttrs (map
        (n1: {
          name = n1;
          value = builtins.listToAttrs (map
            (n2: { name = n2; value = cases.${n1} == cases.${n2}; })
            names);
        })
        names);

  results = {
    functionsDirect = [
      (f1 == f1)
      (f1 == f2)
      (f2 == f1)
      (f2 == f2)
    ];
    functionsSelected = [
      (sets.set1.f == sets.set1.f)
      (sets.set1.f == sets.set1'.f)
      (sets.set1'.f == sets.set1.f)
      (sets.set1'.f == sets.set1'.f)
    ];
    sets = compareAllPairs sets;
    lists = compareAllPairs lists;
    nans = compareAllPairs nans;
  };
in
builtins.deepSeq results results
