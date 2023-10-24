# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(my-test-big) begin
(my-test-big) thread32: got lock e
(my-test-big) thread32: got lock a
(my-test-big) thread32: got lock b
(my-test-big) thread35: got lock b
(my-test-big) thread35: done
(my-test-big) thread34: got lock a
(my-test-big) thread34: done
(my-test-big) thread33: got lock c
(my-test-big) thread33: done
(my-test-big) thread32: done
(my-test-big) This should be the last line before finishing this test.
(my-test-big) end
EOF
pass;
