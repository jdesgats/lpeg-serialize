#include <stdint.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "lptree.h"
#include "lpvm.h"

/*
** ktable serialization (actually totally independant from LPeg.
*/

#if (LUA_VERSION_NUM < 502)
static int lua_absindex (lua_State *L, int idx) {
  return (idx > 0 || idx <= LUA_REGISTRYINDEX) ?
      idx :
      lua_gettop(L) + 1 + idx;
}
#endif

#ifndef LUA_OK
# define LUA_OK 0
#endif

static void encodevalue(lua_State *L, int idx, luaL_Buffer *buf);

static int writer (lua_State *L, const void* b, size_t size, void* B) {
  (void)L;
  luaL_addlstring((luaL_Buffer*) B, (const char *)b, size);
  return 0;
}

/* serializes value on top into given buffer, the buffer must not be on the
** same state as L (because this function uses stack).
*/
static void encodevalue(lua_State *L, int idx, luaL_Buffer *buf) {
  idx = lua_absindex(L, idx);
  switch(lua_type(L, idx)) {
    case LUA_TNIL:
      luaL_addchar(buf, LUA_TNIL);
      break;
    case LUA_TBOOLEAN:
      luaL_addchar(buf, LUA_TBOOLEAN);
      luaL_addchar(buf, (char)lua_toboolean(L, idx));
      break;
    case LUA_TNUMBER: {
      lua_Number n = lua_tonumber(L, idx);
      luaL_addchar(buf, LUA_TNUMBER);
      luaL_addlstring(buf, (const char *)&n, sizeof(lua_Number));
      break;
    }
    case LUA_TSTRING: {
      size_t len;
      uint32_t serlen;
      const char *str = lua_tolstring(L, idx, &len);
      if (len > UINT32_MAX) {
          luaL_error(L, "string too long");
      }

      serlen = len; /* be sure of the size */
      luaL_addchar(buf, LUA_TSTRING);
      luaL_addlstring(buf, (const char *)&serlen, sizeof(uint32_t));
      luaL_addlstring(buf, str, len);
      break;
    }
    case LUA_TTABLE:
      if (lua_getmetatable(L, idx)) {
          /* TODO: why not ? */
          luaL_error(L, "cannot serialize table with metatable");
      }
      luaL_addchar(buf, LUA_TTABLE);
      lua_pushnil(L);
      while(lua_next(L, idx) != 0) {
        encodevalue(L, -2, buf);
        encodevalue(L, -1, buf);
        lua_pop(L, 1);
      }
      /* signal end of table (key cannot be nil) */
      luaL_addchar(buf, LUA_TNIL);
      break;
    case LUA_TFUNCTION: {
      luaL_Buffer dumpbuf;
      lua_Debug ar;
      uint32_t dumplen;

      lua_pushvalue(L, idx);
      if (lua_iscfunction(L, idx)) {
        luaL_error(L, "cannot serialize C function");
      }
      if (lua_getinfo(L, ">u", &ar) == 0 || ar.nups > 0) {
        luaL_error(L, "cannot serialize function with upvalues");
      }
      
      /* we need to get the size of the dump, before dump itself:
         use a separate buffer for dumping */
      /* TODO: save function name */
      lua_pushvalue(L, idx);
      luaL_buffinit(L, &dumpbuf);
      if (lua_dump(L, writer, &dumpbuf) != 0) {
        luaL_error(L, "unable to dump function");
      }
      luaL_pushresult(&dumpbuf);

      dumplen = lua_objlen(L, -1);
      luaL_addchar(buf, LUA_TFUNCTION);
      luaL_addlstring(buf, (const char *)&dumplen, sizeof(uint32_t));
      lua_xmove(L, buf->L, 1);
      luaL_addvalue(buf);
      lua_pop(L, 1); /* pops function */
      break;
    }
    default:
      luaL_error(L, "cannot serialize %s", luaL_typename(L, idx));
  }
}

#define checkbuffer(cur, end, n) do { \
  if (cur + n > end) luaL_error(L, "wrong code"); \
} while(0)

static const char* decodevalue(lua_State *L, const char *buf, const char *end) {
  checkbuffer(buf, end, 1);
  switch (*buf++) {
    case LUA_TNIL:
      lua_pushnil(L);
      break;
    case LUA_TBOOLEAN:
      checkbuffer(buf, end, 1);
      lua_pushboolean(L, *buf != 0);
      buf++;
      break;
    case LUA_TNUMBER:
      checkbuffer(buf, end, sizeof(lua_Number));
      lua_pushnumber(L, *(const lua_Number *)buf);
      buf += sizeof(lua_Number);
      break;
    case LUA_TSTRING: {
      uint32_t serlen;
      checkbuffer(buf, end, sizeof(uint32_t));
      serlen = *(const uint32_t*)(buf);
      buf += sizeof(uint32_t);
      checkbuffer(buf, end, serlen);
      lua_pushlstring(L, buf, serlen);
      buf += serlen;
      break;
    }
    case LUA_TTABLE:
      lua_newtable(L);
      checkbuffer(buf, end, 1);
      while (*buf != LUA_TNIL) {
        buf = decodevalue(L, buf, end);
        checkbuffer(buf, end, 1);
        buf = decodevalue(L, buf, end);
        checkbuffer(buf, end, 1);
        lua_settable(L, -3);
      }
      break;
    case LUA_TFUNCTION: {
      uint32_t dumplen;
      checkbuffer(buf, end, sizeof(uint32_t));
      dumplen = *(const uint32_t*)(buf);
      buf += sizeof(uint32_t);
      checkbuffer(buf, end, dumplen);
      if (luaL_loadbuffer(L, buf, dumplen, "unserialized") != LUA_OK) {
        luaL_error(L, "failed to load function");
      }
      buf += dumplen;
      break;
    }
    default:
      luaL_error(L, "wrong type identifier");
  }
  assert(buf <= end);
  return buf;
}

/* Actual pattern serialization. Binary format is as follows:
**   1. header
**   2. node tree dump
**      2.1. (uint32_t) tree size
**      2.2. tree nodes
**   3. ktable dump
** Header contains informations about version, type sizes, endianness, ...
** As all informations are dumped using host types, the header *must* match
** when loading patterns.
*/

#define CURRENT_FORMAT_VERSION 1

typedef struct {
    char magic[4];
    uint16_t format_version;
    uint16_t lua_version;
    uint8_t number_size;
    uint8_t tree_size;
    uint8_t instruction_size;
    unsigned is_integer:1;
    unsigned endianness:1;
    unsigned has_bytecode:1;
    /* 5 unused bits */
    char lpeg_version[sizeof(VERSION)];
} PatternHeader;
PatternHeader HOST_HEADER;

static void compile_pattern(lua_State *L, Pattern *p, int idx) {
  if (p->code == NULL) {
    /* not yet compiled: force compilation, do not call compile directly
     * because final fix need to be applied before. Use regular match method */
    /* FIXME: this is very fragile as relies on fact that subject type is
     * checked after having compiled the code */
    lua_getfield(L, idx, "match");
    if (lua_isnil(L, -1))
      luaL_error(L, "can't find :match() method");
    lua_pushvalue(L, idx);
    lua_pushnil(L);
    /* the function is expected to fail, it does not matter as long as pattern
     * has been expected */
    if (lua_pcall(L, 2, 0, 0) != LUA_ERRRUN || p->code == NULL)
      luaL_error(L, "can't compile pattern: unknown error");
  }
}

static int lp_save(lua_State *L) {
  Pattern *p;
  luaL_Buffer buf;
  PatternHeader header;
  uint32_t treelen;

  p = luaL_checkudata(L, 1, PATTERN_T);

  /* prepare header */
  memcpy(&header, &HOST_HEADER, sizeof(PatternHeader));
  header.has_bytecode = lua_toboolean(L, 2);

  /* we want to play with stack during serialization:
     make another one for string buffer */
  lua_State *bufL = lua_newthread(L);
  luaL_buffinit (bufL, &buf);

  /* write header */
  luaL_addlstring(&buf, (const char *)&header, sizeof(PatternHeader));

  /* dump tree */
  treelen = (lua_objlen(L, 1) - sizeof(Pattern)) / sizeof(TTree) + 1;
  luaL_addlstring(&buf, (const char *)&treelen, sizeof(uint32_t));
  luaL_addlstring(&buf, (const char *)p->tree, treelen * sizeof(TTree));

  if (header.has_bytecode) {
    uint32_t codesize;
    compile_pattern(L, p, 1);
    codesize = p->codesize; /* force a fixed size type */
    luaL_addlstring(&buf, (const char *)&codesize, sizeof(uint32_t));
    luaL_addlstring(&buf, (const char *)p->code, p->codesize * sizeof(Instruction));
  }

  /* dump ktable (if any) */
  lua_getfenv(L, 1);
  if (!(lua_istable(L, -1) && lua_objlen(L, -1) > 0)) {
    /* in Lua 5.1, default environment is a table, be sure to encode nil */
    lua_pushnil(L);
  }
  encodevalue(L, -1, &buf);

  /* finalize */
  luaL_pushresult(&buf);
  lua_xmove(bufL, L, 1);
  return 1;
}

static int lp_load(lua_State *L) {
  const char *buf, *end;
  Pattern *p;
  PatternHeader header;
  int has_bytecode;
  size_t len;
  uint32_t treelen;

  buf = luaL_checklstring(L, 1, &len);
  end = buf + len;

  /* check header */
  checkbuffer(buf, end, sizeof(PatternHeader));
  memcpy(&header, buf, sizeof(PatternHeader));
  has_bytecode = header.has_bytecode;
  header.has_bytecode = 0; /* to check header validity */
  if (memcmp(&header, &HOST_HEADER, sizeof(PatternHeader)) != 0) {
    luaL_error(L, "header mismatch");
  }
  buf += sizeof(HOST_HEADER);

  checkbuffer(buf, end, sizeof(uint32_t));
  treelen = *(const uint32_t*)buf;
  buf += sizeof(uint32_t);

  checkbuffer(buf, end, treelen * sizeof(TTree));
  p = (Pattern *)lua_newuserdata(L, (treelen - 1) * sizeof(TTree) + sizeof(Pattern));
  p->code = NULL;
  p->codesize = 0;
  memcpy(p->tree, buf, treelen * sizeof(TTree));
  buf += treelen * sizeof(TTree);

  if (has_bytecode) {
    void *ud;
    lua_Alloc f;
    uint32_t codesize;

    checkbuffer(buf, end, sizeof(uint32_t));
    codesize = *(const uint32_t*)buf;
    buf += sizeof(uint32_t);
    if (codesize == 0) luaL_error(L, "wrong code");

    f = lua_getallocf(L, &ud);
    checkbuffer(buf, end, codesize * sizeof(Instruction));
    p->code = f(ud, NULL, 0, codesize * sizeof(Instruction));
    p->codesize = codesize;
    if (p->code == NULL) luaL_error(L, "not enough memory");
    memcpy(p->code, buf, codesize * sizeof(Instruction));
    buf += codesize * sizeof(Instruction);
  }

  luaL_getmetatable(L, PATTERN_T);
  if (lua_isnil(L, -1)) {
    /* XXX: maybe we could try to require it at this point */
    luaL_error(L, "lpeg not loaded");
  }
  lua_setmetatable(L, -2);

  decodevalue(L, buf, end);
  if (lua_istable(L, -1)) {
    lua_setfenv(L, -2);
  } else {
    lua_pop(L, 1);
  }
  return 1;
}

static struct luaL_Reg serlializereg[] = {
  {"save", lp_save},
  {"load", lp_load},
};

int luaopen_lpeg_serialize (lua_State *L);
int luaopen_lpeg_serialize (lua_State *L) {
  luaL_register(L, "lpeg.serialize", serlializereg);
  return 1;
}

/* TODO: Windows support */
void lib_init(void) __attribute__((constructor));
void lib_init(void) {
  int x=1;
  /* initialize header */
  memset(&HOST_HEADER, 0, sizeof(HOST_HEADER));
  memcpy(HOST_HEADER.magic, "LPEG", 4);
  HOST_HEADER.format_version = CURRENT_FORMAT_VERSION;
  HOST_HEADER.lua_version = LUA_VERSION_NUM;
  HOST_HEADER.number_size = sizeof(lua_Number);
  HOST_HEADER.tree_size = sizeof(TTree);
  HOST_HEADER.instruction_size = sizeof(Instruction);
  HOST_HEADER.is_integer = ((lua_Number)0.5) == 0;
  HOST_HEADER.endianness = *(char*)&x;
  memcpy(HOST_HEADER.lpeg_version, VERSION, sizeof(VERSION));
}

