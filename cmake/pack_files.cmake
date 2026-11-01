# pack_files.cmake — join files into one gzipped pack at build time, for
# embed_resources.cmake.
#
#   cmake -DLIST=<list.cmake> -DOUT=<pack.gz> -P pack_files.cmake
#
# LIST sets FILES to the paths to join, in order. The pack is one gzip
# stream, so each file compresses against the files before it: the addon
# text packs about 16% smaller than when each file is gzipped alone. Inside
# the stream is an index, each file's size as a decimal line and then an
# empty line, followed by the files' bytes in FILES order. The DLL splits
# it in src/embed/Resource.cpp.

include("${LIST}")

set(index "")
foreach(file IN LISTS FILES)
    file(SIZE "${file}" size)
    string(APPEND index "${size}\n")
endforeach()
# Not file(WRITE): on Windows it writes each \n as \r\n.
file(CONFIGURE OUTPUT "${OUT}.index" CONTENT "${index}\n" @ONLY NEWLINE_STYLE UNIX)

# Join `ARGN` byte for byte into `out`.
function(cat out)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E cat ${ARGN}
                    OUTPUT_FILE "${out}" RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "pack_files.cmake: cannot join into ${out}: ${rc}")
    endif()
endfunction()

# `cmake -E cat` takes its files on the command line, which Windows limits
# to 32767 characters: join in batches of 100, then join the batches.
set(parts "${OUT}.index")
list(LENGTH FILES count)
set(first 0)
while(first LESS count)
    list(SUBLIST FILES ${first} 100 batch)
    set(part "${OUT}.part${first}")
    cat("${part}" ${batch})
    list(APPEND parts "${part}")
    math(EXPR first "${first} + 100")
endwhile()
cat("${OUT}.raw" ${parts})

# The raw format holds the one file's bytes with no archive entry around
# them, so OUT is a plain gzip stream.
file(ARCHIVE_CREATE OUTPUT "${OUT}" PATHS "${OUT}.raw" FORMAT raw
     COMPRESSION GZip COMPRESSION_LEVEL 9)
file(REMOVE ${parts} "${OUT}.raw")
