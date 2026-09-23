#ifndef DECLARATION_UTILS_H
#define DECLARATION_UTILS_H

#include "blocks/stmt.h"

namespace block {

bool is_splittable(decl_stmt::Ptr decl);
std::vector<decl_stmt::Ptr> find_declarations(stmt::Ptr scope);
void split_declarations(stmt::Ptr scope, std::vector<decl_stmt::Ptr> &declarations);

} // namespace block
#endif
