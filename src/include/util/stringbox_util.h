
//===----------------------------------------------------------------------===//
//
//                         PelotonDB
//
// stringbox_util.h
//
// Identification: /peloton/src/include/common/stringbox_util.h
//
// Copyright (c) 2015, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include "util/string_util.h"

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace peloton {

/**
 * Pretty-Print Box
 */
class StringBoxUtil {
 public:
  /**
   * Create a box around whatever you string you throw at it.
   * It will split the input based on newlines and pad the box
   * with a single space character all around.
   * It will automatically size the box based on the longest line in the input.
   */
  static std::string Box(const std::string &str) {
    std::string UNICODE_BOX_CORNERS[] = {"\u250C", "\u2510", "\u2514", "\u2518"};
    std::string UNICODE_BOX_VERTICAL = "\u2502";
    std::string UNICODE_BOX_HORIZONTAL = "\u2500";

    return StringBoxUtil::MakeBox(str, -1, UNICODE_BOX_HORIZONTAL, UNICODE_BOX_VERTICAL,
                                  UNICODE_BOX_CORNERS);
  }

  /**
   * Same has StringBoxUtil::Box except that it uses thicker lines.
   */
  static std::string HeavyBox(const std::string &str) {
    std::string UNICODE_HEAVYBOX_CORNERS[] = {"\u250F", "\u2513", "\u2517", "\u251B"};
    std::string UNICODE_HEAVYBOX_VERTICAL = "\u2503";
    std::string UNICODE_HEAVYBOX_HORIZONTAL = "\u2501";

    return StringBoxUtil::MakeBox(str, -1, UNICODE_HEAVYBOX_HORIZONTAL, UNICODE_HEAVYBOX_VERTICAL,
                                  UNICODE_HEAVYBOX_CORNERS);
  }

 private:
  static std::string MakeBox(const std::string &str, int max_len, std::string &horzMark,
                             std::string &vertMark, std::string corners[]) {
    std::vector<std::string> lines = StringUtil::Split(str);
    if (lines.size() == 0) return ("");

    // CORNERS:
    //  0: Top-Left
    //  1: Top-Right
    //  2: Bottom-Left
    //  3: Bottom-Right

    if (max_len <= 0) {
      for (std::string line : lines) {
        int line_len = static_cast<int>(line.length());
        if (max_len <= 0 || line_len > max_len) {
          max_len = line_len;
        }
      }  // FOR
    }

    std::stringstream os;

    // TOP LINE :: padding - two corners
    os << corners[0];
    os << StringUtil::Repeat(horzMark, max_len + 2);
    os << corners[1];
    os << std::endl;

    // INNER
    for (std::string line : lines) {
      os << vertMark << " ";
      os << std::left << std::setw(max_len) << line;
      os << " " << vertMark << std::endl;
    }  // FOR

    // BOTTOM LINE :: padding - two corners
    os << corners[2];
    os << StringUtil::Repeat(horzMark, max_len + 2);
    os << corners[3];

    return (os.str());
  }
};

}  // END peloton
