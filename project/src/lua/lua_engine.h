#pragma once
#include "lua.h"
#include "lua.hpp"
#include <string>
#include <iostream>

class LuaEngine {
public:
    lua_State* L;
    LuaEngine(){
        L = luaL_newstate();
    };
    ~LuaEngine(){
        close();
    };
    
    void setup_system_libs(){
        luaL_requiref(L, "_G", luaopen_base, 1);
        luaL_requiref(L, "math", luaopen_math, 1);
        luaL_requiref(L, "table", luaopen_table, 1);
        luaL_requiref(L, "string", luaopen_string, 1);
        lua_pop(L, 4);

        lua_pushnil(L);
        lua_setglobal(L, "load");
        lua_pushnil(L);
        lua_setglobal(L, "loadfile");
        lua_pushnil(L);
        lua_setglobal(L, "dofile");

        lua_atpanic(L, panic_handler);
    }

    void close(){
        if(L != nullptr){
            lua_close(L);
            L = nullptr;
        }
    }
    bool exec(const std::string& script);

private:
    static int panic_handler(lua_State* L){
        const char* msg = lua_tostring(L, -1);
        std::cerr << "CRITICAL LUA PANIC: " << msg << std::endl;
        return 0;
    }
};