[ "${input1: -2}" = /. ]
[ "${input2: -2}" = /. ]

mkdir $out
echo $(cat $input1/foo)$(cat $input2/bar) > $out/foobar

ln -s $input2 $out/reference-to-input-2
ln -s $input3 $out/reference-to-input-3

# Self-reference.
ln -s $out $out/self

# Executable.
echo program > $out/program
chmod +x $out/program

echo FOO
