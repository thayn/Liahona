# minimp3 (CC0, lieff/minimp3) - MP3 decoder for journey audio clues.
idf_component_register(SRCS "minimp3.c" INCLUDE_DIRS ".")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -O2)
