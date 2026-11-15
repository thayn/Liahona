# Journey engine: runs a Manager "Ball file" (journey.json) against the roster.
idf_component_register(SRCS "journey.c" INCLUDE_DIRS "." REQUIRES cjson)
