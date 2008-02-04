dnl vim: set ft=m4 et :
dnl This file is used by Makefile.am.m4. You should use the provided
dnl autogen.bash script to do all the hard work.
dnl
dnl This file is used to avoid having to make lots of repetitive changes in
dnl Makefile.am every time we add a source or test file. The first parameter is
dnl the base filename with no extension; later parameters can be `hh', `cc',
dnl `test', `impl', `testscript'. Note that there isn't much error checking done
dnl on this file at present...

add(`action_queue',                      `hh', `cc', `test')
add(`attributes',                        `hh')
add(`byte_swap',                         `hh', `test')
add(`config_file',                       `hh', `cc', `se', `test', `testscript')
add(`clone',                             `hh', `impl')
add(`condition_variable',                `hh', `cc', `test')
add(`cookie',                            `hh', `cc')
add(`create_iterator',                   `hh', `fwd', `impl', `test')
add(`damerau_levenshtein',               `hh', `cc', `test')
add(`destringify',                       `hh', `cc', `test')
add(`dir_iterator',                      `hh', `cc', `test', `testscript')
add(`exception',                         `hh', `cc')
add(`fast_unique_copy',                  `hh', `test')
add(`fd_output_stream',                  `hh')
add(`fs_entry',                          `hh', `cc', `fwd', `test', `testscript')
add(`fd_holder',                         `hh')
add(`graph',                             `hh', `cc', `fwd', `impl', `test')
add(`iterator_funcs',                    `hh', `test')
add(`indirect_iterator',                 `hh', `fwd', `impl', `test')
add(`instantiation_policy',              `hh', `impl', `test')
add(`is_file_with_extension',            `hh', `cc', `se', `test', `testscript')
add(`join',                              `hh', `test')
add(`kc',                                `hh', `fwd')
add(`keys',                              `hh')
add(`log',                               `hh', `cc', `se', `test')
add(`make_shared_ptr',                   `hh', `fwd')
add(`map',                               `hh', `fwd', `impl', `cc')
add(`member_iterator',                   `hh', `fwd', `impl', `test')
add(`md5',                               `hh', `cc', `test')
add(`mutex',                             `hh', `cc', `test')
add(`no_type',                           `hh')
add(`operators',                         `hh')
add(`options',                           `hh', `fwd', `cc', `test')
add(`output_wrapper',                    `test', `testscript')
add(`pipe',                              `hh', `cc')
add(`pretty_print',                      `hh', `cc', `test')
add(`private_implementation_pattern',    `hh', `impl')
add(`random',                            `hh', `cc', `test')
add(`remove_shared_ptr',                 `hh')
add(`rmd160',                            `hh', `cc', `test')
add(`save',                              `hh', `test')
add(`sequence',                          `hh', `fwd', `impl')
add(`set',                               `hh', `fwd', `impl')
add(`sha1',                              `hh', `cc', `test')
add(`sha256',                            `hh', `cc', `test')
add(`sr',                                `hh', `cc')
add(`stringify',                         `hh', `test')
add(`strip',                             `hh', `cc', `test')
add(`system',                            `hh', `cc', `test', `testscript')
add(`thread',                            `hh', `cc', `test')
add(`thread_pool',                       `hh', `cc', `test')
add(`tokeniser',                         `hh', `cc', `test')
add(`tr1_memory',                        `hh')
add(`tr1_type_traits',                   `hh')
add(`tr1_functional',                    `hh')
add(`util',                              `hh')
add(`validated',                         `hh', `fwd', `test')
add(`virtual_constructor',               `hh', `impl', `test')
add(`visitor',                           `hh', `impl', `fwd', `test')
add(`visitor_cast',                      `hh', `test')
add(`wrapped_forward_iterator',          `hh', `fwd', `impl', `test')
add(`wrapped_output_iterator',           `hh', `fwd', `impl')

