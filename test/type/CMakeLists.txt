set(test_case_seq 1)
do_test(type "good" ca array1.ca)
do_test(type "line: 44, col: 22:.*type `A5` defined multiple times" ca array2.ca)
# don't know why the test cannot detect following string
#do_test(type "i64\n[i32; 4]\n[[i32; 4]; 3]\n[[[i32; 4]; 3]; 5]\n[[[[i32; 4]; 3]; 5]; 5]\n[[[[[i32; 4]; 3]; 5]; 5]; 3]\n[[[[[[i32; 4]; 3]; 5]; 5]; 3]; 5]\n[[[[[[[i32; 4]; 3]; 5]; 5]; 3]; 5]; 5]\n[[i32; 4]; 3]\n[[[i32; 4]; 3]; 5]\n[[[[i32; 4]; 3]; 5]; 5]\n[[[[[i32; 4]; 3]; 5]; 5]; 3]\n[[[[[[i32; 4]; 3]; 5]; 5]; 3]; 5]\n[[[[[[[i32; 4]; 3]; 5]; 5]; 3]; 5]; 5]\ni32\n[i32; 4]\n[[i32; 4]; 3]\n[[[i32; 4]; 3]; 5]\n[[[[i32; 4]; 3]; 5]; 5]\n[[[[[i32; 4]; 3]; 5]; 5]; 3]\n[[[[[[i32; 4]; 3]; 5]; 5]; 3]; 5]\n[[[[[[[i32; 4]; 3]; 5]; 5]; 3]; 5]; 5]\n[[i32; 4]; 3]\n[[[i32; 4]; 3]; 5]\n[[[[i32; 4]; 3]; 5]; 5]\n[[[[[i32; 4]; 3]; 5]; 5]; 3]\n[[[[[[i32; 4]; 3]; 5]; 5]; 3]; 5]\n[[[[[[[i32; 4]; 3]; 5]; 5]; 3]; 5]; 5]\n" ca dbgarray3.ca)
do_test(type "good" ca dbgarray_pointer1.ca)
do_test(type "good" ca dbgpointer3.ca)
do_test(type "size = 0, type: t:\\{AA\\}\nsize = 8, type: t:\\*\\{AA\\}" ca dbgstruct3.ca)
do_test(type "good" ca dbgstruct_use2.ca)
do_test(type "good" ca dbgstruct_use3.ca)
do_test(type "type `R1` should recursive defining" ca dbgall_r1.ca)
do_test(type "line: 4, col: 14:.*expected `t:i32`, found `t:f32`" ca diff_type.ca)
do_test(type "101" ca duptypevar.ca)
do_test(type "line: 4, col: 14:.*expected `t:i32`, found `t:i64`" ca exprtype1.ca)
do_test(type "line: 5, col: 14:.*expected `t:i32`, found `t:i64`" ca exprtype2.ca)
do_test(type "line: 4, col: 16:.*`f64` type required, but found `i64`" ca exprtype3.ca)
do_test(type "good" ca exprtype4.ca)
do_test(type "line: 4, col: 14:.*expected `t:i32`, found `t:f32`" ca exprtype5.ca)
do_test(type "line: 4, col: 14:.*expected `t:i32`, found `t:i64`" ca exprtype6.ca)
do_test(type "202" ca extern_fn_post.ca)
do_test(type "good" ca fndecl1.ca)
do_test(type "good" ca fndecl2.ca)
do_test(type "good" ca pointer1.ca)
do_test(type "good" ca pointer2.ca)
do_test(type "good" ca pointer3.ca)
do_test(type "8" ca pointer_extend.ca)
do_test(type "f\n102\nB\n66\nC\n67" ca postfndecl1.ca)
do_test(type "fB66C67" ca postfndecl.ca)
do_test(type "102103104" ca postfunc1.ca)
do_test(type "102103104" ca postfunc2b.ca)
do_test(type "102103104" ca postfunc2.ca)
do_test(type "102103104" ca postfunc3.ca)
do_test(type "206" ca postsimple1.ca)
do_test(type "202" ca postsimple2.ca)
do_test(type "202" ca postsimple3.ca)
do_test(type "102" ca posttype1.ca)
do_test(type "102" ca posttype2.ca)
do_test(type "102103" ca posttype3.ca)
do_test(type "102103104" ca posttype4.ca)
do_test(type "good" ca returntype.ca)
do_test(type "line: 2, col: 9:.*return value `0` type 't:i32' not match function type 't:void'" ca returntype2.ca)
do_test(type "good" ca struct1.ca)
do_test(type "good" ca struct2.ca)
do_test(type "(nil)" ca struct3.ca)
do_test(type "good" ca struct4.ca)
do_test(type "AA {  }" ca struct_empty.ca)
do_test(type "AA  = 0\nA1  = 4\nA2  = 8\nA3  = 256\nA4  = 4\nA5  = 8\nA6  = 128\nA7  = 8\nA8  = 8\nA9  = 256\nA10 = 256\nA11 = 8\nA12 = 8192\nA13 = 128\nA14 = 8\nA15 = 4096\nA16 = 8192\nA17 = 8\nA18 = 262144\nA19 = 262144\nA20 = 8\nA21 = 8388608\nA22 = 8388608\nA23 = 8388608\nA24 = 8388864\nA25 = 16\nA26 = 776\n[*][\[][*]A26;6[\]] = 8\nA28 = 1056\n" ca struct_size2.ca)
do_test(type "good" ca struct_use1.ca)

# this test will failed when not increase the stack size, using example: `ulimit -s 65536`, so comment it
# do_test(type "good" ca struct_use2.ca)
do_test(type "good" ca struct_use3.ca)

if(${LLVM_VERSION_MAJOR} GREATER 12)
  do_testf(type "array3.ca.ll.13" "array3.ca.ll.13.tmp" ca -ll array3.ca array3.ca.ll.13.tmp)
  do_testf(type "array_pointer1.ca.ll.13" "array_pointer1.ca.ll.13.tmp" ca -ll array_pointer1.ca array_pointer1.ca.ll.13.tmp)
  do_testf(type "struct_use1.ca.ll.13" "struct_use1.ca.ll.13.tmp" ca -ll struct_use1.ca struct_use1.ca.ll.13.tmp)
  do_testf(type "struct_use2.ca.ll.13" "struct_use2.ca.ll.13.tmp" ca -ll struct_use2.ca struct_use2.ca.ll.13.tmp)
  do_testf(type "struct_use3.ca.ll.13" "struct_use3.ca.ll.13.tmp" ca -ll struct_use3.ca struct_use3.ca.ll.13.tmp)
  do_testf(type "struct_use4.ca.ll.13" "struct_use4.ca.ll.13.tmp" ca -ll struct_use4.ca struct_use4.ca.ll.13.tmp)
  do_testf(type "struct_use5.ca.ll.13" "struct_use5.ca.ll.13.tmp" ca -ll struct_use5.ca struct_use5.ca.ll.13.tmp)
  do_testf(type "array_zero1.ca.ll.13" "array_zero1.ca.ll.13.tmp" ca -ll array_zero1.ca array_zero1.ca.ll.13.tmp)
  do_testf(type "type_zero1.ca.ll.13" "type_zero1.ca.ll.13.tmp" ca -ll type_zero1.ca type_zero1.ca.ll.13.tmp)
  do_testf(type "struct_zero1.ca.ll.13" "struct_zero1.ca.ll.13.tmp" ca -ll struct_zero1.ca struct_zero1.ca.ll.13.tmp)
  do_testf(type "struct_gen1.ca.ll.13" "struct_gen1.ca.ll.13.tmp" ca -ll struct_gen1.ca struct_gen1.ca.ll.13.tmp)
else()
  do_testf(type "array3.ca.ll" "array3.ca.ll.tmp" ca -ll array3.ca array3.ca.ll.tmp)
  do_testf(type "array_pointer1.ca.ll" "array_pointer1.ca.ll.tmp" ca -ll array_pointer1.ca array_pointer1.ca.ll.tmp)
  do_testf(type "struct_use1.ca.ll" "struct_use1.ca.ll.tmp" ca -ll struct_use1.ca struct_use1.ca.ll.tmp)
  do_testf(type "struct_use2.ca.ll" "struct_use2.ca.ll.tmp" ca -ll struct_use2.ca struct_use2.ca.ll.tmp)
  do_testf(type "struct_use3.ca.ll" "struct_use3.ca.ll.tmp" ca -ll struct_use3.ca struct_use3.ca.ll.tmp)
  do_testf(type "struct_use4.ca.ll" "struct_use4.ca.ll.tmp" ca -ll struct_use4.ca struct_use4.ca.ll.tmp)
  do_testf(type "struct_use5.ca.ll" "struct_use5.ca.ll.tmp" ca -ll struct_use5.ca struct_use5.ca.ll.tmp)
  do_testf(type "array_zero1.ca.ll" "array_zero1.ca.ll.tmp" ca -ll array_zero1.ca array_zero1.ca.ll.tmp)
  do_testf(type "type_zero1.ca.ll" "type_zero1.ca.ll.tmp" ca -ll type_zero1.ca type_zero1.ca.ll.tmp)
  do_testf(type "struct_zero1.ca.ll" "struct_zero1.ca.ll.tmp" ca -ll struct_zero1.ca struct_zero1.ca.ll.tmp)
  do_testf(type "struct_gen1.ca.ll" "struct_gen1.ca.ll.tmp" ca -ll struct_gen1.ca struct_gen1.ca.ll.tmp)
endif()

do_test(type "3" ca typedef.ca)
do_test(type "line: 9, col: 20:.*`i32` type required, but found `i64`" ca typehir.ca)
do_test(type "i32\nsize = 4, type: t:i32" ca typeof.ca)
do_test(type "i32\nu64\n[*]i64\n\\[\\[i64; 2\\]; 2\\]\n[*]i64\n\\[[*]i64; 2\\]\n\\[\\[[*]i64; 2\\]; 2\\]\n\\[\\[[*]i64; 2\\]; 2\\]\n[*]\\[i64; 3\\]\ni64\ni64\n3432432\n3\n3223\n44324\n40\n80\n4\n8\n\\[1, 2, 3\\]\n\\[4, 5, 6\\]\nsize = 4, type: t:i32\nsize = 8, type: t:u64\n17\n17\n449\n\\(nil\\)\n0x2b67\n\\(nil\\)\nsize = 8, type: t:[*]i64\n\\[\\[3, 4\\], \\[12, 5\\]\\]\nsize = 32, type: t:\\[\\[i64;2\\];2\\]\n0x4ef60\nsize = 8, type: t:[*]i64\n\\[0x3, 0x4\\]\n\\[0x1e, 0x28\\]\nsize = 16, type: t:\\[[*]i64;2\\]\n\\[\\[0x3, 0x4\\], \\[0xc, 0x5\\]\\]\nsize = 32, type: t:\\[\\[[*]i64;2\\];2\\]\n\\[\\[0x3, 0x4\\], \\[0xc, 0x5\\]\\]\nsize = 32, type: t:\\[\\[[*]i64;2\\];2\\]\n\\(nil\\)\nsize = 8, type: t:[*]\\[i64;3\\]\n3\nsize = 8, type: t:i64\n321\nsize = 8, type: t:i64\n" ca typeof_rec.ca)
do_test(type "101" ca typesimple.ca)
do_testf(type "global_zero1.ca.ll" "global_zero1.ca.ll.tmp" ca -ll global_zero1.ca global_zero1.ca.ll.tmp)
do_testf(type "global_zero2.ca.ll" "global_zero2.ca.ll.tmp" ca -ll global_zero2.ca global_zero2.ca.ll.tmp)
do_testf(type "global_zero3.ca.ll" "global_zero3.ca.ll.tmp" ca -ll global_zero3.ca global_zero3.ca.ll.tmp)
do_testf(type "global_zero4.ca.ll" "global_zero4.ca.ll.tmp" ca -ll global_zero4.ca global_zero4.ca.ll.tmp)
do_test(type "type name `AA` is recursively defined: \\[grammar line: 6, token: -2\\] unwind type name `AA` failed" ca struct_rec1.ca)
do_test(type "type name `A1` is recursively defined: \\[grammar line: 6, token: -2\\] unwind type name `A1` failed" ca struct_rec2.ca)
do_test(type "type name `A2` is recursively defined: \\[grammar line: 8, token: -2\\] unwind type name `A2` failed" ca struct_rec3.ca)
do_test(type "type name `A3` is recursively defined: \\[grammar line: 12, token: -2\\] unwind type name `A3` failed" ca struct_rec4.ca)
do_test(type "type name `A1` is recursively defined: \\[grammar line: 7, token: -2\\] unwind type name `A1` failed" ca struct_bad1.ca)
do_test(type "type name `A7` is recursively defined: \\[grammar line: 28, token: -2\\] unwind type name `A7` failed" ca struct_bad2.ca)
do_testf(type "type_scope1.ca.ll" "type_scope1.ca.ll.tmp" ca -g -ll type_scope1.ca type_scope1.ca.ll.tmp)
do_testf(type "type_scope2.ca.ll" "type_scope2.ca.ll.tmp" ca -g -ll type_scope2.ca type_scope2.ca.ll.tmp)
do_testf(type "type_scope3.ca.ll" "type_scope3.ca.ll.tmp" ca -g -ll type_scope3.ca type_scope3.ca.ll.tmp)
do_testf(type "more_scope.ca.ll" "more_scope.ca.ll.tmp" ca -g -ll more_scope.ca more_scope.ca.ll.tmp)
do_test(type "u64\n1311768467294899695\n0x1234567890abcdef\nsize = 8, type: t:u64" ca hex_simple.ca)
do_test(type "u16\ni16\n16\n433\nsize = 2, type: t:u16\nsize = 2, type: t:i16" ca short1.ca)
do_test(type "16\n43\n65535\n433\n" ca short2.ca)
do_testf(type "short3.ca.ll" "short3.ca.ll.tmp" ca -g -ll short3.ca short3.ca.ll.tmp)

