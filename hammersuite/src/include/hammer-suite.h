#pragma once

#include <stdint.h>

#include "types.h"

void hammer_session(SessionConfig * cfg, MemoryBuffer * memory);
void fuzzing_session(SessionConfig * cfg, MemoryBuffer * memory);
