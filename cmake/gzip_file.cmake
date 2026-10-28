# gzip_file.cmake — gzip one file at build time, for embed_resources.cmake.
#
#   cmake -DIN=<file> -DOUT=<file.gz> -P gzip_file.cmake
#
# The raw format holds the one file's bytes with no archive entry around
# them, so OUT is a plain gzip stream of IN.
file(ARCHIVE_CREATE OUTPUT "${OUT}" PATHS "${IN}" FORMAT raw
     COMPRESSION GZip COMPRESSION_LEVEL 9)
