file(GLOB PRBOOM_SRCS "*.c")
idf_component_register(SRCS ${PRBOOM_SRCS} INCLUDE_DIRS "." LDFRAGMENTS "linker.lf")
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=address -Wno-misleading-indentation -Wno-format-overflow -Wno-char-subscripts
    -Wno-missing-field-initializers -Wno-unused-but-set-variable -Wno-unused-variable
    -Wno-implicit-fallthrough -Wno-sign-compare -Wno-parentheses -Wno-dangling-else
    -Wno-unused-function -Wno-maybe-uninitialized -Wno-stringop-truncation -Wno-format
    -Wno-array-bounds -Wno-pointer-sign -Wno-old-style-declaration -Wno-type-limits
    -Wno-enum-int-mismatch -Wno-shift-negative-value -Wno-switch -Wno-empty-body
    -Wno-int-in-bool-context -Wno-cast-function-type -Wno-unused-value -Wno-error
    -DHAVE_CONFIG_H -O2)
