#include "preprocess/mark_switch_case/MarkSwitchCaseVisitor.h"
#include "helpers.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/Support/JSON.h"
#include "clang/AST/TemplateBase.h"

#include <string>
#include <queue>
#include <stack>

namespace invisible {

// ─── MarkSwitchCaseVisitor ──────────────────────────────────────────────

MarkSwitchCaseVisitor::MarkSwitchCaseVisitor(clang::ASTContext&, clang::Rewriter& rewriter)
    : m_rewriter(rewriter) {}

// Puts an empty statement after a case label, eg.
//
//     case 1: g(f());          =>     case 1: ;
//                                     g(f());
//
// hoist-calls inserts a call's temporary before the statement holding the call. Without the empty
// statement that statement is the label's, and `int __func_hoist_ret__0 = f();` would land above
// `case 1:`, where the jump to the label skips it. After `;` it is the next statement of the block.
// compound-stmt-wrapper has put the body of every switch in a block, where the statement can be
// added. Nested labels, `case 0: case 1: f();`, are marked once, at the innermost label's statement.
bool MarkSwitchCaseVisitor::VisitSwitchCase(clang::SwitchCase* label) {
    if (!llvm::isa<clang::SwitchCase>(label->getSubStmt()))
        m_rewriter.InsertTextBefore(label->getSubStmt()->getBeginLoc(), ";\n");
    return true;
}

}  // namespace invisible
