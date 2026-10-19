# access-specifier
- marks every record decl in the TU (except lamdas) to expose its members with a public access. Converts private/protected specifiers to public counterparts
  as well. All base classes are also marked to be inherited publically. This can fail when say we have static_asserts in place. A fail safe is in place to revert these changes on a per function / method basis. Special member functions are wrapped with the original access specifiers and inherited base classes are reverted to the original access level for inheritance. 

# Name-template-type-param
- materializes an indentifier for the un-named template types. These are necessary for writing out of line member function definitions of templated records. 

# Materialize-Lamdas
- emits a lamda as a record just above the parent function (note that the parent function can be several declContext levels up). Lamdas are processed bottom up. So the most nested lamda is processed first. The clang AST itself parses a lamda as a record and so we can query most information from the AST. Then again, it fails us in ways one would not expect. The parent function can declare type-aliases and local records that the lamda can use, but cannot retain when its materialized into a standalone record. The latter has two solutions, either hoist the local records (which will open another can of worms) or just don't materialize such lamdas. I have taken the second approach. For type-aliases, there is a visitor that collects all the types and then emits them in the record. You can argue why not just use the `printAsCanonical` policy, but the problem there is that it prints template types as `template-paramter-x-y` which is ofcourse unparsable. The record for the lamda retains the template parameters of the actual lexical parent function. If the function has parents, their template parameters can be directly used by the record since its materialized just above the parent function. A lamda in a templated function further does not store information about the implicitly captured variables. There is a custom recursive visitor in place that collects such variables. Lamdas that are implicitly converted to a function pointer are also not materialized because we will have to add a conversion function as well in the record then. Further lamdas that have parameter packs as inputs are not materialized as well because each member of the pack will need to be represented as a member of the record upfront which we don't have information of in the AST. The types of the captured variables also needs to be handled with special care as these types are not always known upfront. For instance if the type is coming from the parent function template or if the type is supposed to be auto deduced. Lastly, `this` captured by a lamda is converted to `__parent_this__` member variable of the record. 

# decl-before-def
- A lot of functions in this pass are there just to get around the clang printer. On a high level, this pass separates the TU into 3 parts. First up, it replaces the function definitions in the TU with the corresponding function declarations. Next, it emits each function declaration in the order it encountered the function definitions. Finally it emits the function definitions (in their monomorphized) form at the end. Special functions (ctors/dtors/virtuals/constexpr) are not monomorphized / deferred because either their copies can't be created or moving them is infeasible because of their usage in constexpr/consteval contexts outside function declContext. 

# Compound-Stmt-Wrapper
- Wraps particular nodes (if/switch/for) with paranthesis so that any function calls in them can be hoisted respecting temporary lifetime. 

# Mark-switch-case
- Adds a nullStmt(;) after every case stmt in a switch Stmt so that the hoisted function calls have an anchor. Without this, the function calls get hoisted at the start of the switch Stmt. 

# DisassembleCXXForRange
- Breaks a complex for-range stmt into its individual C like counterparts. 

# 