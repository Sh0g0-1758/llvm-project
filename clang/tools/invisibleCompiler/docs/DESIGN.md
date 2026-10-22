# invisible: design

`invisible` rewrites a C or C++ translation unit at the source level so that the inlining an
optimizing compiler performs can be replayed in source code. This document explains what the
tool produces, the rules every pass follows, and what each pass does, with the tool's actual
output as examples. Read it before the code: every file maps to a section here.

It describes monomorphization as of v17 (12 October 2026) with the fixes ported onto it, built
standalone against a production LLVM (section 12).

Contents

1. [The goal](#1-the-goal)
2. [Pipeline at a glance](#2-pipeline-at-a-glance)
3. [Invariants](#3-invariants)
4. [The driver: running, checking and recovering](#4-the-driver-running-checking-and-recovering)
5. [The passes](#5-the-passes)
6. [decl-before-def in depth](#6-decl-before-def-in-depth)
7. [Monomorphization](#7-monomorphization)
8. [The inliner](#8-the-inliner)
9. [Testing](#9-testing)
10. [Limitations and accepted differences](#10-limitations-and-accepted-differences)
11. [Code map](#11-code-map)
12. [Building](#12-building)

---

## 1. The goal

The product is the **deferred definitions**: every function that ends up in the binary,
written out concretely at the end of the translation unit (TU). Templates are written out
per instantiation. The optimization records of `clang -O2` say which calls were inlined
where, and `invisible inline` replays that inlining in those definitions.

Everything else, the preprocessing passes included, exists to make that possible:

- A call must be a **statement of its own** whose result lands in a named variable. Then
  the inliner can paste a body in front of it without reasoning about evaluation order.
- Every called function must have a **body in the text**, which the inliner can copy. An
  instantiation such as `std::vector<int>::size()` has no text of its own until it is written
  out, as a free function of its own (a copy, section 7) or as a specialization.
- Every function must be **declared before any definition**. Then a pasted body can call
  anything.

```
 input.cc --> preprocess (14 passes) --> pre.cc --> clang -O2 -fsave-optimization-record
                                            |                         |
                                            |                    opt.yaml --> oracle.txt
                                            v                                    |
                                   invisible inline <----------------------------+
                                            |
                                            v
                                       inlined.cc
```

The oracle is one line per inlined call: `caller callee line`, with functions
named by their mangled names (see section 8).

## 2. Pipeline at a glance

`invisible preprocess input.cc -x c++ -std=c++23` runs these passes in order. Each one
parses the previous one's output from scratch, edits it as text through a `clang::Rewriter`,
and hands the result on. `invisible --list-passes` prints the same list.

| # | Pass | What it does | Repeats |
|---|------|--------------|---------|
| 1 | insert-braces | `if (c) x;` => `if (c) { x; }`; drops `extern template` | |
| 2 | access-specifier | every member and base becomes `public` | |
| 3 | name-template-type-parm | `template <class = void>` => `template <class __type_param__0 = void>` | |
| 4 | materialize-lambda | a lambda becomes a named struct | yes |
| 5 | decl-before-def | declarations first, every definition at the end, instantiations specialized | |
| 6 | compound-stmt-wrapper | `for`, `if` and `switch` are wrapped in `{ }`, and the body of a `switch` | |
| 7 | mark-switch-case | `case 1: f();` => `case 1: ; f();` | |
| 8 | paren-expr-wrapper | `a, b` => `(a, b)` | |
| 9 | disassemble-cxx-for-range-stmt | `for (x : r)` => an iterator loop | |
| 10 | loop-to-ir | a loop condition with a call becomes `if (!(cond)) break;` | |
| 11 | comma-init | `int a = 1, b = f();` => one declaration each | |
| 12 | short-circuit-to-ir | `&&`, `||` and  `?:` with calls become `if`s | yes |
| 13 | constant-fold | constant expressions  become literals | yes |
| 14 | hoist-calls | every nested call moves into a temporary of its own | yes |

Passes 6-14 run on what decl-before-def wrote, and each prepares the code for the last one,
hoist-calls. A repeating pass runs until it changes nothing.

Every pass only looks at C++ constructs when the input is C++. A C input runs the same
pipeline; monomorphization never applies to it.

## 3. Invariants

These hold after **every** pass, not just at the end. The driver checks them, and the
regression tests check the final output.

1. **It compiles, as a compiler builds it.** The driver compiles each pass's output and
   rejects it if it doesn't compile. The check is as strict as `clang -c -w
   -Wno-out-of-line-declaration`: warnings that clang makes errors by default, such as
   `-Winvalid-constexpr`, stay errors (`checkAsCompiled`). An input that compiles only with
   every warning silenced, such as C that passes incompatible pointers, is checked leniently.
   `-Wno-out-of-line-declaration` is needed because decl-before-def redeclares members
   outside their class (`int Vec::size();`), which is otherwise a default error.
   `-Wno-unused-command-line-argument` keeps the driver from warning that a syntax check
   doesn't use `-c`.
2. **It links where the input links.** The output must define everything the input defines,
   and it must not define anything twice across the TUs of a program. Definitions that came
   from implicitly inline functions (in-class members, instantiations) are printed
   `[[gnu::used]] inline`: weak, so duplicates across TUs merge, and `used`, so each one is
   still emitted for the optimization records.
3. **It means the same.** A program prints and returns what the original does. The only
   accepted differences are the object lifetimes that hoisting changes (section 10).
4. **Templates and friends stay as written** where they are needed as written. A
   dependent context is never rewritten by a pass after decl-before-def
   (`isLeftAsWritten`), and friends are left exactly as written.

## 4. The driver: running, checking and recovering

`Pipeline` in `invisible.cpp` runs the passes. `run()` goes through them, and
`runIteration()` runs one iteration of a pass and checks what it wrote. Most of the logic is
recovery: a pass whose output doesn't compile isn't simply a failure, and the driver tries
three things before giving up, from the most precise to the most general.

### 4.1 Reruns from decl-before-def

decl-before-def decides, once, which templates keep their text and which are reduced to
declarations. A later pass can write code that this decision didn't foresee. For example,
short-circuit-to-ir turns `dir d(c ? s.sub(1) : s)` into an assignment, and the assignment
instantiates `operator=(str&&) noexcept(traits<T>::nothrow())`. That needs the body of
`nothrow()`, which decl-before-def stripped.

When a pass's errors show that (`neededAsWritten`), the driver adds what's needed to
`laterUnmoved()` and reruns everything from decl-before-def. Two kinds of error count:

- a note that points at the declaration of a **template decl-before-def stripped**
  (`declared here`, `declaration of template entity is here`);
- an **explicit specialization after instantiation**, reported on the specialization's
  definition: the new code instantiates a function before the deferred specialization
  declares it.

After the last pass, the driver also looks for instantiations that the output **uses but
doesn't define** whose template was stripped (`strippedTemplatesUsedIn`). This happens, for
example, when hoist-calls writes `bool b(ptr);` for the variable of `if (auto ptr = ...)`,
which calls a conversion that was copied away. This is an AST check, not a diagnostic.
clang reports only the first missing template definition, and only while no other error
has occurred, so `-Werror=undefined-func-template` can miss all but one.

At most 100 functions are left as written this way.

### 4.2 Retries inside decl-before-def

decl-before-def prints a lot of code. When some of it doesn't compile, the pass itself
retries (up to 20 attempts), attributing each error to the deferred definition or copy whose
lines it falls in:

- a deferred **specialization** that doesn't compile is left as written instead (`unmoved`);
- a **copy** (section 7) that doesn't compile is dropped (`dropped`), and its callers call the
  original again;
- an instantiation the output uses without a definition, whose template was stripped, has its
  template left as written (`strippedTemplatesUsedIn`, as in section 4.1).

Both are fallbacks: they keep the TU compiling at the cost of a function that the inliner
can't inline into. The verbose log reports them as `attempt N left X as written and Y
copies out`.

### 4.3 Per-function fallback

For a pass after decl-before-def, the functions around the remaining errors are left as
written by that pass, and the pass runs again, up to 6 times (`passSkips`,
`functionsAround`). This is the last resort: the function keeps its text from the previous
pass.

If nothing helps, the pass fails. The tool writes the code that doesn't compile to
`<output>.failed.cc` and exits with status 1. `--save-temps` keeps the output of every pass,
and `-v` logs every error and every fallback.

## 5. The passes

The examples below are the tool's own output for one demo program (the indentation is the
printer's).

### 5.1 insert-braces

Uses clang-format with `InsertBraces` so that every control-flow body is a block. A later
pass can then add a statement before any statement.

```cpp
if (x < 0) return 0;        =>   if (x < 0) {
                                   return 0;
                                 }
```

It also drops the **explicit instantiation declarations**. `extern template class
basic_string<char>;` keeps every member of `basic_string<char>` from being instantiated in
the TU, so they couldn't be specialized either. A declaration is kept when the TU can't
instantiate what it names, for example `extern template ostream &__ostream_insert(...)`
when `basic_ostream` is only forward-declared (doctest). Those are the declarations whose
names appear in the instantiation notes of the errors that dropping them causes
(`dropInstantiableExternTemplates`).

### 5.2 access-specifier

Makes every member and every base `public`. Code that decl-before-def moves out of a class,
a monomorphized copy of a member, and an inlined body all use members from outside the
class.

```cpp
class Counter {                 =>   class Counter {
  int n = 0;                         public:
public:                                int n = 0;
  int next() { return ++n; }         public:
};                                     int next() { return ++n; }
                                     };
```

Type traits can observe access: `static_assert(!std::is_destructible<buffer<int>>())` holds
only while the destructor is protected, and `static_assert(!std::is_convertible_v<D*, B*>)`
only while `B` is a private base. When the all-public version doesn't compile, the pass is
redone with the bases, and the constructors, destructors and assignment operators, **keeping
their access** (`protected: ~buffer() = default; public:`). Everything else is still made
public. A copy of a member that converts to a base left private doesn't compile then, and is
dropped by decl-before-def (section 4.2): its callers call the original.

### 5.3 name-template-type-parm

Names unnamed template parameters. decl-before-def prints declarations for a template's members,
and the printer spells the class's own name inside it with the parameters: `void f(S other)`
prints as `void f(S<type-parameter-0-0> other);` when the parameter is unnamed.

```cpp
template <class T, class = void>   =>   template <class T, class __type_param__0 = void>
```

### 5.4 materialize-lambda

Turns a lambda into a named struct with a call operator, and captures into members, so that
its type can be named, by a specialization of a template it's passed to, for example.

```cpp
auto add = [a](int y) { return a + y; };
        =>
struct __invisible_lambda_1a2b3c4d5e_0 {
  int a;
  inline constexpr int operator()(int y) const { return a + y; }
};
auto add = __invisible_lambda_1a2b3c4d5e_0{a};
```

The struct is named after the enclosing function, by a hash of its mangled name, and the
lambda's index in it, as clang names closures when it mangles them. An inline function gets the
same names in every TU, and different functions never share one. The struct of a function of
internal linkage, such as a `static` one, goes in an unnamed namespace, as another TU may have a
function of the same name.

In a template the struct becomes a template too. Lambdas are processed innermost first, so
the pass repeats until none is left. It leaves a lambda alone when it can't express it:

- the lambda converts to a function pointer;
- it captures a pack;
- it would need two packs;
- it's outside any function, as a global initializer;
- it names a type local to the enclosing function, such as a local `struct` it takes as a
  parameter, which the struct, placed before the function, can't see.

### 5.5 decl-before-def

The heart of the tool; see section 6. In short: every function is declared before any definition,
and every definition the binary has is written at the end of the TU, instantiations as
explicit specializations.

### 5.6 compound-stmt-wrapper

Wraps every `for`, `if` and `switch` in a block. The statements that later passes put before
them, such as a loop's declarations or hoisted calls, then stay in their scope. The body of a
`switch` that isn't a block becomes one, so that mark-switch-case can add to it.

```cpp
if (x < 0) { ... }                  =>   { if (x < 0) { ... } }
switch (x) case 0: default: f();    =>   { switch (x) { case 0: default: f(); } }
```

### 5.7 mark-switch-case

Puts an empty statement after every case label, so that hoist-calls can put a call's
temporary under the label rather than above it. Nested labels are marked once, at the
innermost one.

```cpp
case 1:                     =>   case 1:
  a = twice(a);                    ;
                                   a = twice(a);
```

### 5.8 paren-expr-wrapper

Parenthesizes every comma operator, so that a pass that splits or moves an operand doesn't
change how it groups.

### 5.9 disassemble-cxx-for-range-stmt

Writes a range-based `for` as the iterator loop the standard defines it as, so that the
`begin()`, `end()`, `!=`, `++` and `*` calls are visible and hoistable.

```cpp
for (int x : xs) { a += x; }
        =>
{
auto __for_begin_0 = xs;
auto __for_end_0 = xs + 3L;
for (; __for_begin_0 != __for_end_0; ++__for_begin_0) {
int x = *__for_begin_0;
{ a += x; }
}
}
```

### 5.10 loop-to-ir

A loop whose condition or increment calls something is rewritten so that the call is a
statement of the body. A `continue` then jumps to the increment.

```cpp
while (clamp(a) > 100) {    =>   while (1) {
  a = a / 2;                       if (!(clamp(a) > 100)) { break; }
}                                  a = a / 2;
                                 }
```

`for`, `do` and condition variables (`while (T v = f())`, `if (init; cond)`) are handled
alike. The comments of `LoopToIRVisitor.cpp` show each shape.

"IR" refers to the shape the compiler lowers a loop to: in LLVM IR, the condition is a block
that runs before every iteration and branches out of the loop. The pass writes that shape as
source, `while (1)` for the loop and `if (!(cond)) break;` for the branch.

### 5.11 comma-init

```cpp
int a = 1, b = twice(argc);    =>   int a = 1;
                                    int b = twice(argc);
```

### 5.12 short-circuit-to-ir

`&&`, `||` and `?:` evaluate an operand only sometimes. When an operand calls something, the
expression is spelled out as `if`s, so that hoisting the call keeps it conditional.
"IR" refers to the shape the compiler lowers these operators to: in LLVM IR, `a && f()` is a
branch around the call of `f` and a phi that merges the result. The pass writes that shape as
source, nested `if`s for the branches and `__short_circuit_result_N` for the phi.

```cpp
int both = argc > 1 && twice(b) > 4;
        =>
bool __short_circuit_result_0 = false;
if (argc > 1) {
    if (twice(b) > 4) {
        __short_circuit_result_0 = true;
    }
}
int both = __short_circuit_result_0;
```

- **`&&` and `||`.** The result has the type of the expression: `bool` with `true`/`false` in
  C++, so that an overload or `auto` sees the same type, and `int` with `1`/`0` in C.
- **`?:`.** The result has the type of the whole expression: `c ? f() : 2.5` is a `double`, even
  though `f()` returns an `int`. An lvalue `?:` names an object rather than a value, so the
  result points at it: `(c ? fx() : fy()) = 5` becomes `p = __builtin_addressof(fx())` and
  `(*p) = 5`. A bit-field has no address and is left alone.
- **Constants.** A constant expression is left alone: it may have to be one, and it has the
  same value.

### 5.13 constant-fold

Replaces a constant expression with its value, `2 * 8 + 1` => `17` and `int()` => `0`. A value
keeps its type:

| Value of type | Written as | Why |
|---|---|---|
| `int`, `long`, `unsigned`... | `17`, `17L`, `17U` (`integerSuffix`) | the literal's type is the expression's |
| `char`, `short`, `char16_t`... | `(char)36` | a bare `36` is an `int`: `D(char('$'))` would call `D(const T&)` instead |
| minimum of a signed type wider than `int` | `(9223372036854775807L - 1)` | the literal of the minimum doesn't exist |
| `float`, `double` | `1.5F`, `1.5` | full precision |

It only folds numbers: never a class, union, array, address or enumeration, nor a comma
expression, a builtin call, `__int128`, an expression with undefined behavior, or anything under
`&`, `typeof` or a member access. Only expressions of a numeric type are evaluated at all.

### 5.14 hoist-calls

Moves every call nested in another expression into a temporary of its own, innermost first,
until every call is a statement. This is what the inliner needs.

```cpp
return add(twice(clamp(box.get())));
        =>
int __func_hoist_ret__8 = box.get();
int __func_hoist_ret__7 = clamp(static_cast<int &&>(__func_hoist_ret__8));
int __func_hoist_ret__5 = twice(static_cast<int &&>(__func_hoist_ret__7));
int __func_hoist_ret__3 = add(static_cast<int &&>(__func_hoist_ret__5));
return static_cast<int &&>(__func_hoist_ret__3);
```

- **Value categories.** An lvalue result is bound to a reference. An xvalue is bound to an
  rvalue reference. A prvalue gets a variable and is passed on with `static_cast<T &&>`, so
  that it's moved like the temporary was. A class that can only be copied is copied, and
  one that can be neither copied nor moved isn't hoisted.
- **Conditional calls.** A call in a branch of `?:` or the right of `&&`/`||` that
  short-circuit-to-ir left as written isn't hoisted.
- **Unevaluated operands.** Calls in `decltype`, `sizeof`, `noexcept` and `requires` aren't
  hoisted.
- **Temporaries.** A call taking `__mono_addr(...)` (the address of a temporary, section 7) isn't
  hoisted out of its statement.
- **Constant expressions.** Calls in template arguments, `case` labels and `if constexpr`
  conditions are made by the compiler and aren't hoisted.
- **Explicit conversions.** A conversion applied implicitly, as `if (obj)` does, is
  direct-initialized: `bool __func_hoist_ret__0(obj);`, which also calls an `explicit`
  conversion.
- **Unnameable types.** A temporary of a type that can't be named, such as one specialized for
  a lambda's closure type, is declared `auto`, and cast with `decltype(tmp)`.
- **Labels.** The temporaries of a labeled statement go after its labels, so that a jump to a
  label still makes the calls. In a function with labels, the temporaries of a statement go
  into a block of their own, because a `goto` must not jump past their initialization.

## 6. decl-before-def in depth

### 6.1 What it writes

For every function definition in the TU:

1. Its definition in place becomes a **declaration**.
2. A **declaration** of it is added at the end of the TU, followed by its **definition**.

So every function is declared before any definition, and any deferred body can call any
function, which is what a pasted body needs. The definitions at the end, deferred definitions,
specializations and copies alike, keep the order of the input: a copy is written where the
template it comes from is defined. So what must be defined before it is used, not just
declared, eg. a copy a `constexpr` variable's initializer calls, is defined in time, as it was
in the input. Specializations returning `auto` are the exception and come first
(`deferredDefinitionOrder`). For the demo:

```cpp
// in place
int twice(int ___var__1);
class Counter { public: int n = 0; public: int next(); };
template <class T> T sum(const T *___var__5, int ___var__7);

// at the end: declarations...
int Counter::next();
template<> int Box<int>::get() const;
template<> int sum(const int *___var__5, int ___var__7);         // sum<int>, deduced

// ...then definitions
[[gnu::used]] inline int Counter::next(){ return ++this->n; }
template<> [[gnu::used]] inline int sum(const int *___var__5, int ___var__7){ ... }
```

Every local variable and parameter is renamed `___var__N`, unique in the TU
(`CanonicalizeVarDecl`), so that an inlined body never shadows or captures a caller's name.

### 6.2 Templates become specializations

A function template, or a member of a class template, stays a declaration in place. Each of
its **instantiations** is written at the end as an explicit specialization: the concrete
function the binary contains. Printing a specialization that compiles takes care:

| Problem | Handling |
|---|---|
| a specialization can't repeat default arguments or a `requires` clause | suppressed (`SuppressInitializers`, `erase_requires`) |
| `auto f()` specialized must repeat `auto`, and can't be called before its definition | the placeholder is restored; such definitions come first, callees before callers (`deferredDefinitionOrder`) |
| `noexcept(expr)` may call what's defined later | printed by value: `noexcept(true)`; a member of a class template also gets a probe that makes clang compute the member's exception specification first (`printMemberProbe`) |
| a constructor template's arguments can't be written | stripped (`stripConstructorTemplateArgs`) |
| a pack's arguments are printed with extra `<>` | reprinted (`fixParameterPack`) |
| written template arguments are substituted into every template of the name, which can fail hard, eg. `__niter_base<ostream_iterator<char>>` instantiating `reverse_iterator<ostream_iterator<char>>` | arguments omitted when they're all deducible and only one template deduces the type (`stripTemplateArguments`): `template<> int sum(const int *, int)` |
| a class hidden by a function of its name, `struct stat` and `stat()` | not handled: the printer drops the keyword, the specialization doesn't compile and is left as written (section 4.2, section 10) |
| a type in an unnamed namespace | the definition is reopened in it (`enterUnnamedNamespace`) |
| an instantiation of a `constexpr` function template that can never be constant, eg. constructing a `std::string` before C++23 | printed without `constexpr` (`printsConstexpr`); a member of a class template must keep it |
| an enum template argument that's no enumerator, `VM<int, Mode(Search \| Backward)>` | every template argument is printed with its type, `VM<int, (Mode)5>`, and an integer with its suffix, `array<int, 3UL>` (`codePolicy`, clang's `AlwaysIncludeTypeForTemplateArgument`) |

### 6.3 What stays as written

Some functions keep their text in place, along with their instantiations:

- **Friends.** They're left exactly as written.
- **Defaulted, deleted and `consteval` functions.**
- **`constexpr` functions** that aren't templates, members included. A constant expression
  anywhere may evaluate a `constexpr` function, also through a concept or argument-dependent
  lookup, which no scan can see. It still gets a copy for what is deferred to call.
- **Explicitly instantiated templates.** `template void Data::reserve<true>(int);` needs the
  template's definition where it's written, and no specialization may follow it.
- **Templates instantiated for a lambda's closure type.** The closure type can't be named.
- **What `KeepInPlaceVisitor` finds.** Templates and `constexpr` functions used outside
  function bodies, and what those use; a plain function used there only needs its
  declaration, unless its return type is deduced; what a constant expression calls, in a template argument or a `static_assert` for
  example, also in a body; and a function whose type or class can't be named elsewhere, such as
  a return type deduced to name a lambda's closure type.

The last rule exists because what's used outside function bodies is evaluated where it's
written, in a constant expression for example. That code needs the definition there, and
it instantiates the function before a specialization could be declared:

```cpp
template <class T> constexpr bool small() { return sizeof(T) < 4; }
template <class T, class = std::enable_if_t<small<T>()>> void f(T);   // uses small<T> here
```

### 6.4 Supporting visitors run in the same parse

decl-before-def runs four more visitors on the same AST first:

- **`FunctionalCastToCStyleCast`.** `T(x)` becomes `(T)(x)` in the AST. Once specialized,
  `T(x)` with `T = unsigned long` would print as `unsigned long(x)`, which doesn't parse.
- **`CanonicalizeVarDecl`.** The unique `___var__N` names of section 6.1.
- **`BreakLocalRecordDeclStmt` and `MarkLocalRecordDeclMethodsExplicit`.** By default the
  printer hides a local class declared together with variables, and the implicit
  instantiations of its members. These visitors make it print both.
- **`instantiateConstructorsOfValues`.** It has Sema instantiate the move and copy
  constructors that hoist-calls will call. Otherwise a constructor of a member of a class
  template could be used without ever being defined.

## 7. Monomorphization

An instantiation written as an explicit specialization **leaves its template in place**, and
one that's only declared in place, whose template is still there, works for the compiler but
gives the inliner nothing. So every instantiation of C++ gets a **copy** instead: a free
function with a name of its own. Callers call the copy, and as many templates as possible are
reduced to declarations. C has no templates.

```cpp
Box<int> box{sum(xs, 3)};
return add(twice(clamp(box.get())));
        =>
Box<int> box{sum__monocb30900316(xs, 3)};
return operator__monofddf8e98a0(__builtin_addressof(add), twice(clamp(get__monof72ff75422(__builtin_addressof(box)))));

[[gnu::used]] inline int get__monof72ff75422(const Box<int, void> *__this){
    return __this->value;
}
[[gnu::used]] inline int sum__monocb30900316(const int *xs, int n){ ... }
```

### 7.1 Copies

- **Name.** `<name>__mono<hash of the mangled name>`. A function gets the same copy in every
  TU, and with `inline` the duplicates merge at link time. A function with internal linkage
  gets a `static` copy.
- **Object.** A member function's copy takes its object as the first parameter, `T
  *__this`. `this` in the body becomes `__this`.
  - An lvalue object is passed by `__builtin_addressof(obj)`.
  - A temporary is passed by `__mono_addr(make())`, a helper that returns the address of
    its argument. The temporary lives until the end of the full expression, which is why
    hoist-calls never pulls `__mono_addr(...)` out of its statement.
- **Names.** A copy is printed at the end of the TU, outside every namespace and class.
  Every name in its body is qualified from the global namespace (`qualifyNonLocalNames`)
  and every type printed canonically.
- **Callers.** Calls are redirected in two places. In the bodies of the deferred definitions
  and the copies, `redirectCalls` rewrites the AST, which also reaches calls in declarations
  that a `PrinterHelper` never sees, such as the initializers of a local class's members. In
  the rest of what decl-before-def prints, a constructor's member initializers for example,
  `MonoPrinterHelper` prints a call of a copied function as a call of its copy, the object
  first. A function's address, `&f`, becomes the copy's. A comparison rewritten from another,
  eg. `a != b` from `a == b`, or `a < b` from `<=>`, is written as what it was rewritten to once
  a call in it goes to a copy: `!(operator__mono...(a, b))`, or `operator__mono...(a <=> b, 0)`
  for a `<` whose `<=>` has no copy. clang can only print `a < b` while the calls under it are
  the ones it made (`rewrittenForm`).
- **`constexpr`.** A copy is `constexpr` exactly when its original instantiation can be a
  constant expression, the same rule as for specializations (`printsConstexpr`). The passes
  after decl-before-def still rewrite it. They leave as written only the `constexpr` functions
  that stay in place, which the compiler may evaluate; a copy, called only by deferred code at
  run time, never is (`isCopy`).

### 7.2 What isn't copied

These instantiations are written as explicit **specializations** instead (section 6):

| Kind | Why |
|---|---|
| constructors, destructors | nothing can call a free function as one |
| virtual functions | the vtable points at the member |
| functions with a non-constant `static` local | every caller must share the one variable |
| named where a call can't be redirected: an address of a member, a call from an implicit member such as a defaulted copy constructor | the name stays the member's (`KeepInPlaceVisitor::specialize`) |

### 7.3 Which templates keep their text

A template is reduced to a declaration unless something still needs its text:

- a **compile-time use**: `constexpr`/`constinit` initializers, `static_assert`, template
  arguments, `if constexpr`, any `ConstantExpr`;
- a call from a template that stays, so keeping spreads through calls (a closure);
- an instantiation that can be neither copied nor specialized;
- whatever the driver's recovery found a later pass needs (section 4.1).

A template that's still called keeps its text.

## 8. The inliner

`invisible inline pre.cc --oracle=oracle.txt` reads the oracle derived from `-O2`'s
optimization records:

```
_Z5totalR3VecIiE _Z18at__mono1492d308f1P3VecIiEi 27
_Z5totalR3VecIiE _Z6squarei 28
main _Z5totalR3VecIiE 38
```

and pastes each callee's body at the call on that line of the caller:

```cpp
int &__func_hoist_ret__2 = at__mono1492d308f1(__func_hoist_ret__3, i);
        =>
int *__inline_ret__0;
{Vec<int> *__inline_param__0_0 = __func_hoist_ret__3;
 int __inline_param__0_1 = i;
 { int &__inline_ref__ = __inline_param__0_0->data[__inline_param__0_1];
   __inline_ret__0 = __builtin_addressof(__inline_ref__); goto __inline_exit_0; }
__inline_exit_0: ;}
int &__func_hoist_ret__2 = static_cast<int &>(*__inline_ret__0);
```

- **Parameters and results.** Parameters become variables, and `return` becomes an
  assignment and a `goto`. A reference result is carried as a pointer.
- **Rounds.** Sites are inlined in rounds. Each round takes the sites whose callee has
  nothing left to inline into it, one site per line, and is checked with one compile. A
  round that doesn't compile is split in halves.
- **Calls found in inlined bodies aren't followed.** When LLVM inlines `B` into `A` and then
  the call of `C` it brought along, the oracle gives that call the line in `B`, which isn't in
  `A`, and it is reported as not found.
- **Skipped sites.** These are reported, not inlined: constructors and destructors, calls
  returning a class that isn't trivially default-constructible, calls that aren't a statement
  of their own (`topLevelStatement`, which hoist-calls makes every call it can), and calls
  initializing a static, thread local or constexpr variable. A temporary of the call that the
  inlined code outlives isn't checked, see `VisitCallExpr`.

## 9. Testing

### 9.1 Regression tests: `test/run.py`

Every file in `test/cases/` is a translation unit, and every directory in `test/link/` is a
program of several. Each passes when:

1. invisible preprocesses it;
2. the output compiles with `-w -Wno-out-of-line-declaration`;
3. the output leaves nothing undefined that the original defines (`nm`);
4. a program prints and returns what the original does;
5. the output matches the test's `CHECK` and `CHECK-NOT` lines.

```
$ python3 test/run.py -j 10
93 pass, 0 fail, 0 xfail, 0 xpass (93 tests)
$ python3 test/run.py constant_fold --keep /tmp/t     # one test, keeping its files
```

A test documents itself in its first comment: the problem it guards against. Every bug fixed
gets a minimal test.

### 9.2 Dataset runs

`~/master/invisible-regression/exp/exp.py` runs whole units of real projects:

- **`pp`** preprocesses a unit and compiles the output.
- **`run`** also builds the oracle, inlines and compiles the result.

`linkcheck.py` compares the symbols each unit's output leaves undefined, and what the
outputs of a project define twice, against the originals.

## 10. Limitations and accepted differences

- **Lifetimes.** A hoisted temporary lives until the end of its block, not of its full
  expression. Copy elision is lost and an extra move may happen. This is accepted.
- **Access.** Members and bases made public change what access-based SFINAE sees. The pass
  only notices when the TU stops compiling (section 5.2); an overload that SFINAE silently chooses
  differently isn't noticed. Two runtime checks of a base's access change too, with no compile
  error to tell: a handler `catch (B&)` catches a `D` whose base `B` was private, and
  `dynamic_cast<B*>` to a private base, `nullptr` before, succeeds. So does a trait: with a private copy
  constructor and a move constructor that isn't `noexcept`, `std::vector<T>` moves its elements
  when it grows, and once the copy constructor is public it copies them. A C++03-style
  noncopyable class, its copy constructor private and only declared, then fails to link.
- **Layout.** A class whose data members are all private isn't POD, and under the Itanium ABI a
  derived class may reuse its tail padding. Made public, it is POD and the padding isn't reused,
  eg. `struct B : A { char d; }` of `A { private: int i; char c; }` grows from 8 to 12 bytes. The
  binary agrees with itself, but not with code compiled from the original, eg. a prebuilt library.
- **C++23 range-for temporaries.** Since C++23 every temporary in a range-for's range lives
  until the loop ends. disassemble-cxx-for-range-stmt declares the range as `auto&&
  __for_range_N = <range>;`, so a temporary reached through it, the `Box` of `for (int x :
  Box().self())`, is destroyed before the loop runs.
- **Order of side effects that aren't calls.** comma-init puts the declarations it splits
  before a `goto` label, so `again: int a = next(), b = 1;` runs `next()` once however often
  `goto again;` jumps back. hoist-calls keeps calls in their order, but not an operand without a
  call to their left: `(++i, f(i))` calls `f` before `++i`, and so does `P p{i++, f(i)};`.
- **Fallbacks cost coverage.** Every function left as written, and every copy dropped
  (section 4.2, section 4.3), is code the inliner can't inline into. The verbose log (`-v`) reports each
  one.
- **Constructors and destructors aren't inlined.** A destructor also runs on paths the source
  never writes (exceptions, scope exits).
- **A class hidden by a function of its name.** `struct stat` next to the function `stat()`
  needs its keyword, which clang's printer drops in C++. Fixing that needs a change to clang's
  `TypePrinter`, which a tool linked against a production LLVM can't make, so a specialization
  naming such a class is left as written (section 4.2).

## 11. Code map

| Path | Contents |
|---|---|
| `main.cpp` | command line, modes, files |
| `invisible.cpp` | the passes' list, decl-before-def's retries (section 4.2), `Pipeline` and its recovery (section 4); `oneForAll` |
| `helper.cpp`, `helpers.h` | parsing in memory, the compile check, shared AST utilities |
| `preprocess/<pass>/` | one directory per pass of section 5 |
| `preprocess/decl_before_def/DeclBeforeDefVisitor.*` | section 6: deferring, and printing the specializations; section 7: the copies (`monomorphize`, `copyHeader`, `redirectCalls`, at the end of the file) |
| `preprocess/decl_before_def/KeepInPlaceVisitor.*` | what stays as written (section 6.3, section 7.3) |
| `common/DeclBeforeDefPrinterHelper.*` | how deferred bodies print around the printer's shortcomings; `MonoPrinterHelper`, which prints calls of copied functions as calls of their copies |
| `constant_fold/` | section 5.13, also a standalone optimization |
| `inline/` | section 8 |
| `copy_propagation/`, `dead_code_elim/`, `dead_store_elim/` | the `optimize` mode, outside this pipeline |
| `test/` | section 9.1 |

## 12. Building

The tool builds in the clang tree (`ninja invisible`), or standalone against an installed,
production LLVM of the same version, with no change to clang itself. The top of
`CMakeLists.txt` handles the standalone case:

```
cmake -S clang/tools/invisibleCompiler -B build-invisible -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DClang_DIR=/usr/lib/llvm-22/lib/cmake/clang
ninja -C build-invisible invisible
```

The binary links `libclang-cpp.so.22.1` and `libLLVM.so.22.1` of `/usr/lib/llvm-22` (apt.llvm.org's
22.1.1). A production LLVM is built without assertions, so code that an assertion-enabled clang
would reject in an assertion, but a release clang accepts, behaves as it does for users.
