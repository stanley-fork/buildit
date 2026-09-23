#ifndef LOOP_NORMALIZER_H
#define LOOP_NORMALIZER_H

#include "blocks/stmt.h"

namespace block {
void normalize_loop_backedges(stmt::Ptr ast);
}

#endif
