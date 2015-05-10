# LPeg Serialize

This module can serialize and load LPeg patterns to binary format.
[LPeg](http://www.inf.puc-rio.br/~roberto/lpeg/lpeg.html) is a pattern matching
library for the Lua language.

Like `string.dump` for Lua functions, the pattern is saved in a binary form
and can be loaded later, on another machine, ...

## Status

This library is currently under development:
* It may/does contain bugs
* The binary format *is not guaranteed to be stable* (don't expect commit n+1
  will load pattern saved by commit n)

## Installation

Use the provided Makefile to compile the module (only the `linux` and `macosx`
targets are currently supported). You need original lpeg source code too (only
for headers).

Then install `lpeg/serialize.so` somewhere in your CPATH.

## Usage

```lua
local L = require 'lpeg'
local Ls = require 'lpeg.serialize'

local serialized = Ls.save(L.P'aaa')
assert(type(serialized) == 'string')
local unserialized = Ls.load(serialized)
assert(L.type(unserialized) == 'pattern')

print(unserialized:match('aaa'))
```

## Limitations

As arbitrary Lua values can be included in LPeg patterns (tables, callback
functions, ...), patterns cannot be serialized when they contain:

* Tables with metatables
* C functions
* Functions with upvalues

Also, be careful with global accesses in serialized functions as it requires
that these global still exists when pattern will be loaded.

In addition, the serialized format uses host endianness and type sizes, the
functions are serialized with `lua_dump` so additional limitations may apply.
If pattern cannot be loaded because of host environment, an error will be
raised.

## License

The MIT License (MIT)

Copyright (c) 2015 Julien Desgats

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

