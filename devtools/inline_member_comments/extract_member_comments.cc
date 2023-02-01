// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extract_member_comments.h"

#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"
#include "re2/stringpiece.h"

namespace {
absl::string_view remove_comment_decoration_in_line(absl::string_view line) {
  line = absl::StripAsciiWhitespace(line);
  if (absl::StartsWith(line, "//") || absl::StartsWith(line, "/*") ||
      absl::StartsWith(line, "* ")) {
    line = line.substr(2);
  }
  if (absl::EndsWith(line, "*/")) {
    line = line.substr(0, line.length() - 2);
  }
  return absl::StripAsciiWhitespace(line);
}
}  // namespace

namespace devtools {

std::string comment_text(const std::string& s, const std::string& indent) {
  return absl::StrFormat(
      "%s// %s\n", indent,
      absl::StrReplaceAll(absl::StripTrailingAsciiWhitespace(s),
                          {
                              {"\n", absl::StrFormat("\n%s// ", indent)},
                          }));
}

static re2::LazyRE2 re_collapse_trailers{R"((\n \*)+\n \*\/$)"};
static re2::LazyRE2 re_make_single_line{R"(^\/\* ([^\n]*)\n \*\/$)"};

std::string simplify(std::string s) {
  re2::RE2::Replace(&s, *re_collapse_trailers, "\n */");
  re2::RE2::Replace(&s, *re_make_single_line, "/* \\1 */");
  return s;
}

std::tuple<std::string, std::unordered_map<std::string, std::string>>
extract_comments(const std::string& comment,
                 const std::vector<std::string>& members) {
  std::vector<absl::string_view> replacement_parts;
  std::unordered_map<std::string, std::string> result;

  std::vector<absl::string_view> lines = absl::StrSplit(comment, '\n');
  bool member_comments_started = false;
  std::string last_key;
  std::vector<absl::string_view> last_values;
  const char* delim = nullptr;
  for (absl::string_view raw_line : lines) {
    absl::string_view line = remove_comment_decoration_in_line(raw_line);
    if (!member_comments_started) {
      if (line == "Members:" || line == "Args:" || line == "Member:") {
        member_comments_started = true;
        continue;
      }
      for (absl::string_view member : members) {
        if (absl::StartsWith(line, absl::StrCat(member, " - ")) ||
            absl::StartsWith(line, absl::StrCat(member, ":"))) {
          member_comments_started = true;
        }
      }
      if (!member_comments_started) {
        replacement_parts.push_back(raw_line);
        continue;
      }
    }
    // member_comments_started.
    if (!delim) {
      delim = absl::StrContains(line, " - ") ? " - " : ": ";
    }
    std::vector<absl::string_view> parts =
        absl::StrSplit(line, absl::MaxSplits(delim, 1));
    if (parts.size() == 1) {
      last_values.push_back(parts[0]);
    } else {
      last_values.clear();
      last_key = std::string(parts[0]);
      last_values.push_back(parts[1]);
    }
    result[last_key] = std::string(
        absl::StripAsciiWhitespace(absl::StrJoin(last_values, "\n")));
  }

  if (replacement_parts.empty()) {
    return {"", std::move(result)};
  }

  if (absl::StartsWith(replacement_parts[0], "/*") &&
      !absl::EndsWith(replacement_parts[replacement_parts.size() - 1], "*/")) {
    replacement_parts.push_back(" */");
  }
  std::string replacement = absl::StrJoin(replacement_parts, "\n");
  return {std::move(replacement), std::move(result)};
}

}  // namespace devtools
