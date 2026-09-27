# Turns a binary file into a C source defining
#   const unsigned char NAME[];  const size_t NAME_size;
# Run as a script at build time:
#   cmake -DIN=<file> -DOUT=<file.c> -DNAME=<symbol> -P embed_file.cmake
file(READ "${IN}" hex HEX)
# 32 bytes per line so no compiler meets one enormous source line.
string(REGEX REPLACE "(................................................................)" "\\1\n" hex "${hex}")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${hex}")
get_filename_component(in_name "${IN}" NAME)
file(WRITE "${OUT}"
    "/* Generated from ${in_name} by cmake/embed_file.cmake -- do not edit. */\n"
    "#include <stddef.h>\n"
    "const unsigned char ${NAME}[] = {\n${body}\n};\n"
    "const size_t ${NAME}_size = sizeof(${NAME});\n")
