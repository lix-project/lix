with builtins;

rec {

  fold = op: nul: list:
    if list == []
    then nul
    else op (head list) (fold op nul (tail list));

  and = all id;

  sum = foldl' (x: y: add x y) 0;

  hasSuffix = ext: fileName:
    let lenFileName = stringLength fileName;
        lenExt = stringLength ext;
    in !(lessThan lenFileName lenExt) &&
       substring (sub lenFileName lenExt) lenFileName fileName == ext;  id = x: x;

  const = x: y: x;

  range = first: last:
    if first > last
      then []
      else genList (n: first + n) (last - first + 1);

}
