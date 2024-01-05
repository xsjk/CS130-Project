# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(my-test) begin
(my-test) Main thread acquired the lock
(my-test) thread2 acquired the lock
(my-test) thread2 finished.
(my-test) thread1 acquired the lock
(my-test) thread1 finished.
(my-test) thread1 and thread2 should be finished.
(my-test) Main thread finished.
(my-test) end
EOF
pass;
