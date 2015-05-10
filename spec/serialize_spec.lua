describe("LPeg serialization", function()
  local L = require "lpeg"
  local Ls = require "lpeg.serialize"

  it('should serialize bytecode', function()
	-- no real way to test it unless digging into binary
	local p = (L.P'aaa' + 'bbb') * 'ccc'
	local tree_only = Ls.save(p)
	local tree_and_bytecode = Ls.save(p, true)
	assert.is_true(#tree_only < #tree_and_bytecode)
	assert.are.same(7, Ls.load(tree_and_bytecode):match('aaaccc'))
  end)

  for save_bc, msg in pairs{ [false] = ' (no bytecode)', [true] = ' (with bytecode)'} do
	local function persist(p)
	  return assert(Ls.load(assert(Ls.save(p, save_bc))))
	end

	it('should serialize to strings' .. msg, function()
	  assert.is.equal('string', type(Ls.save(L.P'aaa', save_bc)))
	end)

	it('should deserialize to pattern' .. msg, function()
	  assert.is.equal('pattern', L.type(persist(L.P'a')))
	end)

	it('should match simple patterns' .. msg, function()
	  assert.are.same(4, persist(L.P'aaa'):match('aaa'))
	  assert.are.same('aaa', persist(L.C'aaa'):match('aaa'))
	end)

	it('should serialize attached values' .. msg, function()
	  --XXX: bug in LPeg (crashes Lua, even without serialization)
	  --assert.are.same(nil,     persist(L.P'aaa' * L.Cc(nil)):match('aaa'))
	  assert.are.same(true,    persist(L.P'aaa' * L.Cc(true)):match('aaa'))
	  assert.are.same(42,      persist(L.P'aaa' * L.Cc(42)):match('aaa'))
	  assert.are.same('foo',   persist(L.P'aaa' * L.Cc('foo')):match('aaa'))
	  assert.are.same({1,2,3}, persist(L.P'aaa' * L.Cc({1,2,3})):match('aaa'))
	  assert.are.same('AAA',   persist(L.P'aaa' / function(s) return s:upper() end):match('aaa'))
	end)

	it('encodes multiple tables' .. msg, function()
	  -- taken from LPeg test suite (discovered a bug in table serializer)
	  assert.are.same(-3, persist(L.C(1)/'%0%1'/{aa = 'z'}/{z = -3} * 'x'):match('ax'))
	end)

	it('encodes nil captures' .. msg, function()
	  assert.are.same(nil, persist(L.Cc(nil)):match(''))
	end)
  end

  it('should not load corrupted code', function()
    local ser = assert(Ls.save(L.P'aaa'))
    ser = string.char(0) .. ser:sub(2)
    assert.has.errors(function() Ls.load(set) end)
  end)
end)

