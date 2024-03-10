source common.sh

# You might think that this can trivially be moved into a makefile, however,
# there is various environmental initialization of this shell framework that
# seems load bearing and seemingly prevents the tests working inside the
# builder.
_NIX_TEST_UNIT_DATA=$(pwd)/repl_characterization/data ./repl_characterization/test-repl-characterization
