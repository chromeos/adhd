/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DEVTOOLS_INLINE_MEMBER_COMMENTS_EXTRACT_MEMBER_COMMENTS_H_
#define DEVTOOLS_INLINE_MEMBER_COMMENTS_EXTRACT_MEMBER_COMMENTS_H_

#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace devtools {

// Extract member comments from a struct or enum comment.
// The returned tuple contains the updated struct or enum comment and
// a map of member name to member comment.
std::tuple<std::string, std::unordered_map<std::string, std::string>>
extract_comments(const std::string& comment,
                 const std::vector<std::string>& members);

// Simplify /* ... */ style comments.
std::string simplify(std::string s);

// Converts plain text `s` into a comment with `indent`.
std::string comment_text(const std::string& s, const std::string& indent);

}  // namespace devtools

#endif
