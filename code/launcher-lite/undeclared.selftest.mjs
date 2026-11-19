/**
 * undeclared.selftest.mjs - no module the launcher ships calls a name it never declares.
 * Run with:  node undeclared.selftest.mjs
 *
 * WHY THIS EXISTS. JavaScript does not look a name up until the line runs, so a call to
 * something that does not exist parses, passes `node --check`, and waits for a button press
 * on a player's machine. It has got out three ways:
 *
 *   v0.3.101     installFull read an identifier that was never bound, twice, and every fresh
 *                "Install everything" failed with "manifest is not defined".
 *   2026-08 on   six functions called require(), which an ES module does not have. Uninstall
 *                and Deep clean removed nothing for three months (require.selftest.mjs).
 *   2026-11-18   a rename left three callers of the old name behind. Caught by a grep before
 *                it was committed, which is luck, not a check.
 *
 * So this reads each shipped module, sets aside its comments, strings and regular
 * expressions, collects every name the file declares anywhere - functions, const / let /
 * var, imports, parameters, catch and class names - and fails on a CALL to a bare name that
 * is neither declared in the file nor a global the runtime provides.
 *
 * WHAT IT IS NOT. It is not a linter. "Declared anywhere in the file" ignores scope, so a
 * name declared in one function and called in another passes; and it looks at calls only,
 * not at every read. It catches the name that exists nowhere, which is what all three of
 * the above were. A real linter's no-undef rule would catch more and needs a dependency.
 *
 * Reads files only. Needs nothing from node_modules.
 */

import { readdirSync, readFileSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))

// What Node and Electron's main process provide without an import. A new global that this
// reports is added HERE, with the runtime that provides it - never worked around.
const GLOBALS = new Set([
  // language
  'String', 'Number', 'Boolean', 'Array', 'Object', 'JSON', 'Math', 'Date', 'Promise', 'BigInt', 'Symbol',
  'Map', 'Set', 'WeakMap', 'WeakSet', 'RegExp', 'Error', 'TypeError', 'RangeError', 'Uint8Array', 'ArrayBuffer', 'Intl',
  'parseInt', 'parseFloat', 'isNaN', 'isFinite', 'encodeURIComponent', 'decodeURIComponent', 'encodeURI', 'decodeURI',
  // Node and the browser both
  'setTimeout', 'clearTimeout', 'setInterval', 'clearInterval', 'queueMicrotask', 'structuredClone', 'fetch',
  'URL', 'URLSearchParams', 'TextEncoder', 'TextDecoder', 'AbortController', 'WebSocket', 'atob', 'btoa',
  // Node
  'Buffer', 'setImmediate'
])

const KEYWORDS = new Set(('if for while switch catch function return typeof await new delete void throw else do try ' +
  'finally case in of with yield async static get set import super').split(' '))

// The same text with comments, string contents, template text and regular expressions blanked
// to spaces, line for line, so what is left is code and the line numbers still hold. The
// expressions inside a template's ${ } are code and are kept.
function codeOnly (text) {
  let out = ''
  let i = 0
  let lastCode = ''

  // A slash starts a regular expression after an operator or an opening bracket, and after
  // a few keywords; anywhere else it divides.
  const regexMayStart = () => lastCode === '' || '=(:,;!&|?{}[+-*%<>~^'.includes(lastCode) ||
    /\b(return|typeof|case|in|of|delete|void|throw)\s*$/.test(out)

  while (i < text.length) {
    const c = text[i]
    const next = text[i + 1]

    if (c === '/' && next === '/') {
      while (i < text.length && text[i] !== '\n') { out += ' '; i++ }
      continue
    }

    if (c === '/' && next === '*') {
      while (i < text.length && !(text[i] === '*' && text[i + 1] === '/')) { out += text[i] === '\n' ? '\n' : ' '; i++ }
      out += '  '
      i += 2
      continue
    }

    if (c === "'" || c === '"') {
      out += c
      i++
      while (i < text.length && text[i] !== c && text[i] !== '\n') {
        if (text[i] === '\\') { out += '  '; i += 2; continue }
        out += ' '
        i++
      }
      out += c
      i++
      lastCode = c
      continue
    }

    if (c === '`') {
      out += '`'
      i++
      while (i < text.length && text[i] !== '`') {
        if (text[i] === '\\') { out += '  '; i += 2; continue }
        if (text[i] === '$' && text[i + 1] === '{') {
          i += 2
          let depth = 1
          let expression = ''
          while (i < text.length) {
            if (text[i] === '{') depth++
            else if (text[i] === '}' && --depth === 0) break
            expression += text[i]
            i++
          }
          out += '${' + codeOnly(expression) + '}'
          i++
          continue
        }
        out += text[i] === '\n' ? '\n' : ' '
        i++
      }
      out += '`'
      i++
      lastCode = '`'
      continue
    }

    if (c === '/' && regexMayStart()) {
      out += ' '
      i++
      let inClass = false
      while (i < text.length && text[i] !== '\n' && (text[i] !== '/' || inClass)) {
        if (text[i] === '\\') { out += '  '; i += 2; continue }
        if (text[i] === '[') inClass = true
        else if (text[i] === ']') inClass = false
        out += ' '
        i++
      }
      out += ' '
      i++
      while (i < text.length && /[a-z]/.test(text[i])) { out += ' '; i++ }
      lastCode = 'x'
      continue
    }

    out += c
    if (!/\s/.test(c)) lastCode = c
    i++
  }

  return out
}

// The text between the bracket at `open` and the one that closes it.
function inside (code, open) {
  const pairs = { '(': ')', '{': '}', '[': ']' }
  let depth = 0
  for (let i = open; i < code.length; i++) {
    if ('({['.includes(code[i])) depth++
    else if (')}]'.includes(code[i]) && --depth === 0) return code.slice(open + 1, i)
  }
  return pairs[code[open]] ? code.slice(open + 1) : ''
}

// Splits on the commas that are not inside a bracket.
function pieces (list) {
  const out = []
  let depth = 0
  let piece = ''
  for (const c of list) {
    if ('({['.includes(c)) depth++
    else if (')}]'.includes(c)) depth--
    if (c === ',' && depth === 0) { out.push(piece); piece = '' } else piece += c
  }
  out.push(piece)
  return out
}

// The part of a piece before `mark`, where `mark` is not inside a bracket.
function before (piece, mark) {
  let depth = 0
  for (let i = 0; i < piece.length; i++) {
    if ('({['.includes(piece[i])) depth++
    else if (')}]'.includes(piece[i])) depth--
    else if (piece[i] === mark && depth === 0) return piece.slice(0, i)
  }
  return piece
}

// Every name a parameter list or a destructuring pattern binds: a, b = 1, ...rest,
// { c, d: e, f: { g } }, [h, i].
function bound (list, into, pattern = false) {
  for (let piece of pieces(list)) {
    piece = before(piece, '=').trim().replace(/^\.\.\./, '').trim()
    if (!piece) continue

    if (piece[0] === '{' || piece[0] === '[') {
      bound(inside(piece, 0), into, piece[0] === '{')
      continue
    }

    // In an object pattern "key: target" binds the target, which may be a pattern itself.
    if (pattern && piece.includes(':')) {
      bound(piece.slice(piece.indexOf(':') + 1), into, false)
      continue
    }

    if (/^[A-Za-z_$][\w$]*$/.test(piece)) into.add(piece)
  }
}

function declaredNames (code) {
  const names = new Set()

  for (const match of code.matchAll(/\bfunction\s*\*?\s*([A-Za-z_$][\w$]*)?\s*\(/g)) {
    if (match[1]) names.add(match[1])
    bound(inside(code, match.index + match[0].length - 1), names)
  }

  for (const match of code.matchAll(/\b(?:const|let|var)\s+([A-Za-z_$][\w$]*)/g)) names.add(match[1])
  for (const match of code.matchAll(/\b(?:const|let|var)\s*([{[])/g)) {
    const open = match.index + match[0].length - 1
    bound(inside(code, open), names, match[1] === '{')
  }

  for (const match of code.matchAll(/\bimport\s+([A-Za-z_$][\w$]*)/g)) names.add(match[1])
  for (const match of code.matchAll(/\bimport\s+\*\s+as\s+([A-Za-z_$][\w$]*)/g)) names.add(match[1])
  for (const match of code.matchAll(/\bimport\s+(?:[A-Za-z_$][\w$]*\s*,\s*)?\{([^}]*)\}/g)) {
    for (const piece of match[1].split(',')) {
      const name = piece.trim().split(/\s+as\s+/).pop().trim()
      if (name) names.add(name)
    }
  }

  // Arrow functions: (a, { b }) => and a =>
  for (const match of code.matchAll(/=>/g)) {
    let i = match.index - 1
    while (i >= 0 && /\s/.test(code[i])) i--
    if (code[i] === ')') {
      let depth = 0
      let open = i
      for (; open >= 0; open--) {
        if (')}]'.includes(code[open])) depth++
        else if ('({['.includes(code[open]) && --depth === 0) break
      }
      if (open >= 0) bound(code.slice(open + 1, i), names)
    } else {
      const single = code.slice(0, i + 1).match(/([A-Za-z_$][\w$]*)$/)
      if (single) names.add(single[1])
    }
  }

  for (const match of code.matchAll(/\bcatch\s*\(\s*([A-Za-z_$][\w$]*)\s*\)/g)) names.add(match[1])
  for (const match of code.matchAll(/\bclass\s+([A-Za-z_$][\w$]*)/g)) names.add(match[1])

  return names
}

// Calls to a bare name: not a method (.name), not part of a longer word, not an optional call.
function undeclaredCalls (text) {
  const code = codeOnly(text)
  const declared = declaredNames(code)
  const found = new Map()

  code.split('\n').forEach((line, index) => {
    for (const match of line.matchAll(/(?<![.\w$?])([A-Za-z_$][\w$]*)\s*\(/g)) {
      const name = match[1]
      if (KEYWORDS.has(name) || GLOBALS.has(name) || declared.has(name)) continue
      // A method written in an object or a class: "name (args) {" on its own line.
      if (new RegExp(`^\\s*(?:async\\s+)?${name.replace(/\$/g, '\\$')}\\s*\\([^)]*\\)\\s*\\{\\s*$`).test(line)) continue
      if (!found.has(name)) found.set(name, [])
      found.get(name).push(index + 1)
    }
  })

  return { declared: declared.size, found }
}

let passed = 0
let failed = 0

function check (name, actual, expected) {
  const a = JSON.stringify(actual)
  const e = JSON.stringify(expected)
  if (a === e) {
    passed++
  } else {
    failed++
    console.log(`FAIL: ${name}\n  expected ${e}\n  got      ${a}`)
  }
}

const namesIn = (text) => [...undeclaredCalls(text).found.keys()]

// ---------------------------------------------------------------------------
// Part 1 - the check finds what it is for, and leaves alone what it should.

check('a call to a name that exists nowhere is found', namesIn('function a () { return loadToken() }\n'), ['loadToken'])
check('require is such a name', namesIn("function a () { const { x } = require('node:fs'); return x }\n"), ['require'])
check('a function declared later in the file is fine', namesIn('function a () { return b() }\nfunction b () { return 1 }\n'), [])
check('an import is fine', namesIn("import { execSync } from 'node:child_process'\nexecSync('x')\n"), [])
check('an import under another name is fine', namesIn("import { a as b } from 'x'\nb()\n"), [])
check('a parameter is fine', namesIn('function a (callback) { return callback() }\n'), [])
check('a parameter with a default that has brackets of its own is fine',
  namesIn('function a (client, { log = () => {}, sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms)) } = {}) { log(); return sleep(1) }\n'), [])
check('a destructured name is fine', namesIn('const { run, stop: halt } = tools\nrun(); halt()\n'), [])
check('an arrow parameter is fine', namesIn('const each = (items, visit) => items.forEach((item) => visit(item))\n'), [])
check('a method call is not a bare call', namesIn('const x = thing.missing()\n'), [])
check('a global is fine', namesIn('setTimeout(() => {}, 1); new URL("a:b")\n'), [])
check('a name inside a comment is not a call', namesIn('// was loadToken() once\nconst a = 1 /* and saveToken() */\n'), [])
check('a name inside a string is not a call', namesIn("const a = 'call loadToken() here'\n"), [])
check('a name inside a regular expression is not a call', namesIn('const a = /loadToken\\(/.test(b)\nconst b = 1\n'), [])
check('code inside a template is still code', namesIn('const a = `x ${loadToken()} y`\n'), ['loadToken'])
check('text inside a template is not', namesIn('const a = `call loadToken() here`\n'), [])
check('the line of the call is reported', undeclaredCalls('const a = 1\n\nconst b = gone()\n').found.get('gone'), [3])

// ---------------------------------------------------------------------------
// Part 2 - the modules the launcher ships.

const modules = readdirSync(here)
  .filter((name) => /\.(js|mjs)$/.test(name) && !name.endsWith('.selftest.mjs'))
  .sort()

check('main.js, manifest.js and preload.mjs are among the modules this reads',
  ['main.js', 'manifest.js', 'preload.mjs'].every((name) => modules.includes(name)), true)

for (const name of modules) {
  const text = readFileSync(path.join(here, name), 'utf8').split('\r\n').join('\n')
  const { declared, found } = undeclaredCalls(text)

  // A check that sees no declarations is reading the wrong thing, not a clean file.
  check(`${name}: the check can see the names it declares`, declared > 0, true)

  if (found.size === 0) {
    passed++
    continue
  }

  failed++
  console.log(`FAIL: ${name} calls ${found.size} name(s) it never declares`)
  for (const [called, lines] of found) console.log(`  ${called}()  ${name}:${lines.slice(0, 8).join(', ')}${lines.length > 8 ? ', ...' : ''}`)
  console.log('  what:  the name is looked up only when the line runs, so this parses, passes')
  console.log('         node --check, and throws a ReferenceError on a player\'s machine - silently, if')
  console.log('         it sits in a try.')
  console.log('  fix:   declare or import it, or call what it was renamed to. For require: import the')
  console.log("         name at the top of the file (import { execSync } from 'node:child_process').")
  console.log('         If it really is a global the runtime provides, add it to GLOBALS in')
  console.log('         undeclared.selftest.mjs with a word on which runtime.')
}

console.log(`${passed} passed, ${failed} failed`)
process.exit(failed ? 1 : 0)
