// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_set>

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "extract_member_comments.h"
#include "llvm/Support/CommandLine.h"

using namespace clang;
using namespace clang::tooling;
using namespace clang::ast_matchers;

namespace devtools {
struct Member {
  std::string name;
  SourceLocation line;
};

class CommentInliner : public MatchFinder::MatchCallback {
 public:
  CommentInliner(std::map<std::string, Replacements>& replacements)
      : replacements_(replacements) {}

  virtual void run(const MatchFinder::MatchResult& result) {
    const TypeDecl* decl = nullptr;
    if (const auto* record = result.Nodes.getNodeAs<RecordDecl>("struct")) {
      if (!record->isThisDeclarationADefinition()) {
        // Skip forward declarations.
        return;
      }

      decl = record;
    } else if (const auto* enum_decl =
                   result.Nodes.getNodeAs<EnumDecl>("enum")) {
      decl = enum_decl;
    }

    if (!decl) {
      return;
    }

    if (const RawComment* huge_comment_block =
            decl->getASTContext().getRawCommentForDeclNoCache(decl)) {
      SourceManager& sm = decl->getASTContext().getSourceManager();

      std::string name = decl->getNameAsString();
      if (visited_.find(name) != visited_.end()) {
        return;
      }
      visited_.insert(name);

      std::vector<Member> members;
      auto get_line_loc = [&sm](SourceLocation loc) {
        return loc.getLocWithOffset(1 - sm.getPresumedColumnNumber(loc));
      };
      if (const auto* record = dyn_cast<const RecordDecl>(decl)) {
        for (auto* field : record->fields()) {
          members.push_back({
              field->getNameAsString(),
              get_line_loc(field->getUnderlyingDecl()->getLocation()),
          });
        }
      } else if (const auto* enum_decl = dyn_cast<const EnumDecl>(decl)) {
        for (const auto* item : enum_decl->enumerators()) {
          members.push_back({
              item->getNameAsString(),
              get_line_loc(item->getBeginLoc()),
          });
        }
      }

      std::vector<std::string> member_names;
      member_names.reserve(members.size());
      for (const Member& member : members) {
        member_names.push_back(member.name);
      }
      std::string replacement_comment;
      std::unordered_map<std::string, std::string> member_comments;
      std::tie(replacement_comment, member_comments) = extract_comments(
          huge_comment_block->getRawText(sm).str(), member_names);

      if (member_comments.empty()) {
        return;
      }

      Replacement replacement(sm, huge_comment_block,
                              simplify(replacement_comment),
                              result.Context->getLangOpts());

      llvm::errs() << "== fixing " << decl->getName() << " @ "
                   << replacement.getFilePath() << " ==\n";

      addReplacement(replacement, sm.getFileManager());

      for (const Member& member : members) {
        if (const auto parsed_field_comment = member_comments.find(member.name);
            parsed_field_comment != member_comments.end()) {
          Replacement replacement(
              sm, member.line, 0u,
              comment_text(parsed_field_comment->second, "\t"));

          addReplacement(replacement, sm.getFileManager());

          // Erase to mark completion.
          member_comments.erase(parsed_field_comment);
        }
      }

      for (auto comment : member_comments) {
        llvm::errs() << "!! discarded " << comment.first
                     << " which is not found in members\n"
                     << comment_text(comment.second, "");
      }
    }
  }

 private:
  void addReplacement(Replacement replacement, FileManager& fm) {
    SmallString<128> path(replacement.getFilePath());
    if (!path.startswith("/") && !fm.makeAbsolutePath(path)) {
      llvm::errs() << "!! cannot makeAbsolutePath(\"" << path << "\"\n";
      return;
    }
    if (path.endswith("/")) {
      llvm::errs() << "!! " << __func__
                   << ": bad absolute path: " << replacement.getFilePath()
                   << " -> " << path << "\n";
      return;
    }
    if (auto err = replacements_[std::string(path)].add(replacement)) {
      llvm::errs() << __func__ << ": " << err << "\n";
    }
  }

  std::map<std::string, Replacements>& replacements_;
  std::unordered_set<std::string> visited_;
};

static llvm::cl::OptionCategory MyToolCategory("my-tool options");
static llvm::cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
}  // namespace devtools

int main(int argc, const char** argv) {
  auto expected_parser =
      CommonOptionsParser::create(argc, argv, devtools::MyToolCategory);
  if (!expected_parser) {
    // Fail gracefully for unsupported options.
    llvm::errs() << expected_parser.takeError();
    return 1;
  }
  CommonOptionsParser& OptionsParser = expected_parser.get();
  RefactoringTool tool(OptionsParser.getCompilations(),
                       OptionsParser.getSourcePathList());

  devtools::CommentInliner inliner(tool.getReplacements());
  MatchFinder finder;
  finder.addMatcher(recordDecl().bind("struct"), &inliner);
  finder.addMatcher(enumDecl().bind("enum"), &inliner);

  return tool.runAndSave(newFrontendActionFactory(&finder).get());
}
