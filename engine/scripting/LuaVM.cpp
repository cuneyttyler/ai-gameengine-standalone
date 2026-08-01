#include "scripting/LuaVM.h"

namespace aige {

LuaVM::LuaVM() {
    lua_.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);
}

} // namespace aige
