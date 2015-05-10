#include <stdint.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "lptree.h"

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

struct {
    char magic[4];
    uint16_t lua_version;
    uint8_t number_size;
    unsigned is_integer:1;
    unsigned endianness:1;
    /* 6 unused bits */
    char lpeg_version[sizeof(VERSION)];
} PATTERN_HEADER;

static int lp_save(lua_State *L) {
  luaL_Buffer buf;
  uint32_t treelen;
  Pattern *p = luaL_checkudata(L, 1, PATTERN_T);

  /* we want to play with stack during serialization:
     make another one for string buffer */
  lua_State *bufL = lua_newthread(L);
  luaL_buffinit (bufL, &buf);

  /* write header */
  luaL_addlstring(&buf, (const char *)&PATTERN_HEADER, sizeof(PATTERN_HEADER));

  /* dump tree */
  treelen = lua_objlen(L, 1) - sizeof(Pattern) + sizeof(TTree);
  luaL_addlstring(&buf, (const char *)&treelen, sizeof(uint32_t));
  luaL_addlstring(&buf, (const char *)p->tree, treelen);

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
  size_t len;
  const char *buf, *end;
  uint32_t treesize;
  Pattern *p;

  buf = luaL_checklstring(L, 1, &len);
  end = buf + len;

  /* check header */
  checkbuffer(buf, end, sizeof(PATTERN_HEADER));
  if (memcmp(buf, &PATTERN_HEADER, sizeof(PATTERN_HEADER)) != 0) {
    luaL_error(L, "header mismatch");
  }
  buf += sizeof(PATTERN_HEADER);

  checkbuffer(buf, end, sizeof(uint32_t));
  treesize = *(const uint32_t*)buf;
  buf += sizeof(uint32_t);

  checkbuffer(buf, end, treesize);
  p = (Pattern *)lua_newuserdata(L, treesize + sizeof(Pattern) - sizeof(TTree));
  p->code = NULL;
  p->codesize = 0;
  memcpy(p->tree, buf, treesize);
  buf += treesize;

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
  memset(&PATTERN_HEADER, 0, sizeof(PATTERN_HEADER));
  memcpy(PATTERN_HEADER.magic, "LPEG", 4);
  PATTERN_HEADER.lua_version = LUA_VERSION_NUM;
  PATTERN_HEADER.number_size = sizeof(lua_Number);
  PATTERN_HEADER.is_integer = ((lua_Number)0.5) == 0;
  PATTERN_HEADER.endianness = *(char*)&x;
  memcpy(PATTERN_HEADER.lpeg_version, VERSION, sizeof(VERSION));
}

