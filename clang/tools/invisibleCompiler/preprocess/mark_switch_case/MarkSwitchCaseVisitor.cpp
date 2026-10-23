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

// --- MarkSwitchCaseVisitor ----------------------------------------------

MarkSwitchCaseVisitor::MarkSwitchCaseVisitor(clang::ASTContext&, clang::Rewriter& rewriter)
    : m_rewriter(rewriter) {}

// Puts an empty statement after a case label, eg.
//
//     case 1: g(f());          =>     case 1: ;
//                                     g(f());
//
// hoist-calls puts a call's temporary before the statement that holds the call. Without the `;`,
// that statement is `case 1: g(f());`, label included, so `int __func_hoist_ret__0 = f();` would
// land above `case 1:`, and the jump to the label would skip it. With the `;`, `g(f());` is a
// separate statement of the block, and the temporary goes right before it. compound-stmt-wrapper
// has put every switch body in a block, so `g(f());` stays in the switch. Nested labels,
// `case 0: case 1: f();`, get one `;`, after the innermost label.
bool MarkSwitchCaseVisitor::VisitSwitchCase(clang::SwitchCase* label) {
    if (!llvm::isa<clang::SwitchCase>(label->getSubStmt()))
        m_rewriter.InsertTextBefore(label->getSubStmt()->getBeginLoc(), ";\n");
    return true;
}

}  // namespace invisible
