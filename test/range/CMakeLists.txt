set(test_case_seq 1)
do_test(range "0..6\n1..7\n22..33\nAA { f1: 2 }..AA { f1: 200 }\nA1 { f1: 2, f2: 3.300000 }..A1 { f1: 200, f2: 44.400000 }\nT1 \\( A1 { f1: 2, f2: 3.300000 }, A1 { f1: 200, f2: 44.400000 } \\)..T1 \\( A1 { f1: 200, f2: 44.400000 }, A1 { f1: 2, f2: 3.300000 } \\)" ca range1.ca)
do_test(range "1..4..6..9\n3..6..8..20\n3..6..8..20..1..4..6..9" ca range2.ca)
do_test(range "0..=6\n1..=7\n22..=33\nAA { f1: 2 }..=AA { f1: 200 }\nA1 { f1: 2, f2: 3.300000 }..=A1 { f1: 200, f2: 44.400000 }\nT1 \\( A1 { f1: 2, f2: 3.300000 }, A1 { f1: 200, f2: 44.400000 } \\)..=T1 \\( A1 { f1: 200, f2: 44.400000 }, A1 { f1: 2, f2: 3.300000 } \\)" ca range3.ca)
do_test(range "line: 10, col: 18:\n    let d = c .. r\\;\n                 ^\nexpect type `i32..i32..i32..i32`, but found `i32..=i32..=i32..=i32`, both side of range need to be same type" ca range_error1.ca)
do_test(range "1..=4..=6..=9\n3..=6..=8..=20\n3..=6..=8..=20..1..=4..=6..=9" ca range4.ca)
do_test(range "..\n2..3\n20..=33" ca range_full.ca)
do_test(range "3..\n3..\nAA { f1: 21, f2: 1, f3: 323.220000 }..\nAA { f1: 21, f2: 1, f3: 323.220000 }..\nTT \\( AA { f1: 21, f2: 1, f3: 323.220000 }, 32, 3.200000 \\).." ca range_half1.ca)
do_test(range "..3\n..3\n..AA { f1: 21, f2: 1, f3: 323.220000 }\n..AA { f1: 21, f2: 1, f3: 323.220000 }\n..TT \\( AA { f1: 21, f2: 1, f3: 323.220000 }, 32, 3.200000 \\)" ca range_half2.ca)
do_test(range "AA { f1: 33 }....AA { f1: 33 }..\nAA { f1: 33 }....AA { f1: 33 }....AA { f1: 33 }....AA { f1: 33 }.." ca range_range1.ca)
do_test(range "3....3..\n3....3....3....3..\n..4....4\n..4....4....4....4\n..=5....=5\n..=5....=5....=5....=5" ca range_range2.ca)
do_test(range "1..5\n6..9" ca range_assign.ca)
do_test(range "line: 3, col: 13:\n    a = 1..=6\\;\n            ^\nexpected a type `i32..i32`, but found `i32..=i32`" ca range_assign_error.ca)
do_test(range "1 2 3 4 5 6 7 8 9 \n30 31 32 33 34 35 \na b c d e f g h i j k l m n o p q r s t u v w x y \nA B C D E F G H I J K L M N O P Q R S T U V W X Y Z \\[ \\\\ \\] \\^ _ ` a b c d e f g h i j k l m n o p q r s t u v w x y z" ca range_for1.ca)
do_test(range "1311768467463790320..1311768467463790335\n1311768467463790320\n1311768467463790321\n1311768467463790322\n1311768467463790323\n1311768467463790324\n1311768467463790325\n1311768467463790326\n1311768467463790327\n1311768467463790328\n1311768467463790329\n1311768467463790330\n1311768467463790331\n1311768467463790332\n1311768467463790333\n1311768467463790334" ca range_for2.ca)
do_test(range "line: 3, col: 15:\n    for \\(i in a\\) {\n              \\^\ntype `bool` not support step into next yet" ca range_for_error1.ca)
do_test(range "line: 6, col: 15:\n    for \\(i in a\\) {\n              \\^\ntype `\\(AA\\;i32,i32\\)` not support step into next yet" ca range_for_error2.ca)
do_test(todo-range "good" ca range_array1.ca)
do_test(todo-range "support fetch range element operator" ca range_array2.ca)

