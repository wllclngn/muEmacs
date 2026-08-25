/*
 * languages.c - Built-in language definitions
 *
 * μEmacs v1.0
 *
 * Defines keywords, types, constants, and syntax rules for:
 * - C-family: C, C++, Zig, Go, Rust, JavaScript, TypeScript, Java
 * - Scripting: Python, Bash, Lua
 * - Data: SQL, Markdown, TOML, YAML, JSON
 */

#include "internal/syntax.h"

/* C Language */

static const char *c_extensions[] = {".c", ".h", nullptr};

static const char *c_keywords[] = {
    "auto", "break", "case", "const", "continue", "default", "do", "else",
    "enum", "extern", "for", "goto", "if", "inline", "register", "restrict",
    "return", "sizeof", "static", "struct", "switch", "typedef", "union",
    "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool",
    "_Complex", "_Generic", "_Imaginary", "_Noreturn", "_Static_assert",
    "_Thread_local", "alignas", "alignof", "bool", "static_assert",
    "thread_local", "typeof", "typeof_unqual",
    nullptr
};

static const char *c_types[] = {
    "char", "double", "float", "int", "long", "short", "signed", "unsigned",
    "void", "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "FILE", "NULL",
    nullptr
};

static const char *c_constants[] = {
    "NULL", "true", "false", "EOF", "stdin", "stdout", "stderr",
    "EXIT_SUCCESS", "EXIT_FAILURE",
    nullptr
};

const syntax_language_t lang_c = {
    .name = "c",
    .extensions = c_extensions,
    .keywords = c_keywords,
    .types = c_types,
    .constants = c_constants,
    .builtins = nullptr,
    .line_comment = "//",
    .block_start = "/*",
    .block_end = "*/",
    .nested_comments = false,
    .has_single_quote_str = true,   /* char literals */
    .has_double_quote_str = true,
    .has_backtick_str = false,
    .has_raw_strings = false,
    .has_triple_quote = false,
    .preprocessor = "#",
    .case_sensitive = true,
    .custom_lexer = nullptr,
};

/* C++ Language */

static const char *cpp_extensions[] = {".cpp", ".cc", ".cxx", ".hpp", ".hxx", ".h++", nullptr};

static const char *cpp_keywords[] = {
    /* C keywords */
    "auto", "break", "case", "const", "continue", "default", "do", "else",
    "enum", "extern", "for", "goto", "if", "inline", "register", "return",
    "sizeof", "static", "struct", "switch", "typedef", "union", "volatile",
    "while",
    /* C++ specific */
    "alignas", "alignof", "and", "and_eq", "asm", "bitand", "bitor", "catch",
    "class", "compl", "concept", "consteval", "constexpr", "constinit",
    "const_cast", "co_await", "co_return", "co_yield", "decltype", "delete",
    "dynamic_cast", "explicit", "export", "friend", "mutable", "namespace",
    "new", "noexcept", "not", "not_eq", "NULL", "operator", "or", "or_eq",
    "private", "protected", "public", "reinterpret_cast", "requires",
    "static_assert", "static_cast", "template", "this", "throw", "try",
    "typeid", "typename", "using", "virtual", "xor", "xor_eq", "override",
    "final",
    nullptr
};

static const char *cpp_types[] = {
    "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float",
    "int", "long", "short", "signed", "unsigned", "void", "wchar_t",
    "string", "vector", "map", "set", "list", "deque", "array", "pair",
    "tuple", "optional", "variant", "any", "span", "string_view",
    "unique_ptr", "shared_ptr", "weak_ptr",
    nullptr
};

const syntax_language_t lang_cpp = {
    .name = "cpp",
    .extensions = cpp_extensions,
    .keywords = cpp_keywords,
    .types = cpp_types,
    .constants = c_constants,
    .builtins = nullptr,
    .line_comment = "//",
    .block_start = "/*",
    .block_end = "*/",
    .nested_comments = false,
    .has_single_quote_str = true,
    .has_double_quote_str = true,
    .has_backtick_str = false,
    .has_raw_strings = true,
    .raw_string_prefix = 'R',
    .has_triple_quote = false,
    .preprocessor = "#",
    .case_sensitive = true,
    .custom_lexer = nullptr,
};

/* Zig Language */

static const char *zig_extensions[] = {".zig", nullptr};

static const char *zig_keywords[] = {
    "addrspace", "align", "allowzero", "and", "anyframe", "anytype", "asm",
    "async", "await", "break", "callconv", "catch", "comptime", "const",
    "continue", "defer", "else", "enum", "errdefer", "error", "export",
    "extern", "fn", "for", "if", "inline", "linksection", "noalias",
    "noinline", "nosuspend", "opaque", "or", "orelse", "packed", "pub",
    "resume", "return", "struct", "suspend", "switch", "test", "threadlocal",
    "try", "union", "unreachable", "usingnamespace", "var", "volatile",
    "while",
    nullptr
};

static const char *zig_types[] = {
    "bool", "f16", "f32", "f64", "f80", "f128", "c_longdouble",
    "i8", "i16", "i32", "i64", "i128", "isize",
    "u8", "u16", "u32", "u64", "u128", "usize",
    "c_char", "c_short", "c_ushort", "c_int", "c_uint", "c_long", "c_ulong",
    "c_longlong", "c_ulonglong", "comptime_int", "comptime_float",
    "anyerror", "anyopaque", "anytype", "noreturn", "type", "void",
    nullptr
};

static const char *zig_constants[] = {
    "null", "undefined", "true", "false",
    nullptr
};

static const char *zig_builtins[] = {
    "@This", "@import", "@cImport", "@cInclude", "@embedFile", "@src",
    "@typeInfo", "@TypeOf", "@typeName", "@alignOf", "@sizeOf", "@bitSizeOf",
    "@intFromPtr", "@ptrFromInt", "@intFromFloat", "@floatFromInt",
    "@intCast", "@floatCast", "@ptrCast", "@alignCast", "@truncate",
    "@min", "@max", "@abs", "@sqrt", "@log", "@exp", "@sin", "@cos",
    "@memcpy", "@memset", "@atomicLoad", "@atomicStore", "@cmpxchg",
    "@panic", "@compileError", "@compileLog",
    nullptr
};

const syntax_language_t lang_zig = {
    .name = "zig",
    .extensions = zig_extensions,
    .keywords = zig_keywords,
    .types = zig_types,
    .constants = zig_constants,
    .builtins = zig_builtins,
    .line_comment = "//",
    .block_start = nullptr,  /* Zig has no block comments */
    .block_end = nullptr,
    .nested_comments = false,
    .has_single_quote_str = true,   /* char literals */
    .has_double_quote_str = true,
    .has_backtick_str = false,
    .has_raw_strings = false,
    .has_triple_quote = false,
    .preprocessor = nullptr,
    .case_sensitive = true,
    .custom_lexer = nullptr,
};

/* Go Language */

static const char *go_extensions[] = {".go", nullptr};

static const char *go_keywords[] = {
    "break", "case", "chan", "const", "continue", "default", "defer", "else",
    "fallthrough", "for", "func", "go", "goto", "if", "import", "interface",
    "map", "package", "range", "return", "select", "struct", "switch", "type",
    "var",
    nullptr
};

static const char *go_types[] = {
    "bool", "byte", "complex64", "complex128", "error", "float32", "float64",
    "int", "int8", "int16", "int32", "int64", "rune", "string",
    "uint", "uint8", "uint16", "uint32", "uint64", "uintptr",
    nullptr
};

static const char *go_constants[] = {
    "true", "false", "nil", "iota",
    nullptr
};

static const char *go_builtins[] = {
    "append", "cap", "close", "complex", "copy", "delete", "imag", "len",
    "make", "new", "panic", "print", "println", "real", "recover",
    nullptr
};

const syntax_language_t lang_go = {
    .name = "go",
    .extensions = go_extensions,
    .keywords = go_keywords,
    .types = go_types,
    .constants = go_constants,
    .builtins = go_builtins,
    .line_comment = "//",
    .block_start = "/*",
    .block_end = "*/",
    .nested_comments = false,
    .has_single_quote_str = true,   /* rune literals */
    .has_double_quote_str = true,
    .has_backtick_str = true,       /* raw strings */
    .has_raw_strings = false,
    .has_triple_quote = false,
    .preprocessor = nullptr,
    .case_sensitive = true,
    .custom_lexer = nullptr,
};

/* Rust Language */

static const char *rust_extensions[] = {".rs", nullptr};

static const char *rust_keywords[] = {
    "as", "async", "await", "break", "const", "continue", "crate", "dyn",
    "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in",
    "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return",
    "self", "Self", "static", "struct", "super", "trait", "true", "type",
    "unsafe", "use", "where", "while",
    nullptr
};

static const char *rust_types[] = {
    "bool", "char", "f32", "f64", "i8", "i16", "i32", "i64", "i128", "isize",
    "str", "u8", "u16", "u32", "u64", "u128", "usize",
    "Box", "Cell", "Option", "Result", "Rc", "Arc", "RefCell", "Mutex",
    "String", "Vec", "HashMap", "HashSet", "BTreeMap", "BTreeSet",
    nullptr
};

static const char *rust_constants[] = {
    "true", "false", "None", "Some", "Ok", "Err",
    nullptr
};

static const char *rust_builtins[] = {
    "println", "print", "eprintln", "eprint", "format", "panic", "assert",
    "assert_eq", "assert_ne", "debug_assert", "todo", "unimplemented",
    "unreachable", "vec", "include", "include_str", "include_bytes",
    nullptr
};

const syntax_language_t lang_rust = {
    .name = "rust",
    .extensions = rust_extensions,
    .keywords = rust_keywords,
    .types = rust_types,
    .constants = rust_constants,
    .builtins = rust_builtins,
    .line_comment = "//",
    .block_start = "/*",
    .block_end = "*/",
    .nested_comments = true,   /* Rust supports nested block comments */
    .has_single_quote_str = true,   /* char literals */
    .has_double_quote_str = true,
    .has_backtick_str = false,
    .has_raw_strings = true,
    .raw_string_prefix = 'r',
    .has_triple_quote = false,
    .preprocessor = nullptr,
    .case_sensitive = true,
    .custom_lexer = nullptr,
};

/* JavaScript Language */

static const char *js_extensions[] = {".js", ".mjs", ".cjs", ".jsx", nullptr};

static const char *js_keywords[] = {
    "async", "await", "break", "case", "catch", "class", "const", "continue",
    "debugger", "default", "delete", "do", "else", "export", "extends",
    "finally", "for", "function", "if", "import", "in", "instanceof", "let",
    "new", "of", "return", "static", "super", "switch", "this", "throw",
    "try", "typeof", "var", "void", "while", "with", "yield",
    nullptr
};

static const char *js_types[] = {
    "Array", "ArrayBuffer", "BigInt", "Boolean", "DataView", "Date", "Error",
    "Float32Array", "Float64Array", "Function", "Int8Array", "Int16Array",
    "Int32Array", "Map", "Number", "Object", "Promise", "Proxy", "RegExp",
    "Set", "SharedArrayBuffer", "String", "Symbol", "TypedArray",
    "Uint8Array", "Uint8ClampedArray", "Uint16Array", "Uint32Array",
    "WeakMap", "WeakSet",
    nullptr
};

static const char *js_constants[] = {
    "true", "false", "null", "undefined", "NaN", "Infinity",
    nullptr
};

static const char *js_builtins[] = {
    "console", "document", "window", "globalThis", "process",
    "setTimeout", "setInterval", "clearTimeout", "clearInterval",
    "fetch", "require", "module", "exports",
    nullptr
};

const syntax_language_t lang_javascript = {
    .name = "javascript",
    .extensions = js_extensions,
    .keywords = js_keywords,
    .types = js_types,
    .constants = js_constants,
    .builtins = js_builtins,
    .line_comment = "//",
    .block_start = "/*",
    .block_end = "*/",
    .nested_comments = false,
    .has_single_quote_str = true,
    .has_double_quote_str = true,
    .has_backtick_str = true,   /* template literals */
    .has_raw_strings = false,
    .has_triple_quote = false,
    .preprocessor = nullptr,
    .case_sensitive = true,
    .custom_lexer = nullptr,
};

/* TypeScript Language */

static const char *ts_extensions[] = {".ts", ".tsx", ".mts", ".cts", nullptr};

static const char *ts_keywords[] = {
    /* JS keywords + TS specific */
    "abstract", "any", "as", "asserts", "async", "await", "bigint", "boolean",
    "break", "case", "catch", "class", "const", "constructor", "continue",
    "debugger", "declare", "default", "delete", "do", "else", "enum",
    "export", "extends", "false", "finally", "for", "from", "function",
    "get", "if", "implements", "import", "in", "infer", "instanceof",
    "interface", "is", "keyof", "let", "module", "namespace", "never", "new",
    "null", "number", "object", "of", "override", "package", "private",
    "protected", "public", "readonly", "require", "return", "set", "static",
    "string", "super", "switch", "symbol", "this", "throw", "true", "try",
    "type", "typeof", "undefined", "unique", "unknown", "var", "void",
    "while", "with", "yield",
    nullptr
};

const syntax_language_t lang_typescript = {
    .name = "typescript",
    .extensions = ts_extensions,
    .keywords = ts_keywords,
    .types = js_types,
    .constants = js_constants,
    .builtins = js_builtins,
    .line_comment = "//",
    .block_start = "/*",
    .block_end = "*/",
    .nested_comments = false,
    .has_single_quote_str = true,
    .has_double_quote_str = true,
    .has_backtick_str = true,
    .has_raw_strings = false,
    .has_triple_quote = false,
    .preprocessor = nullptr,
    .case_sensitive = true,
    .custom_lexer = nullptr,
};

/* Java Language */

static const char *java_extensions[] = {".java", nullptr};

static const char *java_keywords[] = {
    "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char",
    "class", "const", "continue", "default", "do", "double", "else", "enum",
    "extends", "final", "finally", "float", "for", "goto", "if", "implements",
    "import", "instanceof", "int", "interface", "long", "native", "new",
    "package", "private", "protected", "public", "return", "short", "static",
    "strictfp", "super", "switch", "synchronized", "this", "throw", "throws",
    "transient", "try", "void", "volatile", "while", "var", "yield", "record",
    "sealed", "permits", "non-sealed",
    nullptr
};

static const char *java_types[] = {
    "Boolean", "Byte", "Character", "Class", "Double", "Enum", "Float",
    "Integer", "Long", "Number", "Object", "Short", "String", "StringBuffer",
    "StringBuilder", "System", "Thread", "Throwable", "Void",
    "ArrayList", "HashMap", "HashSet", "LinkedList", "List", "Map", "Set",
    "Optional", "Stream", "Comparable", "Comparator", "Iterator", "Iterable",
    nullptr
};

static const char *java_constants[] = {
    "true", "false", "null",
    nullptr
};

const syntax_language_t lang_java = {
    .name = "java",
    .extensions = java_extensions,
    .keywords = java_keywords,
    .types = java_types,
    .constants = java_constants,
    .builtins = nullptr,
    .line_comment = "//",
    .block_start = "/*",
    .block_end = "*/",
    .nested_comments = false,
    .has_single_quote_str = true,  /* char literals */
    .has_double_quote_str = true,
    .has_backtick_str = false,
    .has_raw_strings = false,
    .has_triple_quote = true,      /* Java 15+ text blocks */
    .preprocessor = nullptr,
    .case_sensitive = true,
    .custom_lexer = nullptr,
};

/* Python Language */

static const char *python_extensions[] = {".py", ".pyw", ".pyi", nullptr};

static const char *python_keywords[] = {
    "False", "None", "True", "and", "as", "assert", "async", "await", "break",
    "class", "continue", "def", "del", "elif", "else", "except", "finally",
    "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal",
    "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",
    "match", "case", "type",
    nullptr
};

static const char *python_types[] = {
    "int", "float", "complex", "str", "bytes", "bytearray", "bool", "list",
    "tuple", "set", "frozenset", "dict", "range", "slice", "object", "type",
    "NoneType", "Callable", "Iterable", "Iterator", "Generator", "Sequence",
    "Mapping", "MutableMapping", "Set", "MutableSet", "Optional", "Union",
    "Any", "TypeVar", "Generic", "Protocol", "Final", "Literal", "ClassVar",
    nullptr
};

static const char *python_builtins[] = {
    "abs", "all", "any", "ascii", "bin", "bool", "breakpoint", "bytearray",
    "bytes", "callable", "chr", "classmethod", "compile", "complex", "delattr",
    "dict", "dir", "divmod", "enumerate", "eval", "exec", "filter", "float",
    "format", "frozenset", "getattr", "globals", "hasattr", "hash", "help",
    "hex", "id", "input", "int", "isinstance", "issubclass", "iter", "len",
    "list", "locals", "map", "max", "memoryview", "min", "next", "object",
    "oct", "open", "ord", "pow", "print", "property", "range", "repr",
    "reversed", "round", "set", "setattr", "slice", "sorted", "staticmethod",
    "str", "sum", "super", "tuple", "type", "vars", "zip", "__import__",
    nullptr
};

const syntax_language_t lang_python = {
    .name = "python",
    .extensions = python_extensions,
    .keywords = python_keywords,
    .types = python_types,
    .constants = nullptr,
    .builtins = python_builtins,
    .line_comment = "#",
    .block_start = nullptr,
    .block_end = nullptr,
    .nested_comments = false,
    .has_single_quote_str = true,
    .has_double_quote_str = true,
    .has_backtick_str = false,
    .has_raw_strings = true,
    .raw_string_prefix = 'r',
    .has_triple_quote = true,
    .preprocessor = nullptr,
    .case_sensitive = true,
    .custom_lexer = nullptr,
};

/* Bash Language */

static const char *bash_extensions[] = {".sh", ".bash", ".zsh", ".fish", nullptr};

static const char *bash_keywords[] = {
    "case", "do", "done", "elif", "else", "esac", "fi", "for", "function",
    "if", "in", "select", "then", "time", "until", "while", "coproc",
    nullptr
};

static const char *bash_builtins[] = {
    "alias", "bg", "bind", "break", "builtin", "caller", "cd", "command",
    "compgen", "complete", "compopt", "continue", "declare", "dirs",
    "disown", "echo", "enable", "eval", "exec", "exit", "export", "false",
    "fc", "fg", "getopts", "hash", "help", "history", "jobs", "kill", "let",
    "local", "logout", "mapfile", "popd", "printf", "pushd", "pwd", "read",
    "readarray", "readonly", "return", "set", "shift", "shopt", "source",
    "suspend", "test", "times", "trap", "true", "type", "typeset", "ulimit",
    "umask", "unalias", "unset", "wait",
    nullptr
};

const syntax_language_t lang_bash = {
    .name = "bash",
    .extensions = bash_extensions,
    .keywords = bash_keywords,
    .types = nullptr,
    .constants = nullptr,
    .builtins = bash_builtins,
    .line_comment = "#",
    .block_start = nullptr,
    .block_end = nullptr,
    .nested_comments = false,
    .has_single_quote_str = true,
    .has_double_quote_str = true,
    .has_backtick_str = true,   /* command substitution (legacy) */
    .has_raw_strings = false,
    .has_triple_quote = false,
    .preprocessor = nullptr,
    .case_sensitive = true,
    .custom_lexer = nullptr,
};

/* Lua Language */

static const char *lua_extensions[] = {".lua", nullptr};

static const char *lua_keywords[] = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
    "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
    "then", "true", "until", "while",
    nullptr
};

static const char *lua_types[] = {
    "string", "number", "boolean", "table", "function", "thread", "userdata",
    nullptr
};

static const char *lua_builtins[] = {
    "assert", "collectgarbage", "dofile", "error", "getmetatable", "ipairs",
    "load", "loadfile", "next", "pairs", "pcall", "print", "rawequal",
    "rawget", "rawlen", "rawset", "require", "select", "setmetatable",
    "tonumber", "tostring", "type", "xpcall", "_G", "_VERSION",
    nullptr
};

const syntax_language_t lang_lua = {
    .name = "lua",
    .extensions = lua_extensions,
    .keywords = lua_keywords,
    .types = lua_types,
    .constants = nullptr,
    .builtins = lua_builtins,
    .line_comment = "--",
    .block_start = "--[[",
    .block_end = "]]",
    .nested_comments = false,
    .has_single_quote_str = true,
    .has_double_quote_str = true,
    .has_backtick_str = false,
    .has_raw_strings = false,      /* Lua uses [[ ]] for long strings */
    .has_triple_quote = false,
    .preprocessor = nullptr,
    .case_sensitive = true,
    .custom_lexer = nullptr,
};

/* SQL Language */

static const char *sql_extensions[] = {".sql", nullptr};

static const char *sql_keywords[] = {
    "ADD", "ALL", "ALTER", "AND", "AS", "ASC", "BETWEEN", "BY", "CASE",
    "CHECK", "COLUMN", "CONSTRAINT", "CREATE", "CROSS", "DATABASE", "DEFAULT",
    "DELETE", "DESC", "DISTINCT", "DROP", "ELSE", "END", "EXISTS", "FOREIGN",
    "FROM", "FULL", "GROUP", "HAVING", "IF", "IN", "INDEX", "INNER", "INSERT",
    "INTO", "IS", "JOIN", "KEY", "LEFT", "LIKE", "LIMIT", "NOT", "NULL", "ON",
    "OR", "ORDER", "OUTER", "PRIMARY", "REFERENCES", "RIGHT", "SELECT", "SET",
    "TABLE", "THEN", "UNION", "UNIQUE", "UPDATE", "VALUES", "VIEW", "WHEN",
    "WHERE", "WITH",
    nullptr
};

static const char *sql_types[] = {
    "BIGINT", "BINARY", "BIT", "BLOB", "BOOLEAN", "CHAR", "CLOB", "DATE",
    "DATETIME", "DECIMAL", "DOUBLE", "FLOAT", "INT", "INTEGER", "INTERVAL",
    "NCHAR", "NVARCHAR", "NUMERIC", "REAL", "SMALLINT", "TEXT", "TIME",
    "TIMESTAMP", "TINYINT", "VARBINARY", "VARCHAR",
    nullptr
};

static const char *sql_builtins[] = {
    "AVG", "COUNT", "MAX", "MIN", "SUM", "ABS", "CEIL", "FLOOR", "ROUND",
    "CONCAT", "LENGTH", "LOWER", "UPPER", "SUBSTRING", "TRIM", "COALESCE",
    "NULLIF", "CAST", "CONVERT", "NOW", "CURRENT_DATE", "CURRENT_TIME",
    nullptr
};

const syntax_language_t lang_sql = {
    .name = "sql",
    .extensions = sql_extensions,
    .keywords = sql_keywords,
    .types = sql_types,
    .constants = nullptr,
    .builtins = sql_builtins,
    .line_comment = "--",
    .block_start = "/*",
    .block_end = "*/",
    .nested_comments = false,
    .has_single_quote_str = true,
    .has_double_quote_str = false,  /* double quotes for identifiers */
    .has_backtick_str = true,       /* MySQL identifier quotes */
    .has_raw_strings = false,
    .has_triple_quote = false,
    .preprocessor = nullptr,
    .case_sensitive = false,        /* SQL keywords are case-insensitive */
    .custom_lexer = nullptr,
};

/* Markdown (simplified - headings, code blocks) */

static const char *md_extensions[] = {".md", ".markdown", nullptr};

const syntax_language_t lang_markdown = {
    .name = "markdown",
    .extensions = md_extensions,
    .keywords = nullptr,
    .types = nullptr,
    .constants = nullptr,
    .builtins = nullptr,
    .line_comment = nullptr,
    .block_start = nullptr,
    .block_end = nullptr,
    .nested_comments = false,
    .has_single_quote_str = false,
    .has_double_quote_str = false,
    .has_backtick_str = true,   /* inline code */
    .has_raw_strings = false,
    .has_triple_quote = true,   /* code blocks */
    .preprocessor = "#",        /* headings */
    .case_sensitive = true,
    .custom_lexer = nullptr,       /* TODO: custom lexer for MD */
};

/* TOML Language */

static const char *toml_extensions[] = {".toml", nullptr};

static const char *toml_keywords[] = {
    "true", "false",
    nullptr
};

const syntax_language_t lang_toml = {
    .name = "toml",
    .extensions = toml_extensions,
    .keywords = toml_keywords,
    .types = nullptr,
    .constants = nullptr,
    .builtins = nullptr,
    .line_comment = "#",
    .block_start = nullptr,
    .block_end = nullptr,
    .nested_comments = false,
    .has_single_quote_str = true,
    .has_double_quote_str = true,
    .has_backtick_str = false,
    .has_raw_strings = false,
    .has_triple_quote = true,   /* multiline strings */
    .preprocessor = nullptr,
    .case_sensitive = true,
    .custom_lexer = nullptr,
};

/* YAML Language */

static const char *yaml_extensions[] = {".yaml", ".yml", nullptr};

static const char *yaml_keywords[] = {
    "true", "false", "yes", "no", "on", "off", "null", "~",
    nullptr
};

const syntax_language_t lang_yaml = {
    .name = "yaml",
    .extensions = yaml_extensions,
    .keywords = yaml_keywords,
    .types = nullptr,
    .constants = nullptr,
    .builtins = nullptr,
    .line_comment = "#",
    .block_start = nullptr,
    .block_end = nullptr,
    .nested_comments = false,
    .has_single_quote_str = true,
    .has_double_quote_str = true,
    .has_backtick_str = false,
    .has_raw_strings = false,
    .has_triple_quote = false,
    .preprocessor = nullptr,
    .case_sensitive = false,    /* YAML booleans are case-insensitive */
    .custom_lexer = nullptr,
};

/* JSON Language */

static const char *json_extensions[] = {".json", ".jsonc", nullptr};

static const char *json_keywords[] = {
    "true", "false", "null",
    nullptr
};

const syntax_language_t lang_json = {
    .name = "json",
    .extensions = json_extensions,
    .keywords = json_keywords,
    .types = nullptr,
    .constants = nullptr,
    .builtins = nullptr,
    .line_comment = "//",       /* JSONC supports comments */
    .block_start = "/*",
    .block_end = "*/",
    .nested_comments = false,
    .has_single_quote_str = false,
    .has_double_quote_str = true,
    .has_backtick_str = false,
    .has_raw_strings = false,
    .has_triple_quote = false,
    .preprocessor = nullptr,
    .case_sensitive = true,
    .custom_lexer = nullptr,
};
