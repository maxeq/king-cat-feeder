#pragma once

// Semantic versioning: MAJOR.MINOR.PATCH. Bump together with CHANGELOG.md and a git tag.
#define FW_VERSION "0.1.0"

// Set at build time: --build-property "compiler.cpp.extra_flags=-DFW_GIT=\"<hash>\""
#ifndef FW_GIT
#define FW_GIT "dev"
#endif

#define FW_BUILT __DATE__ " " __TIME__
