#pragma once

#include <algorithm>
#include <cassert>
#include <client/TracyScoped.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <execution>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <ankerl/unordered_dense.h>
#include <more_concepts/more_concepts.hpp>

#include <incstd/core/explorers.hpp>
#include <incstd/core/hashing.hpp>
#include <incstd/core/matrix.hpp>
#include <incstd/core/random.hpp>

#include <incstd/polyfills/mdspan.hpp>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace incom::standard::solvers_TEMP {
using namespace incom::standard;

namespace packing {
namespace detail {
// Works for random-access + sized ranges (e.g. std::vector)
struct __strided_view {
    template <std::ranges::random_access_range R>
    requires std::ranges::sized_range<R>
    auto operator()(R &&r, std::size_t step = 1) const {
        step                    += (step == 0uz);
        const std::size_t count  = (std::ranges::size(r) + step - 1) / step; // ceil(n/step)
        return std::views::iota(std::size_t{0}, count) |
               std::views::transform([&r, step](std::size_t i) -> decltype(auto) {
                   return r[i * step]; // keeps reference semantics
               });
    }
};

inline constexpr auto pf_views_stride = __strided_view{};

} // namespace detail

class BoxPacker_2D {
public:
    // Forward declarations
    struct Pos;
    struct AlternID;

    class Shape;
    struct PastRes;
    struct OptionAtPos;

private:
    struct Overlay;

public:
    struct Pos {
        long long y = 0;
        long long x = 0;

        auto operator<=>(Pos const &) const = default;
    };

    struct AlternID {
        size_t shpID    = 0;
        size_t alternID = 0;

        auto operator<=>(AlternID const &) const = default;
    };


    class Shape {
    public:
        using value_type  = unsigned char;
        using matrix_type = std::vector<value_type>;

        size_t      m_height = 0uz;
        size_t      m_width  = 0uz;
        matrix_type m_matrix;


        auto operator<=>(Shape const &other) const = default;

        auto get_mdspanOfSelf() &&       = delete;
        auto get_mdspanOfSelf() const && = delete;
        auto get_mdspanOfSelf() & {
            return polyfills::mdspan<unsigned char, polyfills::dextents<size_t, 2>>(m_matrix.data(), m_height, m_width);
        }
        auto get_mdspanOfSelf() const & {
            return polyfills::mdspan<const unsigned char, polyfills::dextents<size_t, 2>>(m_matrix.data(), m_height,
                                                                                          m_width);
        }

        std::string get_areaState() const {
            std::string                   toPrint{};
            constexpr std::array<char, 3> map{46, 35, 118};

            auto area_view = get_mdspanOfSelf();

            for (size_t lineID = 0uz; lineID < m_height; ++lineID) {
                for (size_t colID = 0uz; colID < m_width; ++colID) { toPrint.push_back(map[area_view[lineID, colID]]); }
                toPrint.push_back('\n');
            }
            return toPrint;
        }


    private:
        // bool _upsize(std::optional<size_t> const tarHeight, std::optional<size_t> const tarWidth);
        // bool _downsize(std::optional<size_t> const tarHeight, std::optional<size_t> const tarWidth);
        // bool _change_size_withoutBorder(size_t const tarHeight, size_t const tarWidth);
        bool _resize_withBorder(size_t const tarHeight, size_t const tarWidth, size_t const borderThickness) {
            long long const rowDelta     = static_cast<long long>(tarHeight) - static_cast<long long>(m_height);
            long long const colDelta     = static_cast<long long>(tarWidth) - static_cast<long long>(m_width);
            auto            row_startEnd = std::pair{borderThickness, m_height - borderThickness};
            auto            col_startEnd = std::pair{borderThickness, m_width - borderThickness};

            // Rows: We only do the following if we are trying to remove rows
            if (rowDelta < 0) {
                auto rd_loc = rowDelta;
                // Rows: Can remove from the end?
                for (long long skip                     = (static_cast<long long>(m_matrix.size()) -
                                                           static_cast<long long>((borderThickness + 1) * m_width)) +
                                                          static_cast<long long>(borderThickness);
                     (skip > 0ll && rd_loc != 0); skip -= m_width) {
                    if (std::ranges::all_of(std::views::drop(m_matrix, skip) |
                                                std::views::take(m_width - (2 * borderThickness)),
                                            [](auto const chr) { return chr == 0; })) {
                        row_startEnd.second--;
                        rd_loc++;
                    }
                    // We break only if we can't remove all we need from the end, we shall try from the beginning
                    else { break; }
                }
                // Rows: Can remove from the beginning?
                for (size_t skip                                    = (borderThickness * m_width) + borderThickness;
                     (skip < m_matrix.size() && rd_loc != 0); skip += m_width) {
                    if (std::ranges::all_of(std::views::drop(m_matrix, skip) |
                                                std::views::take(m_width - (2 * borderThickness)),
                                            [](auto const chr) { return chr == 0; })) {
                        row_startEnd.first++;
                        rd_loc++;
                    }
                    // We cannot safely resize (downsize) because each row has some data
                    else { return false; }
                }
            }

            // Cols: We only do the following if we are trying to remove cols
            if (colDelta < 0) {
                auto cd_loc = colDelta;
                // Cols: Can remove from the end?
                for (size_t dropAdj = borderThickness + 1; (dropAdj < m_width && cd_loc != 0); ++dropAdj) {
                    if (std::ranges::all_of(
                            detail::pf_views_stride(std::views::drop(m_matrix, m_width - dropAdj), m_width),
                            [](auto const chr) { return chr == 0; })) {
                        col_startEnd.second--;
                        cd_loc++;
                    }
                    else {
                        break;
                    } // We break only if we can't remove all we need from the end, we shall try from the beginning
                }

                // Cols: Can remove from the beginning?
                for (size_t dropAdj = borderThickness; (dropAdj < m_width && cd_loc != 0); ++dropAdj) {
                    if (std::ranges::all_of(detail::pf_views_stride(std::views::drop(m_matrix, dropAdj), m_width),
                                            [](auto const chr) { return chr == 0; })) {
                        col_startEnd.first++;
                        cd_loc++;
                    }
                    else { return false; }
                }
            }

            size_t const tarItemCount = tarHeight * tarWidth;
            if (tarItemCount <= m_matrix.size()) {
                for (size_t curRow = 0; curRow < (row_startEnd.second - row_startEnd.first); ++curRow) {

                    std::ranges::rotate(
                        m_matrix.begin() + ((curRow + borderThickness) * tarWidth) + borderThickness,
                        m_matrix.begin() + (((row_startEnd.first + curRow) * m_width) + (col_startEnd.first)),
                        m_matrix.begin() + (((row_startEnd.first + curRow) * m_width) + (col_startEnd.second)));
                }
                m_matrix.resize(tarItemCount);
            }

            // tarItemCount > m_matrix_size =>
            else {
                while (m_matrix.size() < tarItemCount) { m_matrix.push_back(0); }

                long long const rd_floored = std::max(0ll, rowDelta);
                long long const cd_floored = std::max(0ll, colDelta);

                for (size_t curRow = 0; curRow < (row_startEnd.second - row_startEnd.first); ++curRow) {
                    std::ranges::rotate(
                        m_matrix.rbegin() + ((curRow + borderThickness + rd_floored) * tarWidth) + borderThickness +
                            cd_floored,
                        m_matrix.rbegin() +
                            (tarItemCount - (((row_startEnd.second - 1 - curRow) * m_width) + (col_startEnd.second))),
                        m_matrix.rbegin() +
                            (tarItemCount - (((row_startEnd.second - 1 - curRow) * m_width) + (col_startEnd.first))));
                }
            }

            m_height = tarHeight;
            m_width  = tarWidth;
            return true;
        }

        constexpr Overlay _compute_overlayWith_impl(Shape const &other) const {
            ZoneScopedN("overlayWith_impl");

            Overlay res{.ol_shp{Shape::make(m_height, m_width)}};

            if (m_height == 0uz or m_width == 0uz) { return res; } // Early exit

            auto const mv       = get_mdspanOfSelf();
            auto const mv_other = other.get_mdspanOfSelf();

            {
                ZoneScopedN("overlayWith_impl_pointCounting");
                for (size_t r = 0; r < (m_height * m_width); r += m_width) {

                    for (size_t c = 0; c < m_width; ++c) {
                        res.pointsOverlaid         += (m_matrix[r + c] != 0) && (other.m_matrix[r + c] != 0);
                        res.pointsAdded            += (m_matrix[r + c] == 0) && (other.m_matrix[r + c] != 0);
                        res.ol_shp.m_matrix[r + c]  = (m_matrix[r + c] != 0 || other.m_matrix[r + c] != 0);
                    }
                }
            }

            Shape touch    = Shape::make(m_height, m_width);
            Shape notTouch = Shape::make(m_height, m_width);

            {
                ZoneScopedN("overlayWith_touchCounting");
                for (size_t r = 1; r < m_height - 1; ++r) {

                    for (size_t c = 1; c < m_width - 1; ++c) {
                        if (mv_other[r, c] == 0) { continue; }

                        res.bordersTouching += (mv[r - 1, c] != 0) && (mv_other[r - 1, c] == 0);
                        res.bordersTouching += (mv[r, c - 1] != 0) && (mv_other[r, c - 1] == 0);
                        res.bordersTouching += (mv[r, c + 1] != 0) && (mv_other[r, c + 1] == 0);
                        res.bordersTouching += (mv[r + 1, c] != 0) && (mv_other[r + 1, c] == 0);

                        touch.m_matrix[(r - 1) * m_width + c] |= (mv[r - 1, c] != 0) && (mv_other[r - 1, c] == 0);
                        touch.m_matrix[r * m_width + c - 1]   |= (mv[r, c - 1] != 0) && (mv_other[r, c - 1] == 0);
                        touch.m_matrix[r * m_width + c + 1]   |= (mv[r, c + 1] != 0) && (mv_other[r, c + 1] == 0);
                        touch.m_matrix[(r + 1) * m_width + c] |= (mv[r + 1, c] != 0) && (mv_other[r + 1, c] == 0);

                        res.bordersNotTouching += (mv[r - 1, c] == 0) && (mv_other[r - 1, c] == 0);
                        res.bordersNotTouching += (mv[r, c - 1] == 0) && (mv_other[r, c - 1] == 0);
                        res.bordersNotTouching += (mv[r, c + 1] == 0) && (mv_other[r, c + 1] == 0);
                        res.bordersNotTouching += (mv[r + 1, c] == 0) && (mv_other[r + 1, c] == 0);

                        notTouch.m_matrix[(r - 1) * m_width + c] |= (mv[r - 1, c] == 0) && (mv_other[r - 1, c] == 0);
                        notTouch.m_matrix[r * m_width + c - 1]   |= (mv[r, c - 1] == 0) && (mv_other[r, c - 1] == 0);
                        notTouch.m_matrix[r * m_width + c + 1]   |= (mv[r, c + 1] == 0) && (mv_other[r, c + 1] == 0);
                        notTouch.m_matrix[(r + 1) * m_width + c] |= (mv[r + 1, c] == 0) && (mv_other[r + 1, c] == 0);
                    }
                }
            }
            Shape gapPastMemo    = res.ol_shp;
            Shape filledPastMemo = res.ol_shp;

            auto &gapMemo    = gapPastMemo.m_matrix;
            auto &filledMemo = filledPastMemo.m_matrix;

            static thread_local std::vector<std::pair<int, int>> stack;
            stack.reserve(m_height * m_width);

            auto flood_gap_component = [&](int const row, int const col) -> bool {
                ZoneScopedN("overlayWith_flood_gap_component");
                stack.clear();
                stack.emplace_back(row, col);
                gapMemo[row * m_width + col] = 1;

                while (! stack.empty()) {
                    auto const [r, c] = std::move(stack.back());
                    stack.pop_back();

                    if (r > 0 && (gapMemo[(r - 1) * m_width + c] == 0)) {
                        gapMemo[(r - 1) * m_width + c] = 1;
                        stack.emplace_back((r - 1), c);
                    }
                    if ((r + 1) < m_height && (gapMemo[(r + 1) * m_width + c] == 0)) {
                        gapMemo[(r + 1) * m_width + c] = 1;
                        stack.emplace_back((r + 1), c);
                    }
                    if (c > 0 && (gapMemo[r * m_width + c - 1] == 0)) {
                        gapMemo[r * m_width + c - 1] = 1;
                        stack.emplace_back(r, c - 1);
                    }
                    if ((c + 1) < m_width && (gapMemo[r * m_width + c + 1] == 0)) {
                        gapMemo[r * m_width + c + 1] = 1;
                        stack.emplace_back(r, c + 1);
                    }
                }
                return true;
            };

            auto flood_gap_launcher = [&](int const row, int const col) -> size_t {
                ZoneScopedN("overlayWith_flood_gap_launcher");
                size_t res = 0uz;
                if (row > 0 && gapMemo[(row - 1) * m_width + col] == 0) { res += flood_gap_component(row - 1, col); }
                if ((row + 1) < m_height && gapMemo[(row + 1) * m_width + col] == 0) {
                    res += flood_gap_component(row + 1, col);
                }
                if (col > 0 && gapMemo[row * m_width + col - 1] == 0) { res += flood_gap_component(row, col - 1); }
                if ((col + 1) < m_width && gapMemo[row * m_width + col + 1] == 0) {
                    res += flood_gap_component(row, col + 1);
                }
                return res;
            };

            auto flood_filled_component = [&](int const row, int const col) -> bool {
                ZoneScopedN("overlayWith_flood_filled_component");
                stack.clear();
                stack.emplace_back(row, col);
                filledMemo[(row * m_width) + col] = 0;

                while (! stack.empty()) {
                    auto const [r, c] = std::move(stack.back());
                    stack.pop_back();
                    size_t const idx = (r * m_width) + c;

                    if (r > 0 && (filledMemo[idx - m_width] != 0)) {
                        filledMemo[idx - m_width] = 0;
                        stack.emplace_back(r - 1, c);
                    }
                    if ((r + 1) < m_height && (filledMemo[idx + m_width] != 0)) {
                        filledMemo[idx + m_width] = 0;
                        stack.emplace_back(r + 1, c);
                    }
                    if (c > 0 && (filledMemo[idx - 1] != 0)) {
                        filledMemo[idx - 1] = 0;
                        stack.emplace_back(r, c - 1);
                    }
                    if ((c + 1) < m_width && (filledMemo[idx + 1] != 0)) {
                        filledMemo[idx + 1] = 0;
                        stack.emplace_back(r, c + 1);
                    }
                }
                return true;
            };

            for (int row = 0; row < m_height; ++row) {
                for (int col = 0; col < m_width; ++col) {
                    if (other.m_matrix[(row * m_width) + col] != 0) { res.gapsCount += flood_gap_launcher(row, col); }
                }
            }

            for (int row = 0; row < m_height; ++row) {
                for (int col = 0; col < m_width; ++col) {
                    if (filledMemo[(row * m_width) + col] != 0 && flood_filled_component(row, col)) {
                        res.shapesCount++;
                    }
                }
            }


            res.pointsTouching    = touch.count_filled();
            res.pointsNotTouching = notTouch.count_filled();
            double const denomP   = std::max(static_cast<double>(res.pointsTouching + res.pointsNotTouching), 1.0);
            double const denomB   = std::max(static_cast<double>(res.bordersTouching + res.bordersNotTouching), 1.0);

            res.surfacePointsCovered_relative = res.pointsTouching / denomP;
            res.surfacePointsOpened_relative  = res.pointsNotTouching / denomP;

            res.surfaceCovered_relative = res.bordersTouching / denomB;
            res.surfaceOpened_relative  = res.bordersNotTouching / denomB;

            return res;
        }

        template <typename T>
        requires more_concepts::container<T> && more_concepts::container<typename T::value_type>
        constexpr static std::pair<size_t, size_t> _get_maxHeightMaxWidth(T const &VofV) {
            return std::pair{
                VofV.size(),
                std::ranges::fold_left(std::views::transform(VofV, [](auto const &line) { return line.size(); }), 0uz,
                                       [&](auto &&init, auto const &oneLen) { return std::max(init, oneLen); })};
        }

    public:
        static Shape make(size_t const sqsz) {
            return Shape{.m_height = sqsz, .m_width = sqsz, .m_matrix = matrix_type(sqsz * sqsz, 0)};
        }

        static Shape make(size_t const tarHeight, size_t const tarWidth) {
            return Shape{.m_height = tarHeight, .m_width = tarWidth, .m_matrix = matrix_type(tarHeight * tarWidth, 0)};
        }

        template <typename T>
        requires more_concepts::container<T> && more_concepts::container<typename T::value_type>
        static Shape make(T const &VofV) {
            auto const [height, maxWidth] = _get_maxHeightMaxWidth(VofV);
            Shape res{.m_height = height, .m_width = maxWidth, .m_matrix = matrix_type{}};
            res.m_matrix.reserve(height * maxWidth);

            for (auto const &line : VofV) {
                // TODO: It might not be such a good idea to do static_cast here ... need to investigate at some point
                res.m_matrix.append_range(std::views::transform(
                    line, [](auto const &oneChar) { return static_cast<unsigned char>(oneChar); }));
                for (size_t id = line.size(); id < maxWidth; ++id) { res.m_matrix.push_back(0); }
            }
            return res;
        }

        template <typename T>
        requires more_concepts::container<T> && more_concepts::container<typename T::value_type>
        static Shape make(T const &VofV, size_t const borderThickness) {
            auto const [height, maxWidth] = _get_maxHeightMaxWidth(VofV);
            size_t const heightInclBorder = (height + (2 * borderThickness));
            size_t const widthInclBorder  = (maxWidth + (2 * borderThickness));
            Shape        res{.m_height = heightInclBorder, .m_width = widthInclBorder, .m_matrix = matrix_type()};
            res.m_matrix.reserve(heightInclBorder * widthInclBorder);

            for (size_t id = 0; id < (borderThickness * widthInclBorder); ++id) { res.m_matrix.push_back(0); }
            for (auto const &line : VofV) {
                for (size_t id = 0; id < borderThickness; ++id) { res.m_matrix.push_back(0); }
                // TODO: It might not be such a good idea to do static_cast here ... need to investigate at some point
                res.m_matrix.append_range(std::views::transform(
                    line, [](auto const &oneChar) { return static_cast<unsigned char>(oneChar); }));
                for (size_t id = line.size(); id < (maxWidth + borderThickness); ++id) { res.m_matrix.push_back(0); }
            }
            for (size_t id = 0; id < (borderThickness * widthInclBorder); ++id) { res.m_matrix.push_back(0); }
            return res;
        }

        template <typename T, size_t H, size_t W, size_t borderThickness_c = 0uz>
        requires std::convertible_to<T, value_type>
        static Shape make(std::array<std::array<T, W>, H> const &src) {
            auto out        = Shape::make(H + (2 * borderThickness_c), W + (2 * borderThickness_c));
            auto matrixView = out.get_mdspanOfSelf();
            for (size_t r = borderThickness_c; r < (H + borderThickness_c); ++r) {
                for (size_t c = borderThickness_c; c < (H + borderThickness_c); ++c) {
                    matrixView[r, c] = src[r - borderThickness_c][c - borderThickness_c] ? 1 : 0;
                }
            }
            return out;
        }

        void reset() { std::ranges::fill(m_matrix, 0); }
        void reset(size_t const tarHeight, size_t const tarWidth) {
            m_height = tarHeight;
            m_width  = tarWidth;
            m_matrix.resize(tarHeight * tarWidth);
            reset();
        }
        // constexpr std::array<char, 3> map{46, 35, 118};
        // Shape &operator=(Shape const &) = default;
        // Shape &operator=(Shape &&)      = default;


        // RET: -1 => empty, 0 => partial, 1 => filled
        int is_emptyOrFilled() const {
            size_t const count = count_filled();
            if (count == 0) { return -1; }
            if (count == (m_height * m_width)) { return 1; }
            return 0;
        }

        bool has_sameSizeAs(Shape const &other) { return ((m_height == other.m_height) && (m_width == other.m_width)); }

        constexpr size_t count_filled() const {
            return std::ranges::count_if(m_matrix, [](auto oneCell) { return oneCell != 0; });
        }

        template <size_t borderThickness_c = 0uz>
        constexpr size_t count_filledBorderLess() const {
            size_t count      = 0;
            auto   matrixView = get_mdspanOfSelf();
            for (size_t r = borderThickness_c; r < (m_height - borderThickness_c); ++r) {
                for (size_t c = borderThickness_c; c < (m_width - borderThickness_c); ++c) {
                    count += (matrixView[r, c] != 0);
                }
            }
            return count;
        }

        constexpr size_t count_filledBorderLess(size_t const borderThickness) const {
            size_t count      = 0;
            auto   matrixView = get_mdspanOfSelf();
            for (size_t r = borderThickness; r < (m_height - borderThickness); ++r) {
                for (size_t c = borderThickness; c < (m_width - borderThickness); ++c) {
                    count += (matrixView[r, c] != 0);
                }
            }
            return count;
        }
        constexpr bool has_overlapWith(Shape const &other) const {
            for (size_t i = 0; i < m_matrix.size(); ++i) {
                if (m_matrix[i] != 0 && other.m_matrix[i] != 0) { return true; }
            }
            return false;
        }

        constexpr Overlay compute_overlayWith(Shape const &other) const {

            // Same case
            if (other.m_height == m_height && other.m_width == m_width) { return _compute_overlayWith_impl(other); }

            // Other smaller or equal
            else if (other.m_height <= m_height && other.m_width <= m_width) {
                auto otherAdj = other;
                otherAdj.resize_safe(m_height, m_width);
                return _compute_overlayWith_impl(otherAdj);
            }

            // Other bigger or equal
            else if (other.m_height >= m_height && other.m_width >= m_width) {
                auto selfAdj = *this;
                selfAdj.resize_safe(other.m_height, other.m_width);
                return selfAdj._compute_overlayWith_impl(other);
            }

            // Mixed case
            else {
                auto selfAdj = *this;
                selfAdj.resize_safe(std::max(m_height, other.m_height), std::max(m_width, other.m_width));

                auto otherAdj = other;
                otherAdj.resize_safe(std::max(m_height, other.m_height), std::max(m_width, other.m_width));

                return selfAdj._compute_overlayWith_impl(otherAdj);
            }
        }


        constexpr void flip_v() {
            auto mdspn = get_mdspanOfSelf();
            for (size_t rowID = 0uz; rowID < (m_height / 2); ++rowID) {
                for (size_t colID = 0uz; colID < m_width; ++colID) {
                    std::swap(mdspn[rowID, colID], mdspn[m_height - rowID - 1, colID]);
                }
            }
        }
        constexpr void flip_h() {
            auto mdspn = get_mdspanOfSelf();
            for (size_t rowID = 0uz; rowID < m_height; ++rowID) {
                for (size_t colID = 0uz; colID < (m_width / 2); ++colID) {
                    std::swap(mdspn[rowID, colID], mdspn[rowID, m_width - colID - 1]);
                }
            }
        }

        constexpr Shape rotateCopy_left() const {
            Shape res{.m_height = m_width, .m_width = m_height, .m_matrix = matrix_type(m_height * m_width, 0)};
            auto  mdspn_src = get_mdspanOfSelf();
            auto  mdspn_res = res.get_mdspanOfSelf();

            // Swapped indices between 'src' and 'res' + 'flipped' height index
            for (size_t srcRow = 0uz; srcRow < m_height; ++srcRow) {
                for (size_t srcCol = 0uz; srcCol < m_width; ++srcCol) {
                    mdspn_res[res.m_height - srcCol - 1, srcRow] = mdspn_src[srcRow, srcCol];
                }
            }
            return res;
        }

        constexpr Shape rotateCopy_right() const {
            Shape res{.m_height = m_width, .m_width = m_height, .m_matrix = matrix_type(m_height * m_width, 0)};
            auto  mdspn_src = get_mdspanOfSelf();
            auto  mdspn_res = res.get_mdspanOfSelf();

            // Swapped indices between 'src' and 'res' + 'flipped' height index
            for (size_t srcRow = 0uz; srcRow < m_height; ++srcRow) {
                for (size_t srcCol = 0uz; srcCol < m_width; ++srcCol) {
                    mdspn_res[srcCol, res.m_width - srcRow - 1] = mdspn_src[srcRow, srcCol];
                }
            }
            return res;
        }

        enum class RotFlip : std::uint32_t {
            None    = 0,
            Rot     = (1 << 0),
            Flip    = (1 << 1),
            RotFlip = (11 << 0)
        };

        constexpr std::vector<Shape> compute_alternsRotFlip_dispatch(RotFlip const rf) const {
            switch (rf) {
                case RotFlip::RotFlip: return this->compute_alternsRotFlip();
                case RotFlip::Rot:     return this->compute_alternsRot();
                case RotFlip::Flip:    return this->compute_alternsFlip();
                default:               return std::vector<Shape>{*this};
            }
            std::unreachable();
        }

        constexpr std::vector<Shape> compute_alternsRot() const {
            auto                                                               shpCpy = this->rotateCopy_left();
            ankerl::unordered_dense::set<Shape, standard::hashing::XXH3Hasher> hlprMP;

            hlprMP.insert(shpCpy);
            shpCpy.flip_v();
            hlprMP.insert(shpCpy);
            shpCpy.flip_h();
            hlprMP.insert(shpCpy);
            shpCpy.flip_v();
            hlprMP.insert(shpCpy);

            return std::vector<Shape>(hlprMP.begin(), hlprMP.end());
        }

        constexpr std::vector<Shape> compute_alternsFlip() const {
            auto                                                               shpCpy = *this;
            ankerl::unordered_dense::set<Shape, standard::hashing::XXH3Hasher> hlprMP;

            hlprMP.insert(shpCpy);
            shpCpy.flip_v();
            hlprMP.insert(shpCpy);
            shpCpy.flip_h();
            hlprMP.insert(shpCpy);
            shpCpy.flip_v();
            hlprMP.insert(shpCpy);

            return std::vector<Shape>(hlprMP.begin(), hlprMP.end());
        }

        constexpr std::vector<Shape> compute_alternsRotFlip() const {
            auto                                                               shpCpy     = *this;
            auto                                                               shpCpy_rot = shpCpy.rotateCopy_left();
            ankerl::unordered_dense::set<Shape, standard::hashing::XXH3Hasher> hlprMP;

            hlprMP.insert(shpCpy);
            shpCpy.flip_v();
            hlprMP.insert(shpCpy);
            shpCpy.flip_h();
            hlprMP.insert(shpCpy);
            shpCpy.flip_v();
            hlprMP.insert(shpCpy);

            hlprMP.insert(shpCpy_rot);
            shpCpy_rot.flip_v();
            hlprMP.insert(shpCpy_rot);
            shpCpy_rot.flip_h();
            hlprMP.insert(shpCpy_rot);
            shpCpy_rot.flip_v();
            hlprMP.insert(shpCpy_rot);

            return std::vector<Shape>(hlprMP.begin(), hlprMP.end());
        }


        constexpr bool verify_borderExists(size_t const borderThickness) {
            if (std::ranges::any_of(std::views::take(m_matrix, borderThickness * m_width),
                                    [](auto const chr) { return (chr != 0); })) {
                return false;
            }
            if (std::ranges::any_of(std::views::drop(m_matrix, m_matrix.size() - (borderThickness * m_width)),
                                    [](auto const chr) { return (chr != 0); })) {
                return false;
            }
            for (size_t skip = 0uz; skip < borderThickness; ++skip) {
                if (std::ranges::any_of(detail::pf_views_stride(std::views::drop(m_matrix, skip), m_width),
                                        [](auto const chr) { return (chr != 0); })) {
                    return false;
                }
                if (std::ranges::any_of(
                        detail::pf_views_stride(std::views::drop(m_matrix, m_width - 1 - skip), m_width),
                        [](auto const chr) { return (chr != 0); })) {
                    return false;
                }
            }
            return true;
        }

        // If something part of the border had to be changed then returns 'true', otherwise returns 'false'
        constexpr bool change_forceBorder(size_t const borderThickness, value_type const forceBorderItemsTo = 0) {
            bool res = false;
            for (auto &oneChr : std::views::take(m_matrix, borderThickness * m_width)) {
                res    |= (oneChr != forceBorderItemsTo);
                oneChr  = forceBorderItemsTo;
            }
            for (auto &oneChr : std::views::drop(m_matrix, m_matrix.size() - (borderThickness * m_width))) {
                res    |= (oneChr != forceBorderItemsTo);
                oneChr  = forceBorderItemsTo;
            }
            for (size_t skip = 0uz; skip < borderThickness; ++skip) {
                for (auto &oneChr : detail::pf_views_stride(std::views::drop(m_matrix, skip), m_width)) {
                    res    |= (oneChr != forceBorderItemsTo);
                    oneChr  = forceBorderItemsTo;
                }
                for (auto &oneChr : detail::pf_views_stride(std::views::drop(m_matrix, m_width - 1 - skip), m_width)) {
                    res    |= (oneChr != forceBorderItemsTo);
                    oneChr  = forceBorderItemsTo;
                }
            }

            return res;
        }

        // Adds empty 'border' (ie. empty lines around the shape area)
        // Performs m_height += (2-borderThickness), m_width += (2-borderThickness)
        // Note: Adds border even if there already is a 'border' previously
        Shape &add_border(size_t const borderThickness) {
            size_t const targetTotalSz =
                m_matrix.size() + (2 * borderThickness) * ((2 * borderThickness) + m_height + m_width);
            size_t const target_height = m_height + (2 * borderThickness);
            size_t const target_width  = m_width + (2 * borderThickness);
            size_t const sizeDelta     = targetTotalSz - m_matrix.size();

            while (m_matrix.size() < targetTotalSz) { m_matrix.push_back(0); }
            // size_t const rowDelta = target_width - m_width;

            // for (int oldRowID = (static_cast<int>(m_height) - 1); oldRowID > 0; --oldRowID) {
            //     size_t const fromStart = oldRowID * m_height;
            //     std::ranges::rotate(m_matrix.begin() + fromStart, m_matrix.begin() + fromStart + m_width,
            //                         m_matrix.begin() + fromStart + m_width + (rowDelta * oldRowID) + borderThickness
            //                         +
            //                             (target_width * borderThickness));
            // }

            for (size_t r_rowID = 0uz; r_rowID < m_height; ++r_rowID) {
                std::ranges::rotate(m_matrix.rbegin() + ((r_rowID + borderThickness) * target_width) + borderThickness,
                                    m_matrix.rbegin() + sizeDelta + (r_rowID * m_width),
                                    m_matrix.rbegin() + sizeDelta + (r_rowID * m_width) + m_width);
            }

            m_height = target_height;
            m_width  = target_width;
            return *this;
        }


        // Return true if resized, returns false otherwise (no change to 'this')
        bool resize_safe(std::optional<size_t> const tarHeight, std::optional<size_t> const tarWidth) {
            size_t const th = tarHeight.value_or(m_height);
            size_t const tw = tarWidth.value_or(m_width);

            if ((th == m_height) && (tw == m_width)) {
                return false; // Cannot resize
            }
            else { return _resize_withBorder(th, tw, 0uz); }
            std::unreachable();
        }


        // Return true if resized, returns false otherwise (no change to 'this')
        bool resize_safe(std::optional<size_t> const tarHeight, std::optional<size_t> const tarWidth,
                         size_t const borderThickness) {
            size_t const th = tarHeight.value_or(m_height);
            size_t const tw = tarWidth.value_or(m_width);

            if ((borderThickness > th) || (borderThickness > tw) || ((th == m_height) && (tw == m_width))) {
                return false; // Cannot resize
            }

            // Means 'no border' ... so same as baseline version
            else if (borderThickness == 0uz) { return resize_safe(tarHeight, tarWidth); }

            // Verify that there actually is a border at this time, if not return 'false'
            // May want to 'add_border' first if returned here or the data is somehow different than expected
            else if (not verify_borderExists(borderThickness)) { return false; }

            else { return _resize_withBorder(th, tw, borderThickness); }
            std::unreachable();
        }


        bool resize_squareify() {
            size_t const targetSz = std::max(m_height, m_width);
            return resize_safe(targetSz, targetSz);
        }

        // Always resizes
        // Return true if it were 'forced', returns false if as if by resize_safe()
        // UNIMPLEMENTED
        bool resize_force(std::optional<size_t> const tarHeight, std::optional<size_t> const tarWidth) {

            // if (resize_safe(target_sqsz)) { return false; }

            // m_sqsz = target_sqsz;
            // return true;
            return false;
        }


        // UNIMPLEMENTED
        std::string stringify_self(size_t const borderThickness = 0) {
            std::string res;
            if (((2 * borderThickness) >= m_height) || ((2 * borderThickness) >= m_width)) { return res; }

            static constexpr std::array<char, 3> map{46, 35, 118};
            auto const                           inner =
                polyfills::submdspan(get_mdspanOfSelf(), std::pair{borderThickness, m_height - borderThickness},
                                     std::pair{borderThickness, m_width - borderThickness});
            res.reserve(inner.extent(0) * (inner.extent(1) + 1));

            for (size_t r = 0; r < inner.extent(0); ++r) {
                for (size_t c = 0; c < inner.extent(1); ++c) {
                    res.push_back(map[std::min<size_t>(inner[r, c], map.size() - 1)]);
                }
                res.push_back('\n');
            }
            return res;
        }

        friend constexpr void XXH3Hash(Shape const &input, XXH3_state_t *state) {
            XXH3_64bits_update(state, input.m_matrix.data(),
                               sizeof(typename std::remove_cvref_t<decltype(input.m_matrix)>::value_type) *
                                   input.m_matrix.size());
            XXH3_64bits_update(state, &input.m_height, sizeof(decltype(input.m_height)));
            XXH3_64bits_update(state, &input.m_width, sizeof(decltype(input.m_width)));
        }
    };

private:
    struct Overlay {
        Shape ol_shp;

        size_t pointsAdded        = 0;
        size_t pointsOverlaid     = 0;
        size_t bordersTouching    = 0;
        size_t bordersNotTouching = 0;

        size_t pointsTouching    = 0;
        size_t pointsNotTouching = 0;

        size_t gapsCount   = 0;
        size_t shapesCount = 0;

        double surfacePointsCovered_relative = 0.0;
        double surfacePointsOpened_relative  = std::numeric_limits<double>::infinity();

        double surfaceCovered_relative = 0.0;
        double surfaceOpened_relative  = std::numeric_limits<double>::infinity();
    };

public:
    // Objects of this type are stored inside m_pastComputed and are referenced from m_frontierTiles
    struct PastRes {
        size_t   uncoveredBySurr = 0;
        AlternID ol_shpID{};
        Overlay  ol_res{};
    };

    // This is the type that gets created by findNextStep functions
    struct OptionAtPos {
        enum class Type : uint8_t {
            Gapless = 1,
            Dividing,
            Gapcreating
        };

        Pos      p{};
        AlternID altID{};
        // Shape    shp{};
        // PastRes pr_option{};

        Type type = Type::Gapcreating;
    };

    using possibsByShape_t      = std::vector<std::vector<PastRes>>;
    using frontierTilePossibs_t = std::optional<std::reference_wrapper<possibsByShape_t>>;
    using pastResMap_t =
        ankerl::unordered_dense::segmented_map<Shape, possibsByShape_t, incom::standard::hashing::XXH3Hasher>;
    using consideredOptionsByShape_t = std::vector<std::vector<OptionAtPos>>;

    struct SolverPolicy {
        struct SelectionState {
            double lowestSOR = std::numeric_limits<double>::max();

            [[nodiscard]] bool shouldStopOn(double const curAdjSOR) const { return lowestSOR < curAdjSOR; }

            [[nodiscard]] bool hasNewBest(double const curAdjSOR) const { return lowestSOR > curAdjSOR; }

            void reset(consideredOptionsByShape_t &toConsider, double const curAdjSOR) {
                lowestSOR = curAdjSOR;
                for (auto &toConsLine : toConsider) { toConsLine.clear(); }
            }
        };

        [[nodiscard]] static bool allows(PastRes const &toCheck) { return toCheck.ol_res.pointsOverlaid == 0; }

        [[nodiscard]] static bool prefer_precomputed(PastRes const &l, PastRes const &r) {
            double const soDif = r.ol_res.surfaceOpened_relative - l.ol_res.surfaceOpened_relative;
            if (soDif == 0.0) { return l.ol_res.pointsAdded > r.ol_res.pointsAdded; }
            return (soDif > 0.0);
        }
    };

    std::optional<std::tuple<Pos, AlternID, Shape>> solve_oneStep() {
        // TODO: Need to create findNextStep_onePossibility
        auto selOpt = findNextStep_covering()
                          .or_else([this]() { return findNextStep_regular(); })
                          .or_else([this]() { return findNextStep_withGap(); })
                          .and_then([this](auto const &VofV_csos) { return select_oneCSO(VofV_csos); });

        if (! selOpt.has_value()) { return std::nullopt; }
        OptionAtPos const &sel = selOpt.value();

        std::tuple<Pos, AlternID, Shape> const res{sel.p, sel.altID,
                                                   m_shapes_alterns.at(sel.altID.shpID).at(sel.altID.alternID)};

        auto const surrPoss = get_surrOverlappingPoss_forWindowsAt(std::get<0>(res), m_shapeOLCount_border);
        erase_fromFrontier(surrPoss);
        add_shapeAtPos(sel.p, std::get<2>(res));
        add_toFrontier(surrPoss);
        m_placedShapes++;

        m_useableCount_perShape[std::get<1>(res).shpID]--;

        for (Pos const &uncov : verify_uncoverable(sel)) {
            if (std::ranges::find_if(m_uncoverableFrontierPoss, [&](auto const &item) {
                    return (item.y == uncov.y && item.x == uncov.x);
                }) == m_uncoverableFrontierPoss.end()) {
                m_uncoverableFrontierPoss.push_back(uncov);
            }
        }

        return res;
    }

    std::vector<std::tuple<Pos, AlternID, Shape>> solve_XSteps(size_t numOfSteps = std::numeric_limits<size_t>::max()) {
        std::vector<std::tuple<Pos, AlternID, Shape>> res;
        while (numOfSteps-- > 0) {
            if (auto oneStepRes = solve_oneStep()) { res.push_back(std::move(oneStepRes.value())); }
            else { break; }
        }
        return res;
    }

public:
    template <size_t N>
    BoxPacker_2D(size_t const area_ySize, size_t const area_xSize,
                 std::vector<std::array<std::array<bool, N>, N>> const &shps, std::vector<size_t> const &shps_counts,
                 pastResMap_t const &pastReslts = {})
        : BoxPacker_2D(area_ySize, area_xSize,
                       std::views::transform(shps, [&](auto const &smallerShp) { return Shape::make(smallerShp); }) |
                           std::ranges::to<std::vector>(),
                       shps_counts, pastReslts) {}

    template <size_t N>
    BoxPacker_2D(size_t const area_ySize, size_t const area_xSize,
                 std::vector<std::vector<std::array<std::array<bool, N>, N>>> const &shpsAltrs,
                 std::vector<size_t> const &shps_counts, pastResMap_t const &pastReslts = {})
        : BoxPacker_2D(area_ySize, area_xSize,
                       std::views::transform(shpsAltrs,
                                             [&](auto const &oneShpAltrns) {
                                                 return std::views::transform(
                                                            oneShpAltrns,
                                                            [&](auto const &item) { return Shape::make(item); }) |
                                                        std::ranges::to<std::vector>();
                                             }) |
                           std::ranges::to<std::vector>(),
                       shps_counts, pastReslts) {}

    BoxPacker_2D()                     = delete;
    BoxPacker_2D(BoxPacker_2D const &) = delete;
    BoxPacker_2D(BoxPacker_2D &&)      = default;
    ~BoxPacker_2D()                    = default;

    BoxPacker_2D &operator=(BoxPacker_2D const &) = delete;
    BoxPacker_2D &operator=(BoxPacker_2D &&)      = delete;

    BoxPacker_2D(size_t const area_ySize, size_t const area_xSize, std::vector<std::vector<Shape>> const &shps_alterns,
                 std::vector<size_t> const &shps_counts, pastResMap_t const &pastResults = {})
        : m_shapes_alterns([&]() {
              size_t maxSize{1uz}; // The sizes need to be at least 1uz

              for (auto const &shpAlternLine : shps_alterns) {
                  for (auto const &shpAlt : shpAlternLine) {
                      maxSize = std::max(maxSize, shpAlt.m_height);
                      maxSize = std::max(maxSize, shpAlt.m_width);
                  }
              }

              return std::views::transform(shps_alterns,
                                           [&](auto const &shpAlternLine) {
                                               return std::views::transform(shpAlternLine,
                                                                            [&](auto const &oneAltern) {
                                                                                Shape res = oneAltern;
                                                                                if (res.m_height != maxSize ||
                                                                                    res.m_width != maxSize) {
                                                                                    res.resize_safe(maxSize, maxSize);
                                                                                }
                                                                                res.add_border(1);
                                                                                return res;
                                                                            }) |
                                                      std::ranges::to<std::vector>();
                                           }) |
                     std::ranges::to<std::vector>();
          }()),
          m_shapes_alterns_totalCount(std::ranges::fold_left(
              std::views::transform(m_shapes_alterns, [](auto const &alts) { return alts.size(); }), 0uz, std::plus{})),
          m_sqsz(std::ranges::fold_left(
              std::views::transform(m_shapes_alterns,
                                    [](auto const &shpAlternLine) {
                                        return shpAlternLine.size() == 0 ? 0uz : shpAlternLine.front().m_height;
                                    }),
              0uz, [](size_t init, size_t oneMaxSize) { return std::max(init, oneMaxSize); })),
          m_shapesMaxEmpty(((m_sqsz - 2) * (m_sqsz - 2)) -
                           [&] {
                               return std::ranges::fold_left(
                                   std::views::transform(m_shapes_alterns,
                                                         [&](auto &vecOfAlterns) {
                                                             return vecOfAlterns.empty()
                                                                        ? ((m_sqsz - 2) * (m_sqsz - 2))
                                                                        : vecOfAlterns.front().count_filledBorderLess();
                                                         }),
                                   ((m_sqsz - 2uz) * (m_sqsz - 2uz)),
                                   [](size_t init, size_t oneFilledCount) { return std::min(init, oneFilledCount); });
                           }()),
          m_useableCount_perShape(std::views::take(shps_counts, std::min(shps_counts.size(), shps_alterns.size())) |
                                  std::ranges::to<std::vector>()),
          m_shapesRatios_orig(
              std::views::transform(m_useableCount_perShape,
                                    [sum = std::max(static_cast<double>(std::ranges::fold_left(m_useableCount_perShape,
                                                                                               size_t{0}, std::plus{})),
                                                    1.0)](size_t oneCount) { return oneCount / sum; }) |
              std::ranges::to<std::vector>()),
          m_area_ySize(area_ySize + (2 * m_sqsz - 4)), m_area_xSize(area_xSize + (2 * m_sqsz - 4)),
          m_area(m_area_ySize * m_area_xSize, 0), m_frontier_ySz(m_area_ySize + 1 - m_sqsz),
          m_frontier_xSz(m_area_xSize + 1 - m_sqsz),
          m_frontierTiles(m_frontier_ySz * m_frontier_xSz, frontierTilePossibs_t{}),


          m_pastComputed(pastResults) {

        assert(m_sqsz > 2);

        reset_area();

        m_useableCount_perShape.resize(m_shapes_alterns.size(), 0);

        prime_fprng();
    }
    BoxPacker_2D(size_t const area_ySize, size_t const area_xSize, std::vector<Shape> const &shps,
                 std::vector<size_t> const &shps_counts,
                 Shape::RotFlip const shpsAlternsMethod = Shape::RotFlip::RotFlip, pastResMap_t const &pastResults = {})
        : BoxPacker_2D(
              area_ySize, area_xSize,
              [&]() {
                  size_t maxSize{1uz}; // The sizes need to be at least 1uz
                  for (auto const &shpAlt : shps) {
                      maxSize = std::max(maxSize, shpAlt.m_height);
                      maxSize = std::max(maxSize, shpAlt.m_width);
                  }

                  return std::views::transform(shps,
                                               [&](auto const &oneShp) {
                                                   Shape shpCpy = oneShp;
                                                   if (shpCpy.m_height != maxSize || shpCpy.m_width != maxSize) {
                                                       shpCpy.resize_safe(maxSize, maxSize);
                                                   }
                                                   shpCpy.add_border(1);

                                                   return shpCpy.compute_alternsRotFlip_dispatch(shpsAlternsMethod);
                                               }) |
                         std::ranges::to<std::vector>();
              }(),
              shps_counts, pastResults) {}

private:
    std::vector<std::vector<Shape>> const m_shapes_alterns;
    size_t const                          m_shapes_alterns_totalCount;

    size_t const m_sqsz                = 0uz; // This is for 'Shapes' used by the BoxPacker
    size_t const m_shapeOLCount_full   = (2 * m_sqsz) - 1;
    size_t const m_shapeOLCount_border = m_shapeOLCount_full - 2;
    size_t const m_shapeOLCount_inside = m_shapeOLCount_full - 4;
    size_t const m_shapesMaxEmpty      = 0;

    std::vector<size_t>                       m_useableCount_perShape;
    std::vector<double>                       m_shapesRatios_orig;
    incom::standard::random::FastPseudoRandom m_fprng;

    size_t          m_placedShapes            = 0uz;
    pastResMap_t    m_pastComputed            = {};
    std::deque<Pos> m_uncoverableFrontierPoss = {};

    size_t                     m_area_ySize;
    size_t                     m_area_xSize;
    std::vector<unsigned char> m_area;

    size_t                             m_frontier_ySz;
    size_t                             m_frontier_xSz;
    std::vector<frontierTilePossibs_t> m_frontierTiles{(m_area_ySize + 3 - m_sqsz) * (m_area_xSize + 3 - m_sqsz),
                                                       frontierTilePossibs_t{}};


public:
    polyfills::mdspan<const unsigned char, polyfills::dextents<size_t, 2>> get_mdspanOfArea() const {
        return polyfills::mdspan<const unsigned char, polyfills::dextents<size_t, 2>>(m_area.data(), m_area_ySize,
                                                                                      m_area_xSize);
    }
    polyfills::mdspan<unsigned char, polyfills::dextents<size_t, 2>> get_mdspanOfArea() {
        return polyfills::mdspan<unsigned char, polyfills::dextents<size_t, 2>>(m_area.data(), m_area_ySize,
                                                                                m_area_xSize);
    }

    auto get_mdspanOfFrontier() const {
        return polyfills::mdspan<const frontierTilePossibs_t, polyfills::dextents<size_t, 2>>(
            m_frontierTiles.data(), m_frontier_ySz, m_frontier_xSz);
    }
    auto get_mdspanOfFrontier() {
        return polyfills::mdspan<frontierTilePossibs_t, polyfills::dextents<size_t, 2>>(m_frontierTiles.data(),
                                                                                        m_frontier_ySz, m_frontier_xSz);
    }

    std::string get_areaState() const {
        std::string                   toPrint{};
        constexpr std::array<char, 3> map{46, 35, 118};

        auto area_view = get_mdspanOfArea();

        for (size_t lineID = 0uz; lineID < m_area_ySize; ++lineID) {
            for (size_t colID = 0uz; colID < m_area_xSize; ++colID) {
                toPrint.push_back(map[area_view[lineID, colID]]);
            }
            toPrint.push_back('\n');
        }
        return toPrint;
    }

    std::pair<size_t, size_t> get_areaSize() const { return {m_area_ySize, m_area_xSize}; }

    std::pair<size_t, size_t> get_areaSize_borderless() const {
        return {m_area_ySize > (2 * m_sqsz - 4) ? m_area_ySize - (2 * m_sqsz - 4) : 0U,
                (m_area_xSize > (2 * m_sqsz - 4) ? m_area_xSize - (2 * m_sqsz - 4) : 0U)};
    }

    std::pair<size_t, size_t> get_emptyFilled() const noexcept {
        std::pair<size_t, size_t> res{};
        for (size_t id = 0uz; id < m_area.size(); ++id) {
            res.first  += (m_area[id] == 0);
            res.second += (m_area[id] != 0);
        }
        return res;
    }

    size_t get_useableShapeCountRemaining() const noexcept {
        return std::ranges::fold_left(m_useableCount_perShape, size_t{0}, std::plus{});
    }

    size_t get_pastResSize() const noexcept { return m_pastComputed.size(); }

public:
    BoxPacker_2D clone_keepShapeData(std::vector<size_t> const &shps_counts) const {
        auto const [rDim, cDim] = get_areaSize_borderless();
        return BoxPacker_2D(rDim, cDim, m_shapes_alterns, shps_counts, m_pastComputed);
    }

    BoxPacker_2D clone_keepShapeData(size_t const area_ySize, size_t const area_xSize,
                                     std::vector<size_t> const &shps_counts) const {
        return BoxPacker_2D(area_ySize, area_xSize, m_shapes_alterns, shps_counts, m_pastComputed);
    }

    void reset_allButNotPastComputed(std::vector<size_t> const &shps_counts) {
        reset_placedCounter();
        reset_area();
        reset_frontier();
        reset_useableShapeCounts(shps_counts);
        prime_fprng();
    }

    void reset_allButNotPastComputed(size_t area_ySize, size_t area_xSize, std::vector<size_t> const &shps_counts) {
        reset_placedCounter();
        reset_area(area_ySize, area_xSize);
        reset_frontier();
        reset_useableShapeCounts(shps_counts);
        prime_fprng();
    }

    void reset_allButNotPastComputed(size_t area_ySize, size_t area_xSize, std::vector<size_t> const &shps_counts,
                                     Pos const &p) {
        reset_placedCounter();
        reset_area(area_ySize, area_xSize);
        reset_frontier(p);
        reset_useableShapeCounts(shps_counts);
        prime_fprng();
    }

    void reset_placedCounter() noexcept { m_placedShapes = 0uz; }

    void reset_area() noexcept {
        std::ranges::fill(m_area, 0);
        auto areaView = get_mdspanOfArea();

        size_t const borderThickness = std::max(2uz, m_sqsz) - 2uz;

        for (size_t rowID : std::views::iota(0uz) | std::views::take(borderThickness)) {
            for (size_t colID = 0; colID < m_area_xSize; ++colID) { areaView[rowID, colID] = 1; }
        }
        for (size_t rowID : std::views::iota(m_area_ySize - (m_sqsz - 2)) | std::views::take(borderThickness)) {
            for (size_t colID = 0; colID < m_area_xSize; ++colID) { areaView[rowID, colID] = 1; }
        }
        for (size_t rowID = (m_sqsz - 2); rowID < (m_area_ySize - (m_sqsz - 2)); ++rowID) {
            for (size_t colID : std::views::iota(0uz) | std::views::take(borderThickness)) {
                areaView[rowID, colID] = 1;
            }
            for (size_t colID : std::views::iota(m_area_xSize - (m_sqsz - 2)) | std::views::take(borderThickness)) {
                areaView[rowID, colID] = 1;
            }
        }
    }

    void reset_area(size_t const area_ySize, size_t const area_xSize) {
        m_area_ySize = area_ySize + (2 * m_sqsz - 4);
        m_area_xSize = area_xSize + (2 * m_sqsz - 4);

        m_area.resize((m_area_ySize) * (m_area_xSize));
        reset_area();
    }

    void reset_frontier() {
        m_frontier_ySz = (m_area_ySize + 1 - m_sqsz);
        m_frontier_xSz = (m_area_xSize + 1 - m_sqsz);

        m_frontierTiles.resize(m_frontier_ySz * m_frontier_xSz);

        std::ranges::fill(m_frontierTiles, std::nullopt);
    }

    void reset_frontier(Pos const &firstTilePos) {
        reset_frontier();

        auto const ftPos =
            Pos{.y = static_cast<long long>(std::min(firstTilePos.y, static_cast<long long>(m_area_ySize - m_sqsz))),
                .x = static_cast<long long>(std::min(firstTilePos.x, static_cast<long long>(m_area_xSize - m_sqsz)))};

        auto firstTile                                         = get_windowAtPos(ftPos).value();
        m_frontierTiles.at((ftPos.y * m_area_xSize + ftPos.x)) = std::ref(getOrCompute_possibsFor(firstTile));
    }

    // UNIMPLEMENTED
    void reset_frontier(std::vector<Pos> const &) noexcept {}

    void reset_useableShapeCounts(std::vector<size_t> const &shps_counts) {
        m_useableCount_perShape = shps_counts;
        m_useableCount_perShape.resize(m_shapes_alterns.size(), 0);
        m_shapesRatios_orig =
            std::views::transform(m_useableCount_perShape,
                                  [sum = std::max(static_cast<double>(std::ranges::fold_left(m_useableCount_perShape,
                                                                                             size_t{0}, std::plus{})),
                                                  1.0)](size_t oneCount) { return oneCount / sum; }) |
            std::ranges::to<std::vector>();
    }

    void reset_pastComputed() noexcept {
        m_pastComputed.clear();
        reset_frontier();
    }

public:
    size_t erase_fromFrontier(std::vector<Pos> const &shapePoss) {
        size_t res_removed = 0;
        for (Pos const &onePos : shapePoss) {
            if (m_frontierTiles[((onePos.y) * m_frontier_xSz) + onePos.x] != std::nullopt) { res_removed++; }
            m_frontierTiles[((onePos.y) * m_frontier_xSz) + onePos.x] = std::nullopt;
        }
        return res_removed;
    }


    bool add_toFrontier(Pos const &onePos) {
        auto const window = get_windowAtPos(onePos);
        // If the window is has more filled spaces than maximum available empty spaces of any shape => nothing can be
        // placed here
        if (window.has_value() && window.value().count_filledBorderLess<1uz>() <= m_shapesMaxEmpty) {
            auto &possibsForWindow = getOrCompute_possibsFor(window.value());
            if (possibsForWindow.size() > 0) {
                m_frontierTiles.at((onePos.y * m_frontier_xSz) + onePos.x) = std::ref(possibsForWindow);
                return true;
            }
        }
        return false;
    }


    size_t add_toFrontier(std::vector<Pos> const &shapePoss) {
        size_t resCount = 0;
        for (auto const &onePos : shapePoss) { resCount += add_toFrontier(onePos); }
        return resCount;
    }

    size_t add_toFrontier_allCorners() {
        size_t resCount = 0;
        if (m_area_ySize >= m_sqsz && m_area_xSize >= m_sqsz) {
            for (auto const [rStart, cStart] : std::array<std::array<size_t, 2>, 4>{
                     {{0, 0},
                      {0, m_area_xSize + 3 - m_sqsz - m_sqsz},
                      {m_area_ySize + 3 - m_sqsz - m_sqsz, 0},
                      {m_area_ySize + 3 - m_sqsz - m_sqsz, m_area_xSize + 3 - m_sqsz - m_sqsz}}}) {

                for (auto const rAdj : std::views::iota(0) | std::views::take(m_sqsz - 2)) {
                    for (auto const cAdj : std::views::iota(0) | std::views::take(m_sqsz - 2)) {
                        resCount += add_toFrontier(
                            Pos{static_cast<long long>(rStart + rAdj), static_cast<long long>(cStart + cAdj)});
                    }
                }
            }
        }

        return resCount;
    }

private:
    [[nodiscard]] static OptionAtPos make_OptionAtPos(Pos const &p, PastRes const &pr, OptionAtPos::Type const type) {
        return OptionAtPos{.p = p, .altID = pr.ol_shpID, .type = type};
    }

    [[nodiscard]] bool has_useableAlternatives(std::vector<PastRes> const &oneShpAltsVec) const {
        if (oneShpAltsVec.empty()) { return false; }
        return m_useableCount_perShape.at(oneShpAltsVec.front().ol_shpID.shpID) > 0;
    }

    template <typename Predicate>
    void collect_consideredOptionsAt(consideredOptionsByShape_t &sinkInto, bool &anyFilled,
                                     typename SolverPolicy::SelectionState &selectionState, Pos const &candidatePos,
                                     possibsByShape_t const    &possibilitiesByShape,
                                     std::vector<double> const &perShpScoringAdj, OptionAtPos::Type const type,
                                     Predicate const &predicate) const {
        for (auto const &v_pr2 : std::views::filter(possibilitiesByShape, [this](auto const &oneShpAltsVec) {
                 return has_useableAlternatives(oneShpAltsVec);
             })) {
            for (PastRes const &pr : v_pr2) {
                if (! predicate(pr)) { continue; }
                double const curAdjSOR = pr.ol_res.surfaceOpened_relative * perShpScoringAdj.at(pr.ol_shpID.shpID);

                if (selectionState.shouldStopOn(curAdjSOR)) { break; }
                if (selectionState.hasNewBest(curAdjSOR)) { selectionState.reset(sinkInto, curAdjSOR); }

                sinkInto.at(pr.ol_shpID.shpID).push_back(make_OptionAtPos(candidatePos, pr, type));
                anyFilled = true;
            }
        }
    }

    std::vector<double> compute_perShapeScoringAdjustments() const {
        double const sum = static_cast<double>(std::ranges::fold_left(m_useableCount_perShape, 0uz, std::plus{}));

        return std::views::zip(m_useableCount_perShape, m_shapesRatios_orig) |
               std::views::transform([&](auto const &oneCount) {
                   return (std::get<0>(oneCount) == 0 ? std::numeric_limits<double>::max()
                                                      : (sum / std::get<0>(oneCount))) *
                          std::get<1>(oneCount);
               }) |
               std::ranges::to<std::vector>();
    }

    possibsByShape_t &getOrCompute_possibsFor(Shape const &tile) {
        auto insRes = m_pastComputed.insert({tile, possibsByShape_t(m_shapes_alterns.size())});
        if (insRes.second) {
            possibsByShape_t &vpr = insRes.first->second;

            struct EvalItem {
                size_t                 shpID    = 0;
                size_t                 alternID = 0;
                std::optional<PastRes> accepted{};
            };

            std::vector<EvalItem> work;
            work.reserve(m_shapes_alterns_totalCount);

            for (size_t shpID = 0; shpID < m_shapes_alterns.size(); ++shpID) {
                for (size_t alternID = 0; alternID < m_shapes_alterns[shpID].size(); ++alternID) {
                    work.emplace_back(shpID, alternID, std::nullopt);
                }
            }

            // Phase 1: parallel compute (each iteration writes only to its own EvalItem)
            std::for_each(std::execution::par_unseq, work.begin(), work.end(), [&](EvalItem &item) {
                if (not tile.has_overlapWith(m_shapes_alterns[item.shpID][item.alternID])) {
                    item.accepted =
                        PastRes{.ol_shpID{item.shpID, item.alternID},
                                .ol_res = tile.compute_overlayWith(m_shapes_alterns[item.shpID][item.alternID])};
                }
            });

            // Phase 2: sequential merge into shared container
            for (auto &item : work) {
                if (item.accepted.has_value()) { vpr.at(item.shpID).push_back(std::move(*item.accepted)); }
            }

            // for (size_t shpID = 0; shpID < m_shapes_alterns.size(); ++shpID) {
            //     for (size_t alternID = 0; alternID < m_shapes_alterns.at(shpID).size(); ++alternID) {
            //         auto rs = PastRes{.ol_shpID{shpID, alternID},
            //                           .ol_res = tile.compute_overlayWith(m_shapes_alterns.at(shpID).at(alternID))};
            //         if (SolverPolicy::allows(rs)) { vpr.at(shpID).push_back(rs); }
            //     }
            // }

            for (auto &vprLine : vpr) { std::ranges::sort(vprLine, SolverPolicy::prefer_precomputed); }
        }
        return insRes.first->second;
    }

    std::optional<consideredOptionsByShape_t> findNextStep_covering() {
        if (m_uncoverableFrontierPoss.empty()) { return std::nullopt; }

        std::vector<unsigned char>                                       trackerUncov(m_area_ySize * m_area_xSize, 0);
        polyfills::mdspan<unsigned char, polyfills::dextents<size_t, 2>> trackerUncov_view(trackerUncov.data(),
                                                                                           m_area_ySize, m_area_xSize);
        std::vector<unsigned char>                                       tracker(m_area_ySize * m_area_xSize, 0);
        polyfills::mdspan<unsigned char, polyfills::dextents<size_t, 2>> tracker_view(tracker.data(), m_area_ySize,
                                                                                      m_area_xSize);

        auto const perShpScoringAdj = compute_perShapeScoringAdjustments();

        // std::cout << get_areaState() << '\n' << '\n';
        auto areaView     = get_mdspanOfArea();
        auto frontierView = get_mdspanOfFrontier();

        while (! m_uncoverableFrontierPoss.empty()) {
            Pos const seed = m_uncoverableFrontierPoss.front();
            if (areaView[static_cast<size_t>(seed.y), static_cast<size_t>(seed.x)] != 0) {
                m_uncoverableFrontierPoss.pop_front();
                continue;
            }

            auto explr =
                explorers::Chebyshev([&](std::array<size_t, 2> const &item) { return areaView[item[0], item[1]] == 0; },
                                     std::array{static_cast<size_t>(seed.y), static_cast<size_t>(seed.x)},
                                     std::array{m_area_ySize, m_area_xSize});

            auto eva = [&](std::vector<Pos> const &poss) -> std::optional<consideredOptionsByShape_t> {
                consideredOptionsByShape_t            toConsider(m_shapes_alterns.size());
                bool                                  anyFilled = false;
                typename SolverPolicy::SelectionState selectionState{};

                for (auto const &onePos : poss) {
                    for (auto const &prPos : get_surrOverlappingPoss<false>(onePos)) {
                        if (prPos.y < 0 || prPos.x < 0 || prPos.y >= static_cast<long long>(m_frontier_ySz) ||
                            prPos.x >= static_cast<long long>(m_frontier_xSz)) {
                            continue;
                        }
                        // if (trackerUncov_view[onePos.y, onePos.x] == 1) {}
                        size_t const py = static_cast<size_t>(prPos.y);
                        size_t const px = static_cast<size_t>(prPos.x);

                        if (tracker_view[py, px] != 0) { continue; }
                        tracker_view[py, px] = 1;

                        if (! frontierView[py, px].has_value()) { continue; }

                        collect_consideredOptionsAt(
                            toConsider, anyFilled, selectionState, prPos, frontierView[py, px].value().get(),
                            perShpScoringAdj, OptionAtPos::Type::Gapcreating, [&](auto const &item) {
                                return item.ol_res.ol_shp.m_matrix[(onePos.y - prPos.y) * item.ol_res.ol_shp.m_width +
                                                                   (onePos.x - prPos.x)] != 0;
                                ;
                            });
                    }
                }

                if (! anyFilled) { return std::nullopt; }
                return toConsider;
            };

            size_t           level = 0;
            std::vector<Pos> posToEval;

            while (! explr.is_atEnd()) {
                auto const [yLoc, xLoc]       = explr.get_next();
                trackerUncov_view[yLoc, xLoc] = 1;
                posToEval.push_back({static_cast<long long>(std::move(yLoc)), static_cast<long long>(std::move(xLoc))});

                if (level < explr.m_queueIDToUseNext) {
                    if (auto potRes = eva(posToEval); potRes.has_value()) { return potRes; }
                    posToEval.clear();
                }

                level = explr.m_queueIDToUseNext;
            }
            if (auto potRes = eva(posToEval); potRes.has_value()) { return potRes; }

            std::erase_if(m_uncoverableFrontierPoss,
                          [&](auto const &item) { return (trackerUncov_view[item.y, item.x] == 1); });
        }


        return std::nullopt;
    }

    std::optional<consideredOptionsByShape_t> findNextStep_regular() const {
        consideredOptionsByShape_t            toConsider(m_shapes_alterns.size());
        bool                                  anyFilled = false;
        typename SolverPolicy::SelectionState selectionState{};
        auto const                            perShpScoringAdj = compute_perShapeScoringAdjustments();

        Pos        curPos{.y = -1, .x = -1};
        auto const frontierView = get_mdspanOfFrontier();
        for (curPos.y = 0uz; curPos.y < m_frontier_ySz; ++curPos.y) {
            for (curPos.x = 0uz; curPos.x < m_frontier_xSz; ++curPos.x) {
                if (auto const &frontierPos = frontierView[curPos.y, curPos.x]) {
                    collect_consideredOptionsAt(toConsider, anyFilled, selectionState, curPos,
                                                frontierPos.value().get(), perShpScoringAdj, OptionAtPos::Type::Gapless,
                                                [](auto const &item) { return item.ol_res.gapsCount < 2; });
                }
            }
        }
        if (! anyFilled) { return std::nullopt; }
        return toConsider;
    }

    std::optional<consideredOptionsByShape_t> findNextStep_withGap() const {
        consideredOptionsByShape_t            toConsider(m_shapes_alterns.size());
        bool                                  anyFilled = false;
        typename SolverPolicy::SelectionState selectionState{};
        auto const                            perShpScoringAdj = compute_perShapeScoringAdjustments();

        Pos        curPos{.y = -1, .x = -1};
        auto const frontierView = get_mdspanOfFrontier();
        for (curPos.y = 0uz; curPos.y < m_frontier_ySz; ++curPos.y) {
            for (curPos.x = 0uz; curPos.x < m_frontier_xSz; ++curPos.x) {
                if (auto const &frontierPos = frontierView[curPos.y, curPos.x]) {
                    collect_consideredOptionsAt(
                        toConsider, anyFilled, selectionState, curPos, frontierPos.value().get(), perShpScoringAdj,
                        OptionAtPos::Type::Dividing, [](auto const &item) { return item.ol_res.gapsCount > 1; });
                }
            }
        }
        if (! anyFilled) { return std::nullopt; }
        return toConsider;
    }

    std::optional<OptionAtPos> select_oneCSO(consideredOptionsByShape_t const &VofV_csos) {
        size_t const optsCount = std::ranges::fold_left(
            VofV_csos, size_t{0}, [](size_t init, auto const &VofCSO) { return init + VofCSO.size(); });
        if (optsCount == 0) { return std::nullopt; }

        size_t numToConsider = m_fprng.pseudoRandom_0_to(optsCount - 1) + 1;

        for (auto const &VofCSO : VofV_csos) {
            if (VofCSO.size() < numToConsider) { numToConsider -= VofCSO.size(); }
            else { return VofCSO.at(numToConsider - 1); }
        }
        assert(false);
        std::unreachable();
    }

    std::vector<Pos> verify_uncoverable(OptionAtPos const &opt) const {
        std::vector<Pos> res{};
        long long const  halfCount = static_cast<long long>(m_shapeOLCount_border / 2);

        auto       window_previous = get_windowAtPos(opt.p).value();
        auto       olResMat_view   = window_previous.get_mdspanOfSelf();
        auto const frontierView    = get_mdspanOfFrontier();
        for (long long thisShpRow = opt.p.y; thisShpRow < (opt.p.y + static_cast<long long>(m_sqsz)); ++thisShpRow) {
            for (long long thisShpCol = opt.p.x; thisShpCol < (opt.p.x + static_cast<long long>(m_sqsz));
                 ++thisShpCol) {
                if (olResMat_view[thisShpRow - opt.p.y, thisShpCol - opt.p.x] != 0) { continue; }

                bool onePointCovered      = false;
                bool atLeastOneWithoutGap = false;

                for (long long influRow = thisShpRow - (static_cast<long long>(m_sqsz) - 2); influRow < thisShpRow;
                     ++influRow) {
                    for (long long influCol = thisShpCol - (static_cast<long long>(m_sqsz) - 2); influCol < thisShpCol;
                         ++influCol) {
                        if (! is_posValid(Pos{.y = influRow, .x = influCol})) { continue; }
                        else if (! frontierView[influRow, influCol].has_value()) { continue; }
                        else {
                            size_t const computedID = ((m_sqsz * (thisShpRow - influRow)) + (thisShpCol - influCol));

                            for (auto const &prLine : frontierView[influRow, influCol].value().get()) {
                                for (PastRes const &onePR : prLine) {
                                    onePointCovered      |= onePR.ol_res.ol_shp.m_matrix.at(computedID);
                                    atLeastOneWithoutGap |= (onePR.ol_res.gapsCount < 2);
                                }
                            }
                        }
                    }
                }
                if (! onePointCovered || ! atLeastOneWithoutGap) {
                    res.push_back(Pos{.y = thisShpRow, .x = thisShpCol});
                }
            }
        }

        (void)halfCount;
        return res;
    }

private:
    template <bool INCLBorder = true>
    std::vector<Pos> get_surrOverlappingPoss(Pos const &shp_pos) const {

        std::vector<Pos> res;
        size_t const     adj = (m_sqsz - 2 + static_cast<size_t>(INCLBorder));

        for (long long row = shp_pos.y - static_cast<long long>(adj);
             row < (shp_pos.y + static_cast<long long>(INCLBorder)); ++row) {
            for (long long col = shp_pos.x - static_cast<long long>(adj);
                 col < (shp_pos.x + static_cast<long long>(INCLBorder)); ++col) {
                if (row < 0 || row > (static_cast<long long>(m_area_ySize - m_sqsz)) || col < 0 ||
                    col > (static_cast<long long>(m_area_xSize - m_sqsz))) {}
                else { res.push_back(Pos{.y = row, .x = col}); }
            }
        }

        return res;
    }

    std::vector<Pos> get_surrOverlappingPoss_forWindowsAt(Pos const &shp_pos, size_t olCount) const {
        assert(olCount % 2 == 1);

        long long const  halfCount = static_cast<long long>(olCount / 2);
        std::vector<Pos> res;

        for (long long row = shp_pos.y - halfCount; row < (shp_pos.y + halfCount + 1); ++row) {
            for (long long col = shp_pos.x - halfCount; col < (shp_pos.x + halfCount + 1); ++col) {
                if (row < 0 || row > (static_cast<long long>(m_area_ySize - m_sqsz)) || col < 0 ||
                    col > (static_cast<long long>(m_area_xSize - m_sqsz))) {}
                else { res.push_back(Pos{.y = row, .x = col}); }
            }
        }

        return res;
    }

    std::optional<Shape> get_windowAtPos(Pos const &shapePos) const {

        if (shapePos.y >= 0 && shapePos.y <= static_cast<long long>(m_area_ySize - m_sqsz) && shapePos.x >= 0 &&
            shapePos.x <= static_cast<long long>(m_area_xSize - m_sqsz)) {
            auto res      = Shape::make(m_sqsz);
            auto res_view = res.get_mdspanOfSelf();
            auto areaView = get_mdspanOfArea();
            for (long long row = shapePos.y; row < shapePos.y + static_cast<long long>(m_sqsz); ++row) {
                for (long long col = shapePos.x; col < (shapePos.x + static_cast<long long>(m_sqsz)); ++col) {
                    res_view[row - shapePos.y, col - shapePos.x] = areaView[row, col];
                }
            }
            return res;
        }
        return std::nullopt;
    }

    bool is_posValid(Pos const &p) const noexcept {

        if (p.y < 0 || p.y > (static_cast<long long>(m_area_ySize) - static_cast<long long>(m_sqsz)) || p.x < 0 ||
            p.x > (static_cast<long long>(m_area_xSize - static_cast<long long>(m_sqsz)))) {
            return false;
        }
        return true;
    }

    bool set_windowAtPos(Pos const &shapePos, PastRes const &pr) {
        if (! is_posValid(shapePos)) { return false; }
        auto rm_view  = pr.ol_res.ol_shp.get_mdspanOfSelf();
        auto areaView = get_mdspanOfArea();
        for (long long r = shapePos.y; r < (shapePos.y + static_cast<long long>(m_sqsz)); ++r) {
            for (long long c = shapePos.x; c < (shapePos.x + static_cast<long long>(m_sqsz)); ++c) {
                areaView[r, c] = rm_view[r - shapePos.y, c - shapePos.x];
            }
        }
        return true;
    }

    bool set_windowAtPos(Pos const &shapePos, Shape const &newWindow) {
        if (! is_posValid(shapePos)) { return false; }
        auto nw_view  = newWindow.get_mdspanOfSelf();
        auto areaView = get_mdspanOfArea();
        for (long long r = shapePos.y; r < (shapePos.y + static_cast<long long>(m_sqsz)); ++r) {
            for (long long c = shapePos.x; c < (shapePos.x + static_cast<long long>(m_sqsz)); ++c) {
                areaView[r, c] = nw_view[r - shapePos.y, c - shapePos.x];
            }
        }
        return true;
    }
    bool add_shapeAtPos(Pos const &shapePos, Shape const &newWindow) {
        if (! is_posValid(shapePos)) { return false; }
        auto nw_view  = newWindow.get_mdspanOfSelf();
        auto areaView = get_mdspanOfArea();
        for (long long r = shapePos.y; r < (shapePos.y + static_cast<long long>(m_sqsz)); ++r) {
            for (long long c = shapePos.x; c < (shapePos.x + static_cast<long long>(m_sqsz)); ++c) {
                areaView[r, c] = nw_view[r - shapePos.y, c - shapePos.x] != 0 ? nw_view[r - shapePos.y, c - shapePos.x]
                                                                              : areaView[r, c];
            }
        }
        return true;
    }

private:
    size_t prime_fprng() noexcept {
        size_t const res = hash_ofSelf();
        set_pseudoRandomSeed(res);
        return res;
    }

    void set_pseudoRandomSeed(uint64_t seed) noexcept { m_fprng.setSeed(seed); }

    std::size_t hash_ofSelf() const noexcept {
        XXH3_state_t *state = XXH3_createState();
        XXH3_64bits_reset_withSeed(state, 0);

        XXH3_64bits_update(state, &m_sqsz, sizeof(size_t));
        XXH3_64bits_update(state, &m_area_ySize, sizeof(size_t));
        XXH3_64bits_update(state, &m_area_xSize, sizeof(size_t));

        for (auto const &alternsLine : m_shapes_alterns) {
            for (auto const &shp : alternsLine) { XXH3Hash(shp, state); }
        }
        XXH3_64bits_update(state, m_useableCount_perShape.data(),
                           sizeof(typename std::remove_cvref_t<decltype(m_useableCount_perShape)>::value_type) *
                               m_useableCount_perShape.size());

        XXH64_hash_t result = XXH3_64bits_digest(state);
        XXH3_freeState(state);
        return result;
    }
};


// ##################################
// ### IMPLEMENTATIONS
// ##################################


} // namespace packing

} // namespace incom::standard::solvers_TEMP
