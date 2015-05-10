describe("LPeg serialization", function()
  local L = require "lpeg"
  local Ls = require "lpeg.serialize"
  local function persist(p)
    return assert(Ls.load(assert(Ls.save(p))))
  end

  it('should serialize to strings', function()
    assert.is.equal('string', type(Ls.save(L.P'aaa')))
  end)

  it('should deserialize to pattern', function()
    assert.is.equal('pattern', L.type(persist(L.P'a')))
  end)

  it('should match simple patterns', function()
    assert.are.same(4, persist(L.P'aaa'):match('aaa'))
    assert.are.same('aaa', persist(L.C'aaa'):match('aaa'))
  end)

  it('should serialize attached values', function()
    assert.are.same(true,    persist(L.P'aaa' * L.Cc(true)):match('aaa'))
    assert.are.same(42,      persist(L.P'aaa' * L.Cc(42)):match('aaa'))
    assert.are.same('foo',   persist(L.P'aaa' * L.Cc('foo')):match('aaa'))
    assert.are.same({1,2,3}, persist(L.P'aaa' * L.Cc({1,2,3})):match('aaa'))
    assert.are.same('AAA',   persist(L.P'aaa' / function(s) return s:upper() end):match('aaa'))
  end)

  it('should not load corrupted code', function()
    local ser = assert(Ls.save(L.P'aaa'))
    ser = string.char(0) .. ser:sub(2)
    assert.has.errors(function() Ls.load(set) end)
  end)
end)

