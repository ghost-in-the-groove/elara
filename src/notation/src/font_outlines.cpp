// SPDX-License-Identifier: AGPL-3.0-or-later
// Glyph outlines from an OpenType font with CFF outlines (ADR-0074): the table
// directory, the cmap table (formats 4 and 12) and the CFF table with a Type 2
// charstring interpreter, as the OpenType and Adobe CFF specifications
// (Technical Notes 5176 and 5177) describe them.
#include <elara/notation/font_outlines.hpp>

#include <cmath>
#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace elara::notation {

namespace {

constexpr double kStaffSpacesPerEm = 4.0;
constexpr int kMaxSubroutineDepth = 10;

// Big-endian reads with bounds checks; a read past the end throws.
class Bytes {
public:
    explicit Bytes(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }

    [[nodiscard]] std::uint32_t read(std::size_t at, std::size_t count) const {
        check(at, count);
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            value = (value << 8U) | bytes_[at + i];
        }
        return value;
    }
    [[nodiscard]] std::uint8_t u8(std::size_t at) const {
        return static_cast<std::uint8_t>(read(at, 1));
    }
    [[nodiscard]] std::uint16_t u16(std::size_t at) const {
        return static_cast<std::uint16_t>(read(at, 2));
    }
    [[nodiscard]] std::uint32_t u32(std::size_t at) const { return read(at, 4); }

    [[nodiscard]] std::span<const std::uint8_t> slice(std::size_t at, std::size_t count) const {
        check(at, count);
        return bytes_.subspan(at, count);
    }

private:
    void check(std::size_t at, std::size_t count) const {
        if (at > bytes_.size() || count > bytes_.size() - at) {
            throw std::runtime_error("the font file is truncated");
        }
    }

    std::span<const std::uint8_t> bytes_;
};

// The table `tag` of the font, if it has one.
std::optional<std::span<const std::uint8_t>> table(const Bytes& font, std::string_view tag) {
    const std::uint16_t count = font.u16(4);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t record = 12 + 16 * i;
        const auto name = font.slice(record, 4);
        if (std::equal(name.begin(), name.end(), tag.begin(), tag.end(),
                       [](std::uint8_t a, char b) { return a == static_cast<std::uint8_t>(b); })) {
            return font.slice(font.u32(record + 8), font.u32(record + 12));
        }
    }
    return std::nullopt;
}

// The glyph index the cmap subtable at `at` of `cmap` gives `code`, 0 for none.
std::uint32_t lookUp(const Bytes& cmap, std::size_t at, char32_t code) {
    const std::uint16_t format = cmap.u16(at);
    if (format == 12) {
        const std::uint32_t groups = cmap.u32(at + 12);
        for (std::size_t g = 0; g < groups; ++g) {
            const std::size_t group = at + 16 + 12 * g;
            const std::uint32_t first = cmap.u32(group);
            const std::uint32_t last = cmap.u32(group + 4);
            if (code >= first && code <= last) {
                return cmap.u32(group + 8) + (code - first);
            }
        }
        return 0;
    }
    if (format != 4 || code > 0xFFFF) {
        return 0;
    }
    const std::size_t segments = cmap.u16(at + 6) / 2U;
    const std::size_t ends = at + 14;
    const std::size_t starts = ends + 2 * segments + 2;
    const std::size_t deltas = starts + 2 * segments;
    const std::size_t rangeOffsets = deltas + 2 * segments;
    for (std::size_t s = 0; s < segments; ++s) {
        const std::uint16_t end = cmap.u16(ends + 2 * s);
        if (code > end) {
            continue;
        }
        const std::uint16_t start = cmap.u16(starts + 2 * s);
        if (code < start) {
            return 0;
        }
        const std::uint16_t delta = cmap.u16(deltas + 2 * s);
        const std::uint16_t rangeOffset = cmap.u16(rangeOffsets + 2 * s);
        if (rangeOffset == 0) {
            return (code + delta) & 0xFFFFU;
        }
        const std::size_t entry = rangeOffsets + 2 * s + rangeOffset + 2 * (code - start);
        const std::uint16_t glyph = cmap.u16(entry);
        return glyph == 0 ? 0 : (glyph + delta) & 0xFFFFU;
    }
    return 0;
}

// The Unicode subtable of a cmap table: (3,10) or (0,4) for every plane, else
// (3,1) or (0,3) for the basic plane. Returns its offset.
std::size_t unicodeSubtable(const Bytes& cmap) {
    const std::uint16_t count = cmap.u16(2);
    std::optional<std::size_t> full;
    std::optional<std::size_t> basic;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t record = 4 + 8 * i;
        const std::uint16_t platform = cmap.u16(record);
        const std::uint16_t encoding = cmap.u16(record + 2);
        const std::size_t offset = cmap.u32(record + 4);
        if ((platform == 3 && encoding == 10) || (platform == 0 && encoding == 4)) {
            full = offset;
        } else if ((platform == 3 && encoding == 1) || (platform == 0 && encoding == 3)) {
            basic = offset;
        }
    }
    if (full) {
        return *full;
    }
    if (basic) {
        return *basic;
    }
    throw std::runtime_error("the font has no Unicode cmap");
}

// A CFF INDEX: its items and where it ends.
struct Index {
    std::vector<std::span<const std::uint8_t>> items;
    std::size_t end = 0;
};

Index readIndex(const Bytes& cff, std::size_t at) {
    Index index;
    const std::uint16_t count = cff.u16(at);
    if (count == 0) {
        index.end = at + 2;
        return index;
    }
    const std::uint8_t size = cff.u8(at + 2);
    if (size < 1 || size > 4) {
        throw std::runtime_error("a CFF INDEX has a bad offset size");
    }
    const std::size_t offsets = at + 3;
    const std::size_t data = offsets + (count + 1U) * size - 1; // offsets count from 1
    std::size_t previous = cff.read(offsets, size);
    index.items.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        const std::size_t next = cff.read(offsets + i * size, size);
        if (next < previous) {
            throw std::runtime_error("a CFF INDEX has offsets out of order");
        }
        index.items.push_back(cff.slice(data + previous, next - previous));
        previous = next;
    }
    index.end = data + previous;
    return index;
}

// A real number of a DICT, from its nibbles starting at `at`; moves `at` past it.
double readReal(std::span<const std::uint8_t> dict, std::size_t& at) {
    double mantissa = 0.0;
    double scale = 1.0;
    bool fraction = false;
    bool negative = false;
    int exponent = 0;
    int exponentSign = 0; // 0: no exponent yet
    for (; at < dict.size(); ++at) {
        for (const unsigned nibble : {static_cast<unsigned>(dict[at] >> 4U), dict[at] & 0xFU}) {
            if (nibble <= 9) {
                if (exponentSign != 0) {
                    exponent = exponent * 10 + static_cast<int>(nibble);
                } else if (fraction) {
                    scale /= 10.0;
                    mantissa += scale * nibble;
                } else {
                    mantissa = mantissa * 10.0 + nibble;
                }
            } else if (nibble == 0xA) {
                fraction = true;
            } else if (nibble == 0xB) {
                exponentSign = 1;
            } else if (nibble == 0xC) {
                exponentSign = -1;
            } else if (nibble == 0xE) {
                negative = true;
            } else if (nibble == 0xF) {
                ++at;
                const double value = mantissa * std::pow(10.0, exponentSign * exponent);
                return negative ? -value : value;
            }
        }
    }
    throw std::runtime_error("a CFF DICT has an unterminated real number");
}

// A CFF DICT: each operator (12 x as 1200 + x) with its operands.
std::map<int, std::vector<double>> readDict(std::span<const std::uint8_t> dict) {
    std::map<int, std::vector<double>> entries;
    std::vector<double> operands;
    std::size_t at = 0;
    const auto byte = [&](std::size_t i) -> int {
        if (i >= dict.size()) {
            throw std::runtime_error("a CFF DICT is truncated");
        }
        return dict[i];
    };
    while (at < dict.size()) {
        const int b0 = dict[at];
        if (b0 <= 21) {
            int op = b0;
            if (b0 == 12) {
                op = 1200 + byte(at + 1);
                ++at;
            }
            ++at;
            entries[op] = std::exchange(operands, {});
        } else if (b0 == 28) {
            operands.push_back(static_cast<std::int16_t>((byte(at + 1) << 8) | byte(at + 2)));
            at += 3;
        } else if (b0 == 29) {
            const auto value = static_cast<std::uint32_t>(
                (byte(at + 1) << 24) | (byte(at + 2) << 16) | (byte(at + 3) << 8) | byte(at + 4));
            operands.push_back(static_cast<std::int32_t>(value));
            at += 5;
        } else if (b0 == 30) {
            ++at;
            operands.push_back(readReal(dict, at));
        } else if (b0 >= 32 && b0 <= 246) {
            operands.push_back(b0 - 139);
            ++at;
        } else if (b0 >= 247 && b0 <= 250) {
            operands.push_back((b0 - 247) * 256 + byte(at + 1) + 108);
            at += 2;
        } else if (b0 >= 251 && b0 <= 254) {
            operands.push_back(-(b0 - 251) * 256 - byte(at + 1) - 108);
            at += 2;
        } else {
            throw std::runtime_error("a CFF DICT has a reserved operand");
        }
    }
    return entries;
}

// The number added to a subroutine's index in a charstring.
std::size_t bias(const Index& subrs) noexcept {
    const std::size_t count = subrs.items.size();
    return count < 1240 ? 107 : count < 33900 ? 1131 : 32768;
}

// Runs Type 2 charstrings into outline steps, scaled from font units to staff
// spaces.
class Charstring {
public:
    Charstring(const Index& globals, const Index& locals, double scale)
        : globals_(globals), locals_(locals), scale_(scale) {}

    std::vector<OutlineStep> run(std::span<const std::uint8_t> code) {
        execute(code, 0);
        close();
        return std::move(steps_);
    }

private:
    void execute(std::span<const std::uint8_t> code, int depth) {
        if (depth > kMaxSubroutineDepth) {
            throw std::runtime_error("a charstring nests subroutines too deep");
        }
        std::size_t at = 0;
        const auto byte = [&](std::size_t i) -> int {
            if (i >= code.size()) {
                throw std::runtime_error("a charstring is truncated");
            }
            return code[i];
        };
        while (at < code.size() && !ended_) {
            const int b0 = code[at];
            if (b0 == 28) {
                stack_.push_back(static_cast<std::int16_t>((byte(at + 1) << 8) | byte(at + 2)));
                at += 3;
            } else if (b0 == 255) {
                const auto fixed =
                    static_cast<std::uint32_t>((byte(at + 1) << 24) | (byte(at + 2) << 16) |
                                               (byte(at + 3) << 8) | byte(at + 4));
                stack_.push_back(static_cast<std::int32_t>(fixed) / 65536.0);
                at += 5;
            } else if (b0 >= 32 && b0 <= 246) {
                stack_.push_back(b0 - 139);
                ++at;
            } else if (b0 >= 247 && b0 <= 250) {
                stack_.push_back((b0 - 247) * 256 + byte(at + 1) + 108);
                at += 2;
            } else if (b0 >= 251 && b0 <= 254) {
                stack_.push_back(-(b0 - 251) * 256 - byte(at + 1) - 108);
                at += 2;
            } else if (b0 == 12) {
                escaped(byte(at + 1));
                at += 2;
            } else if (b0 == 19 || b0 == 20) { // hintmask, cntrmask
                stems();                       // a vstem may come before the mask
                at += 1 + static_cast<std::size_t>((stems_ + 7) / 8);
            } else if (b0 == 10 || b0 == 29) { // callsubr, callgsubr
                const Index& subrs = b0 == 10 ? locals_ : globals_;
                const double index = pop() + static_cast<double>(bias(subrs));
                if (index < 0.0 || index >= static_cast<double>(subrs.items.size())) {
                    throw std::runtime_error("a charstring calls a missing subroutine");
                }
                execute(subrs.items[static_cast<std::size_t>(index)], depth + 1);
                ++at;
            } else if (b0 == 11) { // return
                return;
            } else {
                operate(b0);
                ++at;
            }
        }
    }

    void operate(int op) {
        switch (op) {
        case 1:  // hstem
        case 3:  // vstem
        case 18: // hstemhm
        case 23: // vstemhm
            stems();
            return;
        case 21: // rmoveto
            width(2);
            moveTo(arg(0), arg(1));
            break;
        case 22: // hmoveto
            width(1);
            moveTo(arg(0), 0.0);
            break;
        case 4: // vmoveto
            width(1);
            moveTo(0.0, arg(0));
            break;
        case 5: // rlineto
            for (std::size_t i = 0; i + 1 < stack_.size(); i += 2) {
                lineTo(stack_[i], stack_[i + 1]);
            }
            break;
        case 6: // hlineto
        case 7: // vlineto
            for (std::size_t i = 0; i < stack_.size(); ++i) {
                const bool horizontal = (i % 2 == 0) == (op == 6);
                lineTo(horizontal ? stack_[i] : 0.0, horizontal ? 0.0 : stack_[i]);
            }
            break;
        case 8: // rrcurveto
            for (std::size_t i = 0; i + 5 < stack_.size(); i += 6) {
                curveTo(stack_[i], stack_[i + 1], stack_[i + 2], stack_[i + 3], stack_[i + 4],
                        stack_[i + 5]);
            }
            break;
        case 27: { // hhcurveto
            std::size_t i = stack_.size() % 2;
            double dy1 = i == 1 ? stack_[0] : 0.0;
            for (; i + 3 < stack_.size(); i += 4) {
                curveTo(stack_[i], dy1, stack_[i + 1], stack_[i + 2], stack_[i + 3], 0.0);
                dy1 = 0.0;
            }
            break;
        }
        case 26: { // vvcurveto
            std::size_t i = stack_.size() % 2;
            double dx1 = i == 1 ? stack_[0] : 0.0;
            for (; i + 3 < stack_.size(); i += 4) {
                curveTo(dx1, stack_[i], stack_[i + 1], stack_[i + 2], 0.0, stack_[i + 3]);
                dx1 = 0.0;
            }
            break;
        }
        case 31: // hvcurveto
        case 30: // vhcurveto
            alternatingCurves(op == 31);
            break;
        case 24: { // rcurveline
            std::size_t i = 0;
            for (; i + 7 < stack_.size(); i += 6) {
                curveTo(stack_[i], stack_[i + 1], stack_[i + 2], stack_[i + 3], stack_[i + 4],
                        stack_[i + 5]);
            }
            lineTo(arg(i), arg(i + 1));
            break;
        }
        case 25: { // rlinecurve
            std::size_t i = 0;
            for (; i + 7 < stack_.size(); i += 2) {
                lineTo(stack_[i], stack_[i + 1]);
            }
            curveTo(arg(i), arg(i + 1), arg(i + 2), arg(i + 3), arg(i + 4), arg(i + 5));
            break;
        }
        case 14: // endchar
            if (!widthTaken_ && (stack_.size() == 1 || stack_.size() == 5)) {
                stack_.erase(stack_.begin());
            }
            widthTaken_ = true;
            close();
            ended_ = true;
            break;
        default:
            throw std::runtime_error(std::format("a charstring uses operator {}", op));
        }
        stack_.clear();
    }

    void escaped(int op) {
        switch (op) {
        case 35: // flex
            curveTo(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5));
            curveTo(arg(6), arg(7), arg(8), arg(9), arg(10), arg(11));
            break;
        case 34: // hflex
            curveTo(arg(0), 0.0, arg(1), arg(2), arg(3), 0.0);
            curveTo(arg(4), 0.0, arg(5), -arg(2), arg(6), 0.0);
            break;
        case 36: // hflex1
            curveTo(arg(0), arg(1), arg(2), arg(3), arg(4), 0.0);
            curveTo(arg(5), 0.0, arg(6), arg(7), arg(8), -(arg(1) + arg(3) + arg(7)));
            break;
        case 37: { // flex1
            const double dx = arg(0) + arg(2) + arg(4) + arg(6) + arg(8);
            const double dy = arg(1) + arg(3) + arg(5) + arg(7) + arg(9);
            const bool horizontal = std::abs(dx) > std::abs(dy);
            curveTo(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5));
            curveTo(arg(6), arg(7), arg(8), arg(9), horizontal ? arg(10) : -dx,
                    horizontal ? -dy : arg(10));
            break;
        }
        default:
            throw std::runtime_error(std::format("a charstring uses operator 12 {}", op));
        }
        stack_.clear();
    }

    // hvcurveto (starting horizontal) and vhcurveto: curves that alternate,
    // the last taking a fifth argument when one is left.
    void alternatingCurves(bool horizontal) {
        std::size_t i = 0;
        while (i + 3 < stack_.size()) {
            const bool last = stack_.size() - i == 5;
            const double extra = last ? stack_[i + 4] : 0.0;
            if (horizontal) {
                curveTo(stack_[i], 0.0, stack_[i + 1], stack_[i + 2], extra, stack_[i + 3]);
            } else {
                curveTo(0.0, stack_[i], stack_[i + 1], stack_[i + 2], stack_[i + 3], extra);
            }
            i += last ? 5 : 4;
            horizontal = !horizontal;
        }
    }

    // Stem hints: counted for the bytes of hint masks, otherwise ignored.
    void stems() {
        if (!widthTaken_ && stack_.size() % 2 == 1) {
            stack_.erase(stack_.begin());
        }
        widthTaken_ = true;
        stems_ += static_cast<int>(stack_.size() / 2);
        stack_.clear();
    }

    // The first stack-clearing operator may carry the glyph's width first.
    void width(std::size_t arguments) {
        if (!widthTaken_ && stack_.size() > arguments) {
            stack_.erase(stack_.begin());
        }
        widthTaken_ = true;
    }

    [[nodiscard]] double arg(std::size_t i) const {
        if (i >= stack_.size()) {
            throw std::runtime_error("a charstring operator lacks arguments");
        }
        return stack_[i];
    }

    double pop() {
        if (stack_.empty()) {
            throw std::runtime_error("a charstring calls a subroutine with no index");
        }
        const double value = stack_.back();
        stack_.pop_back();
        return value;
    }

    [[nodiscard]] Point here() const { return Point{x_ * scale_, y_ * scale_}; }

    void moveTo(double dx, double dy) {
        close();
        x_ += dx;
        y_ += dy;
        steps_.push_back(OutlineStep{.kind = OutlineStep::Kind::Move, .to = here()});
        open_ = true;
    }

    void lineTo(double dx, double dy) {
        x_ += dx;
        y_ += dy;
        steps_.push_back(OutlineStep{.kind = OutlineStep::Kind::Line, .to = here()});
    }

    void curveTo(double dxa, double dya, double dxb, double dyb, double dxc, double dyc) {
        x_ += dxa;
        y_ += dya;
        const Point first = here();
        x_ += dxb;
        y_ += dyb;
        const Point second = here();
        x_ += dxc;
        y_ += dyc;
        steps_.push_back(OutlineStep{
            .kind = OutlineStep::Kind::Curve, .to = here(), .control1 = first, .control2 = second});
    }

    void close() {
        if (open_) {
            steps_.push_back(OutlineStep{.kind = OutlineStep::Kind::Close});
            open_ = false;
        }
    }

    const Index& globals_;
    const Index& locals_;
    double scale_;
    std::vector<double> stack_;
    std::vector<OutlineStep> steps_;
    double x_ = 0.0;
    double y_ = 0.0;
    int stems_ = 0;
    bool widthTaken_ = false;
    bool open_ = false;
    bool ended_ = false;
};

} // namespace

std::expected<FontOutlines, std::string> FontOutlines::fromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(std::format("cannot open {}", path.generic_string()));
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size > 0 ? size : 0));
    if (!bytes.empty() &&
        !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        return std::unexpected(std::format("cannot read {}", path.generic_string()));
    }
    auto outlines = fromBytes(bytes);
    if (!outlines) {
        return std::unexpected(std::format("{}: {}", path.generic_string(), outlines.error()));
    }
    return outlines;
}

std::expected<FontOutlines, std::string>
FontOutlines::fromBytes(std::span<const std::uint8_t> bytes) {
    try {
        const Bytes font(bytes);
        if (font.u32(0) != 0x4F54544FU) { // "OTTO"
            return std::unexpected("not an OpenType font with CFF outlines");
        }
        const auto cmapTable = table(font, "cmap");
        const auto cffTable = table(font, "CFF ");
        if (!cmapTable || !cffTable) {
            return std::unexpected("the font has no cmap or CFF table");
        }
        const Bytes cmap(*cmapTable);
        const std::size_t subtable = unicodeSubtable(cmap);
        const Bytes cff(*cffTable);
        const Index names = readIndex(cff, cff.u8(2));
        const Index topDicts = readIndex(cff, names.end);
        const Index strings = readIndex(cff, topDicts.end);
        const Index globals = readIndex(cff, strings.end);
        if (topDicts.items.empty()) {
            return std::unexpected("the CFF table has no font");
        }
        const auto top = readDict(topDicts.items.front());
        if (top.contains(1230)) {
            return std::unexpected("CID-keyed CFF fonts are not supported");
        }
        if (const auto type = top.find(1206);
            type != top.end() && type->second != std::vector{2.0}) {
            return std::unexpected("the CFF charstrings are not of type 2");
        }
        const auto charStrings = top.find(17);
        if (charStrings == top.end() || charStrings->second.empty()) {
            return std::unexpected("the CFF font has no charstrings");
        }
        double unit = 0.001; // of an em per font unit
        if (const auto matrix = top.find(1207); matrix != top.end() && !matrix->second.empty()) {
            unit = matrix->second.front();
        }
        Index locals;
        if (const auto priv = top.find(18); priv != top.end() && priv->second.size() == 2) {
            const auto size = static_cast<std::size_t>(priv->second[0]);
            const auto offset = static_cast<std::size_t>(priv->second[1]);
            const auto dict = readDict(cff.slice(offset, size));
            if (const auto subrs = dict.find(19); subrs != dict.end() && !subrs->second.empty()) {
                locals = readIndex(cff, offset + static_cast<std::size_t>(subrs->second.front()));
            }
        }
        const Index glyphs = readIndex(cff, static_cast<std::size_t>(charStrings->second.front()));
        FontOutlines outlines;
        for (std::size_t g = 0; g < kGlyphCount; ++g) {
            const std::uint32_t index = lookUp(cmap, subtable, codepoint(static_cast<Glyph>(g)));
            if (index == 0 || index >= glyphs.items.size()) {
                continue;
            }
            Charstring charstring(globals, locals, unit * kStaffSpacesPerEm);
            outlines.outlines_[g] = charstring.run(glyphs.items[index]);
        }
        return outlines;
    } catch (const std::runtime_error& error) {
        return std::unexpected(error.what());
    }
}

} // namespace elara::notation
