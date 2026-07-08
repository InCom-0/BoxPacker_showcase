#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <limits>
#include <mdspan>
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <ankerl/unordered_dense.h>

#include <incstd/core/explorers.hpp>
#include <incstd/core/hashing.hpp>
#include <incstd/core/matrix.hpp>
#include <incstd/core/random.hpp>

#include <incstd/polyfills/mdspan.hpp>

namespace incom::standard::solvers_TEMP {
using namespace incom::standard;

namespace packing {

class BoxPacker_2D {
#if defined(INCSTD_MDSPAN_UNDER_KOKKOS)
    template <class IndexType, size_t Rank>
    using pf_dextents = Kokkos::dextents<IndexType, Rank>;

    template <class ElementType, class Extents>
    using pf_mdspan = Kokkos::mdspan<ElementType, Extents>;

    template <class... Args>
    constexpr decltype(auto) pf_submdspan(Args &&...args) {
        return Kokkos::submdspan(std::forward<Args>(args)...);
    }
#else
    template <class IndexType, size_t Rank>
    using pf_dextents = std::dextents<IndexType, Rank>;

    template <class ElementType, class Extents>
    using pf_mdspan = std::mdspan<ElementType, Extents>;

    template <class... Args>
    constexpr decltype(auto) pf_submdspan(Args &&...args) {
        return std::submdspan(std::forward<Args>(args)...);
    }
#endif


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

    struct Shape {
        struct OverlayRes {
            size_t sqsz = 0;

            std::vector<std::vector<char>> res_matrix;

            size_t pointsAdded        = 0;
            size_t pointsOverlaid     = 0;
            size_t bordersTouching    = 0;
            size_t bordersNotTouching = 0;

            size_t pointsTouching    = 0;
            size_t pointsNotTouching = 0;

            size_t gapsCount   = 0;
            size_t shapesCount = 0;

            double surfacePointsCovered_relative = 0.0;
            double surfacePointsOpened_relative  = std::numeric_limits<double>::max();

            double surfaceCovered_relative = 0.0;
            double surfaceOpened_relative  = std::numeric_limits<double>::max();
        };

        size_t                         m_sqsz = 0;
        std::vector<std::vector<char>> m_matrix;

        Shape() = default;
        explicit Shape(size_t sqsz) : m_sqsz(sqsz), m_matrix(sqsz, std::vector<char>(sqsz, 0)) {

            auto sp    = std::mdspan(m_matrix.data(), 2, 2);
            auto subsp = std::submdspan(sp, std::pair{0, 0}, std::pair{1, 1});

            std::vector<int>                                matrix;
            std::mdspan<int, std::dextents<std::size_t, 2>> view(matrix.data(), 3, 3);
            auto inner = std::submdspan(view, std::pair{1, 1}, std::pair{2, 2});
        }

        auto operator<=>(Shape const &other) const = default;

        template <size_t N>
        static Shape from_inner(std::array<std::array<bool, N>, N> const &src, size_t sqsz) {
            assert(sqsz == N + 2);
            Shape out(sqsz);
            for (size_t r = 0; r < N; ++r) {
                for (size_t c = 0; c < N; ++c) { out.m_matrix[r + 1][c + 1] = src[r][c] ? 1 : 0; }
            }
            return out;
        }

        template <size_t N>
        static Shape from_full(std::array<std::array<bool, N>, N> const &src, size_t sqsz) {
            assert(sqsz == N);
            Shape out(sqsz);
            for (size_t r = 0; r < N; ++r) {
                for (size_t c = 0; c < N; ++c) { out.m_matrix[r][c] = src[r][c] ? 1 : 0; }
            }
            return out;
        }

        int is_emptyOrFilled() const {
            size_t const count = count_filled();
            if (count == 0) { return -1; }
            if (count == (m_sqsz * m_sqsz)) { return 1; }
            return 0;
        }

        size_t count_filled() const {
            size_t count = 0;
            for (size_t r = 0; r < m_sqsz; ++r) {
                for (size_t c = 0; c < m_sqsz; ++c) { count += static_cast<size_t>(m_matrix[r][c] != 0); }
            }
            return count;
        }

        size_t count_filledBorderLess() const {
            size_t count = 0;
            if (m_sqsz < 3) { return 0; }
            for (size_t r = 1; r < (m_sqsz - 1); ++r) {
                for (size_t c = 1; c < (m_sqsz - 1); ++c) { count += static_cast<size_t>(m_matrix[r][c] != 0); }
            }
            return count;
        }

        OverlayRes compute_overlayWith(Shape const &other) const {
            assert(m_sqsz == other.m_sqsz);
            OverlayRes res{};
            res.sqsz       = m_sqsz;
            res.res_matrix = std::vector<std::vector<char>>(m_sqsz, std::vector<char>(m_sqsz, 0));

            for (size_t r = 0; r < m_sqsz; ++r) {
                for (size_t c = 0; c < m_sqsz; ++c) {
                    res.pointsOverlaid   += (m_matrix[r][c] != 0) && (other.m_matrix[r][c] != 0);
                    res.pointsAdded      += (m_matrix[r][c] == 0) && (other.m_matrix[r][c] != 0);
                    res.res_matrix[r][c]  = (m_matrix[r][c] != 0 || other.m_matrix[r][c] != 0) ? 1 : 0;
                }
            }

            Shape touch(m_sqsz);
            Shape notTouch(m_sqsz);

            for (size_t r = 1; r < m_sqsz - 1; ++r) {
                for (size_t c = 1; c < m_sqsz - 1; ++c) {
                    if (other.m_matrix[r][c] == 0) { continue; }

                    res.bordersTouching += (m_matrix[r - 1][c] != 0) && (other.m_matrix[r - 1][c] == 0);
                    res.bordersTouching += (m_matrix[r][c - 1] != 0) && (other.m_matrix[r][c - 1] == 0);
                    res.bordersTouching += (m_matrix[r][c + 1] != 0) && (other.m_matrix[r][c + 1] == 0);
                    res.bordersTouching += (m_matrix[r + 1][c] != 0) && (other.m_matrix[r + 1][c] == 0);

                    touch.m_matrix[r - 1][c] |= (m_matrix[r - 1][c] != 0) && (other.m_matrix[r - 1][c] == 0);
                    touch.m_matrix[r][c - 1] |= (m_matrix[r][c - 1] != 0) && (other.m_matrix[r][c - 1] == 0);
                    touch.m_matrix[r][c + 1] |= (m_matrix[r][c + 1] != 0) && (other.m_matrix[r][c + 1] == 0);
                    touch.m_matrix[r + 1][c] |= (m_matrix[r + 1][c] != 0) && (other.m_matrix[r + 1][c] == 0);

                    res.bordersNotTouching += (m_matrix[r - 1][c] == 0) && (other.m_matrix[r - 1][c] == 0);
                    res.bordersNotTouching += (m_matrix[r][c - 1] == 0) && (other.m_matrix[r][c - 1] == 0);
                    res.bordersNotTouching += (m_matrix[r][c + 1] == 0) && (other.m_matrix[r][c + 1] == 0);
                    res.bordersNotTouching += (m_matrix[r + 1][c] == 0) && (other.m_matrix[r + 1][c] == 0);

                    notTouch.m_matrix[r - 1][c] |= (m_matrix[r - 1][c] == 0) && (other.m_matrix[r - 1][c] == 0);
                    notTouch.m_matrix[r][c - 1] |= (m_matrix[r][c - 1] == 0) && (other.m_matrix[r][c - 1] == 0);
                    notTouch.m_matrix[r][c + 1] |= (m_matrix[r][c + 1] == 0) && (other.m_matrix[r][c + 1] == 0);
                    notTouch.m_matrix[r + 1][c] |= (m_matrix[r + 1][c] == 0) && (other.m_matrix[r + 1][c] == 0);
                }
            }

            Shape gapPastMemo(m_sqsz);
            Shape filledPastMemo(m_sqsz);
            Shape curMemo(m_sqsz);

            Pos curPos{.y = 0, .x = 0};

            auto gapsRecLambda = [&](this auto const &self) -> bool {
                if (res.res_matrix[curPos.y][curPos.x] != 0) { return true; }
                if (curMemo.m_matrix[curPos.y][curPos.x] != 0) { return true; }
                curMemo.m_matrix[curPos.y][curPos.x] = 1;

                if (gapPastMemo.m_matrix[curPos.y][curPos.x] != 0) { return false; }
                gapPastMemo.m_matrix[curPos.y][curPos.x] = 1;

                for (long long row : {-1LL, 1LL}) {
                    if (curPos.y + row < 0 || curPos.y + row >= static_cast<long long>(m_sqsz)) { continue; }
                    curPos.y += row;
                    if (! self()) { return false; }
                    curPos.y -= row;
                }
                for (long long col : {-1LL, 1LL}) {
                    if (curPos.x + col < 0 || curPos.x + col >= static_cast<long long>(m_sqsz)) { continue; }
                    curPos.x += col;
                    if (! self()) { return false; }
                    curPos.x -= col;
                }
                return true;
            };

            auto filledRecLambda = [&](this auto const &self) -> bool {
                if (res.res_matrix[curPos.y][curPos.x] == 0) { return true; }
                if (curMemo.m_matrix[curPos.y][curPos.x] != 0) { return true; }
                curMemo.m_matrix[curPos.y][curPos.x] = 1;

                if (filledPastMemo.m_matrix[curPos.y][curPos.x] != 0) { return false; }
                filledPastMemo.m_matrix[curPos.y][curPos.x] = 1;

                for (long long row : {-1LL, 1LL}) {
                    if (curPos.y + row < 0 || curPos.y + row >= static_cast<long long>(m_sqsz)) { continue; }
                    curPos.y += row;
                    if (! self()) { return false; }
                    curPos.y -= row;
                }
                for (long long col : {-1LL, 1LL}) {
                    if (curPos.x + col < 0 || curPos.x + col >= static_cast<long long>(m_sqsz)) { continue; }
                    curPos.x += col;
                    if (! self()) { return false; }
                    curPos.x -= col;
                }
                return true;
            };

            for (size_t r = 0; r < m_sqsz; ++r) {
                for (size_t c = 0; c < m_sqsz; ++c) {
                    if (res.res_matrix[r][c] == 0 && gapPastMemo.m_matrix[r][c] == 0) {
                        curPos.y       = static_cast<long long>(r);
                        curPos.x       = static_cast<long long>(c);
                        curMemo        = Shape(m_sqsz);
                        res.gapsCount += gapsRecLambda();
                    }
                    if (res.res_matrix[r][c] != 0 && filledPastMemo.m_matrix[r][c] == 0) {
                        curPos.y         = static_cast<long long>(r);
                        curPos.x         = static_cast<long long>(c);
                        curMemo          = Shape(m_sqsz);
                        res.shapesCount += filledRecLambda();
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

        std::vector<Shape> compute_alternsRotFlip() const {
            namespace incmatrix = incom::standard::matrix;

            auto                                                                                m_matrix_cpy = m_matrix;
            ankerl::unordered_dense::set<decltype(m_matrix_cpy), standard::hashing::XXH3Hasher> hlprMP;

            hlprMP.insert(m_matrix_cpy);
            for (int rot_i = 0; rot_i < 3; ++rot_i) {
                incmatrix::matrixRotateLeft(m_matrix_cpy);
                hlprMP.insert(m_matrix_cpy);
            }

            for (size_t i = 0; i < (m_matrix_cpy.size() / 2); ++i) {
                std::swap(m_matrix_cpy.at(i), m_matrix_cpy.at(m_matrix_cpy.size() - 1 - i));
            }

            hlprMP.insert(m_matrix_cpy);
            for (int rot_i = 0; rot_i < 3; ++rot_i) {
                incmatrix::matrixRotateLeft(m_matrix_cpy);
                hlprMP.insert(m_matrix_cpy);
            }

            std::vector<Shape> out;
            out.reserve(hlprMP.size());
            for (auto const &item : hlprMP) {
                Shape shp(m_sqsz);
                shp.m_matrix = item;
                out.push_back(std::move(shp));
            }
            return out;
        }

        friend constexpr void XXH3Hash(Shape const &input, XXH3_state_t *state) {
            XXH3_64bits_update(state, &input.m_sqsz, sizeof(input.m_sqsz));
            for (auto const &line : input.m_matrix) {
                XXH3_64bits_update(state, line.data(),
                                   sizeof(typename std::remove_cvref_t<decltype(line)>::value_type) * line.size());
            }
        }
    };

    struct PastRes {
        struct AlternID {
            size_t shpID    = 0;
            size_t alternID = 0;
        };
        size_t            uncoveredBySurr = 0;
        AlternID          ol_shpID{};
        Shape::OverlayRes ol_res{};
    };

    struct ConsideredShapeOption {
        enum class Type : uint8_t {
            Gapless = 1,
            Dividing,
            Gapcreating
        };

        Pos     p{};
        PastRes pr_option{};

        Type type = Type::Gapcreating;
    };

    using possibilitiesByShape_t     = std::vector<std::vector<PastRes>>;
    using frontierTilePossibs_t      = std::optional<std::reference_wrapper<possibilitiesByShape_t>>;
    using consideredOptionsByShape_t = std::vector<std::vector<ConsideredShapeOption>>;
    using pastResMap_t =
        ankerl::unordered_dense::segmented_map<Shape, possibilitiesByShape_t, incom::standard::hashing::XXH3Hasher>;

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

    std::optional<std::tuple<Pos, PastRes>> solve_oneStep() {
        auto selOpt = findNextStep_covering()
                          .or_else([this]() { return findNextStep_regular(); })
                          .or_else([this]() { return findNextStep_withGap(); })
                          .and_then([this](auto const &VofV_csos) { return select_oneCSO(VofV_csos); });

        if (! selOpt.has_value()) { return std::nullopt; }

        ConsideredShapeOption const   &selCSO = selOpt.value();
        std::tuple<Pos, PastRes> const res{selCSO.p, selCSO.pr_option};

        auto const surrPoss = get_surrOverlappingPoss_forWindowsAt(std::get<0>(res), m_shapeOLCount_border);
        erase_fromFrontier(surrPoss);
        set_windowAtPos(selCSO.p, selCSO.pr_option);
        add_toFrontier(surrPoss);

        m_useableCount_perShape[std::get<1>(res).ol_shpID.shpID]--;

        for (Pos const &uncov : verify_uncoverable(selCSO)) {
            if (std::ranges::find_if(m_uncoverableFrontierPoss, [&](auto const &item) {
                    return (item.y == uncov.y && item.x == uncov.x);
                }) == m_uncoverableFrontierPoss.end()) {
                m_uncoverableFrontierPoss.push_back(uncov);
            }
        }

        return res;
    }

    std::vector<std::tuple<Pos, PastRes>> solve_XSteps(size_t numOfSteps = std::numeric_limits<size_t>::max()) {
        std::vector<std::tuple<Pos, PastRes>> res;
        while (numOfSteps-- > 0) {
            if (auto oneStepRes = solve_oneStep()) { res.push_back(std::move(oneStepRes.value())); }
            else { break; }
        }
        return res;
    }

public:
    template <size_t N>
    BoxPacker_2D(size_t sqsz, size_t area_ySize, size_t area_xSize,
                 std::vector<std::array<std::array<bool, N>, N>> const &shps, std::vector<size_t> const &shps_counts,
                 size_t const firstTile_yPos = 0, size_t const firstTile_xPos = 0, pastResMap_t const &pastReslts = {})
        : BoxPacker_2D(sqsz, area_ySize, area_xSize,
                       std::views::transform(shps,
                                             [&](auto const &smallerShp) {
                                                 return Shape::from_inner(smallerShp, sqsz).compute_alternsRotFlip();
                                             }) |
                           std::ranges::to<std::vector>(),
                       shps_counts, firstTile_yPos, firstTile_xPos, pastReslts) {}

    template <size_t N>
    BoxPacker_2D(size_t sqsz, size_t area_ySize, size_t area_xSize,
                 std::vector<std::vector<std::array<std::array<bool, N>, N>>> const &shpsAltrs,
                 std::vector<size_t> const &shps_counts, size_t const firstTile_yPos = 0,
                 size_t const firstTile_xPos = 0, pastResMap_t const &pastReslts = {})
        : BoxPacker_2D(sqsz, area_ySize, area_xSize,
                       std::views::transform(shpsAltrs,
                                             [&](auto const &oneShpAltrns) {
                                                 return std::views::transform(oneShpAltrns,
                                                                              [&](auto const &item) {
                                                                                  return Shape::from_inner(item, sqsz);
                                                                              }) |
                                                        std::ranges::to<std::vector>();
                                             }) |
                           std::ranges::to<std::vector>(),
                       shps_counts, firstTile_yPos, firstTile_xPos, pastReslts) {}

    BoxPacker_2D()                     = delete;
    BoxPacker_2D(BoxPacker_2D const &) = delete;
    BoxPacker_2D(BoxPacker_2D &&)      = default;
    ~BoxPacker_2D()                    = default;

    BoxPacker_2D &operator=(BoxPacker_2D const &) = delete;
    BoxPacker_2D &operator=(BoxPacker_2D &&)      = default;


private:
    BoxPacker_2D(size_t const sqsz, size_t const area_ySize, size_t const area_xSize,
                 std::vector<std::vector<Shape>> const &shps_alterns, std::vector<size_t> const &shps_counts,
                 size_t const firstTile_yPos = 0, size_t const firstTile_xPos = 0, pastResMap_t const &pastReslts = {})
        : m_sqsz(sqsz), m_shapeOLCount_full((2 * sqsz) - 1), m_shapeOLCount_border((2 * sqsz) - 3),
          m_shapeOLCount_inside((2 * sqsz) - 5), m_useableCount_perShape(shps_counts),
          m_area((area_ySize + 2) * (area_xSize + 2), 0), m_areaView(m_area.data(), area_ySize + 2, area_xSize + 2),
          m_frontierTiles((area_ySize + 3 - sqsz) * (area_xSize + 3 - sqsz), frontierTilePossibs_t{}),
          m_frontierTiles_view(m_frontierTiles.data(), (area_ySize + 3 - sqsz), (area_xSize + 3 - sqsz)),
          m_firstTilePos(Pos{.y = static_cast<long long>(firstTile_yPos), .x = static_cast<long long>(firstTile_xPos)}),
          m_shapes_alterns(shps_alterns),
          m_shapesMaxEmpty(((sqsz - 2) * (sqsz - 2)) -
                           [&] {
                               auto filledCounts = std::views::transform(m_shapes_alterns, [](auto &vecOfAlterns) {
                                   return vecOfAlterns.empty() ? size_t{0}
                                                               : vecOfAlterns.front().count_filledBorderLess();
                               });

                               auto       first = std::ranges::begin(filledCounts);
                               auto const last  = std::ranges::end(filledCounts);
                               if (first == last) { return size_t{0}; }

                               size_t minFilled = *first;
                               ++first;
                               return std::ranges::fold_left(std::ranges::subrange(first, last), minFilled,
                                                             [](size_t a, size_t b) { return std::min(a, b); });
                           }()),
          m_pastComputed(pastReslts) {
        assert(m_sqsz > 2);

        {
            size_t const areRows = m_areaView.extent(0);
            size_t const areCols = m_areaView.extent(1);

            for (size_t rowID : {0uz, areRows - 1uz}) {
                for (size_t colID = 0; colID < areCols; ++colID) { m_areaView[rowID, colID] = 1; }
            }
            for (size_t rowID = 1; rowID < (areRows - 1); ++rowID) {
                for (size_t colID : {0uz, areCols - 1uz}) { m_areaView[rowID, colID] = 1; }
            }
        }

        m_useableCount_perShape.resize(m_shapes_alterns.size(), 0);

        auto ratiosHlprView = std::views::transform(
            m_useableCount_perShape,
            [sum = static_cast<double>(std::ranges::fold_left(m_useableCount_perShape, size_t{0}, std::plus{}))](
                size_t oneCount) { return oneCount / std::max(sum, 1.0); });

        m_shapesRatios_orig = decltype(m_shapesRatios_orig)(ratiosHlprView.begin(), ratiosHlprView.end());

        auto const ftPos = Pos{.y = static_cast<long long>(std::min(firstTile_yPos, area_ySize - m_sqsz)),
                               .x = static_cast<long long>(std::min(firstTile_xPos, area_xSize - m_sqsz))};

        auto &ft_possibs                       = getOrCompute_possibsFor(get_windowAtPos(ftPos).value());
        m_frontierTiles_view[ftPos.y, ftPos.x] = std::ref(ft_possibs);
        prime_fprng();
    }

private:
    size_t m_sqsz                = 0;
    size_t m_shapeOLCount_full   = 0;
    size_t m_shapeOLCount_border = 0;
    size_t m_shapeOLCount_inside = 0;

    std::vector<unsigned char>                       m_area;
    pf_mdspan<unsigned char, pf_dextents<size_t, 2>> m_areaView;
    Pos                                              m_firstTilePos;
    std::vector<std::vector<Shape>>                  m_shapes_alterns;
    size_t                                           m_shapesMaxEmpty = 0;

    std::vector<size_t>                       m_useableCount_perShape;
    std::vector<double>                       m_shapesRatios_orig;
    incom::standard::random::FastPseudoRandom m_fprng;

    pastResMap_t                                             m_pastComputed;
    std::deque<Pos>                                          m_uncoverableFrontierPoss;
    std::vector<frontierTilePossibs_t>                       m_frontierTiles;
    pf_mdspan<frontierTilePossibs_t, pf_dextents<size_t, 2>> m_frontierTiles_view;

public:
    std::string get_areaState() const {
        std::string                   toPrint{};
        constexpr std::array<char, 3> map{46, 35, 118};

        for (size_t lineID = 0uz; lineID < m_areaView.extent(0); ++lineID) {
            for (size_t colID; colID < m_areaView.extent(1); ++colID) {
                toPrint.push_back(map[m_areaView[lineID, colID]]);
            }
            toPrint.push_back('\n');
        }
        return toPrint;
    }

    std::pair<size_t, size_t> get_areaSize() const { return {m_areaView.extent(0), m_areaView.extent(1)}; }

    std::pair<size_t, size_t> get_areaSize_borderless() const {
        return {m_areaView.extent(0) > 0 ? m_areaView.extent(0) - 1U : 0U,
                m_areaView.extent(0) > 0 ? (m_areaView.extent(1) > 0 ? m_areaView.extent(1) : 0U) : 0U};
    }

    std::pair<size_t, size_t> get_emptyFilled() const noexcept {
        std::pair<size_t, size_t> res{};

        if (m_areaView.extent(0) < 2 or m_areaView.extent(1) < 2) {}
        else {
            for (size_t r = 1; r < m_areaView.extent(0) - 1; ++r) {
                for (size_t c = 1; c < m_areaView.extent(1) - 1; ++c) {
                    m_areaView[r, c] == 0 ? res.first++ : res.second++;
                }
            }
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
        return BoxPacker_2D(m_sqsz, rDim, cDim, m_shapes_alterns, shps_counts, m_firstTilePos.y, m_firstTilePos.x,
                            m_pastComputed);
    }

    BoxPacker_2D clone_keepShapeData(size_t const area_ySize, size_t const area_xSize,
                                     std::vector<size_t> const &shps_counts) const {
        return BoxPacker_2D(m_sqsz, area_ySize, area_xSize, m_shapes_alterns, shps_counts, m_firstTilePos.y,
                            m_firstTilePos.x, {});
    }

    void reset_allButNotPastComputed(std::vector<size_t> const &shps_counts) {
        reset_area();
        reset_frontier();
        reset_useableShapeCounts(shps_counts);
        prime_fprng();
    }

    void reset_allButNotPastComputed(size_t area_ySize, size_t area_xSize, std::vector<size_t> const &shps_counts) {
        reset_area(area_ySize, area_xSize);
        reset_frontier();
        reset_useableShapeCounts(shps_counts);
        prime_fprng();
    }

    void reset_allButNotPastComputed(size_t area_ySize, size_t area_xSize, std::vector<size_t> const &shps_counts,
                                     Pos const &p) {
        reset_area(area_ySize, area_xSize);
        reset_frontier(p);
        reset_useableShapeCounts(shps_counts);
        prime_fprng();
    }

    void reset_area() noexcept {

        std::ranges::fill(m_area, 0);
        size_t const areRows = m_areaView.extent(0);
        size_t const areCols = m_areaView.extent(1);

        for (size_t rowID : {0uz, areRows - 1uz}) {
            for (size_t colID = 0; colID < areCols; ++colID) { m_areaView[rowID, colID] = 1; }
        }
        for (size_t rowID = 1; rowID < (areRows - 1); ++rowID) {
            for (size_t colID : {0uz, areCols - 1uz}) { m_areaView[rowID, colID] = 1; }
        }
    }

    void reset_area(size_t const area_ySize, size_t const area_xSize) {
        m_area.resize((area_ySize + 2uz) * (area_xSize + 2uz));
        m_areaView = decltype(m_areaView)(m_area.data(), area_ySize + 2uz, area_xSize + 2uz);
        reset_area();
    }

    void reset_frontier() {
        auto         firstTile = get_windowAtPos(m_firstTilePos).value();
        size_t const ftRows    = (m_areaView.extent(0) + 1 - m_sqsz);
        size_t const ftCols    = (m_areaView.extent(1) + 1 - m_sqsz);

        m_frontierTiles.resize(ftRows * ftCols);
        m_frontierTiles_view = decltype(m_frontierTiles_view)(m_frontierTiles.data(), ftRows, ftCols);

        std::ranges::fill(m_frontierTiles, std::nullopt);

        // TODO: Delete the line below once verified working
        auto &ft_possibs                                         = getOrCompute_possibsFor(firstTile);
        m_frontierTiles_view[m_firstTilePos.y, m_firstTilePos.x] = std::ref(getOrCompute_possibsFor(firstTile));
    }

    void reset_frontier(Pos const &firstTilePos) {
        auto const ftPos = Pos{.y = static_cast<long long>(
                                   std::min(firstTilePos.y, static_cast<long long>(m_areaView.extent(0) - m_sqsz))),
                               .x = static_cast<long long>(
                                   std::min(firstTilePos.x, static_cast<long long>(m_areaView.extent(1) - m_sqsz)))};
        m_firstTilePos   = ftPos;
        reset_frontier();
    }

    void reset_frontier(std::vector<Pos> const &) noexcept {}

    void reset_useableShapeCounts(std::vector<size_t> const &shps_counts) {
        m_useableCount_perShape = shps_counts;
        m_useableCount_perShape.resize(m_shapes_alterns.size(), 0);
    }

    void reset_pastComputed() noexcept { m_pastComputed.clear(); }

public:
    size_t erase_fromFrontier(std::vector<Pos> const &shapePoss) {
        size_t res_removed = 0;
        for (Pos const &onePos : shapePoss) {
            if (m_frontierTiles_view[onePos.y, onePos.x] != std::nullopt) { res_removed++; }
            m_frontierTiles_view[onePos.y, onePos.x] = std::nullopt;
        }
        return res_removed;
    }


    bool add_toFrontier(Pos const &onePos) {
        bool res    = true;
        auto window = get_windowAtPos(onePos);
        if (! window.has_value() || window.value().count_filledBorderLess() > m_shapesMaxEmpty) { res = false; }
        else {
            auto &possibsForWindow = getOrCompute_possibsFor(window.value());
            if (possibsForWindow.size() > 0) { m_frontierTiles_view[onePos.y, onePos.x] = std::ref(possibsForWindow); }
        }
        return res;
    }


    size_t add_toFrontier(std::vector<Pos> const &shapePoss) {
        size_t resCount = 0;
        for (auto const &onePos : shapePoss) { resCount += add_toFrontier(onePos); }
        return resCount;
    }

    size_t add_toFrontier_allCorners() {
        size_t resCount = 0;
        if (m_areaView.extent(0) >= m_sqsz && m_areaView.extent(1) >= m_sqsz) {
            for (auto const [r, c] : std::array<std::array<size_t, 2>, 4>{
                     {{0, 0},
                      {0, m_areaView.extent(1) - m_sqsz},
                      {m_areaView.extent(0) - m_sqsz, 0},
                      {m_areaView.extent(0) - m_sqsz, m_areaView.extent(1) - m_sqsz}}}) {
                resCount += add_toFrontier(Pos{static_cast<long long>(r), static_cast<long long>(c)});
            }
        }

        return resCount;
    }

private:
    [[nodiscard]] static ConsideredShapeOption make_consideredShapeOption(Pos const &p, PastRes const &pr,
                                                                          ConsideredShapeOption::Type const type) {
        return ConsideredShapeOption{.p = p, .pr_option = pr, .type = type};
    }

    [[nodiscard]] bool has_useableAlternatives(std::vector<PastRes> const &oneShpAltsVec) const {
        if (oneShpAltsVec.empty()) { return false; }
        return m_useableCount_perShape.at(oneShpAltsVec.front().ol_shpID.shpID) > 0;
    }

    template <typename Predicate>
    void collect_consideredOptionsAt(consideredOptionsByShape_t &toConsider, bool &anyFilled,
                                     typename SolverPolicy::SelectionState &selectionState, Pos const &candidatePos,
                                     possibilitiesByShape_t const     &possibilitiesByShape,
                                     std::vector<double> const        &perShpScoringAdj,
                                     ConsideredShapeOption::Type const type, Predicate const &predicate) const {
        for (auto const &v_pr2 : std::views::filter(possibilitiesByShape, [this](auto const &oneShpAltsVec) {
                 return has_useableAlternatives(oneShpAltsVec);
             })) {
            for (PastRes const &pr : v_pr2) {
                if (! predicate(pr)) { continue; }
                double const curAdjSOR = pr.ol_res.surfaceOpened_relative * perShpScoringAdj.at(pr.ol_shpID.shpID);

                if (selectionState.shouldStopOn(curAdjSOR)) { break; }
                if (selectionState.hasNewBest(curAdjSOR)) { selectionState.reset(toConsider, curAdjSOR); }

                toConsider.at(pr.ol_shpID.shpID).push_back(make_consideredShapeOption(candidatePos, pr, type));
                anyFilled = true;
            }
        }
    }

    std::vector<double> compute_perShapeScoringAdjustments() const {
        double const sum = static_cast<double>(std::ranges::fold_left(m_useableCount_perShape, 0uz, std::plus{}));

        auto ratiosHlprView = std::views::zip(m_useableCount_perShape, m_shapesRatios_orig) |
                              std::views::transform([&](auto const &oneCount) {
                                  return (std::get<0>(oneCount) == 0 ? std::numeric_limits<double>::max()
                                                                     : (sum / std::get<0>(oneCount))) *
                                         std::get<1>(oneCount);
                              });

        return std::vector<double>(ratiosHlprView.begin(), ratiosHlprView.end());
    }

    possibilitiesByShape_t &getOrCompute_possibsFor(Shape const &tile) {
        auto insRes = m_pastComputed.insert({tile, possibilitiesByShape_t(m_shapes_alterns.size())});
        if (insRes.second) {
            possibilitiesByShape_t &vpr = insRes.first->second;

            for (size_t shpID = 0; shpID < m_shapes_alterns.size(); ++shpID) {
                for (size_t alternID = 0; alternID < m_shapes_alterns.at(shpID).size(); ++alternID) {
                    auto rs = PastRes{.ol_shpID{shpID, alternID},
                                      .ol_res = tile.compute_overlayWith(m_shapes_alterns.at(shpID).at(alternID))};
                    if (SolverPolicy::allows(rs)) { vpr.at(shpID).push_back(rs); }
                }
            }

            for (auto &vprLine : vpr) { std::ranges::sort(vprLine, SolverPolicy::prefer_precomputed); }
        }
        return insRes.first->second;
    }

    std::optional<std::vector<std::vector<ConsideredShapeOption>>> findNextStep_covering() {
        if (m_uncoverableFrontierPoss.empty()) { return std::nullopt; }

        std::vector<unsigned char> tracker(m_frontierTiles_view.extent(0) * m_frontierTiles_view.extent(1), 0);
        pf_mdspan mdsp(tracker.data(),
                       std::dextents<size_t, 2>{m_frontierTiles_view.extent(0), m_frontierTiles_view.extent(1)});

        auto const perShpScoringAdj = compute_perShapeScoringAdjustments();

        while (! m_uncoverableFrontierPoss.empty()) {
            if (m_areaView[m_uncoverableFrontierPoss.front().y, m_uncoverableFrontierPoss.front().x] != 0) {
                m_uncoverableFrontierPoss.pop_front();
                continue;
            }

            auto explr = explorers::Chebyshev(
                [&](std::array<size_t, 2> const &item) { return m_areaView[item[0], item[1]] == 0; },
                std::array{static_cast<size_t>(m_uncoverableFrontierPoss.front().y),
                           static_cast<size_t>(m_uncoverableFrontierPoss.front().x)},
                std::array{m_frontierTiles_view.extent(0), m_frontierTiles_view.extent(1)});

            auto eva = [&](std::vector<Pos> const &poss) -> std::optional<consideredOptionsByShape_t> {
                consideredOptionsByShape_t            toConsider(m_shapes_alterns.size());
                bool                                  anyFilled = false;
                typename SolverPolicy::SelectionState selectionState{};

                for (auto const &onePos : poss) {
                    for (auto const &prPos : get_surrOverlappingPoss<false>(onePos)) {
                        if (mdsp[prPos.y, prPos.x] != 0) { continue; }
                        mdsp[prPos.y, prPos.x] = 1;

                        if (! m_frontierTiles_view[prPos.y, prPos.x].has_value()) { continue; }

                        collect_consideredOptionsAt(toConsider, anyFilled, selectionState, prPos,
                                                    m_frontierTiles_view[prPos.y, prPos.x].value().get(),
                                                    perShpScoringAdj, ConsideredShapeOption::Type::Gapcreating,
                                                    [](auto const &item) { return item.ol_res.gapsCount > 1; });
                    }
                }

                if (! anyFilled) { return std::nullopt; }
                return toConsider;
            };

            size_t           level = 0;
            std::vector<Pos> posToEval;

            while (! explr.is_atEnd()) {
                auto locPos = explr.get_next();
                posToEval.push_back({static_cast<long long>(locPos[0]), static_cast<long long>(locPos[1])});

                if (level < explr.m_queueIDToUseNext) {
                    if (auto potRes = eva(posToEval); potRes.has_value()) { return potRes; }
                    posToEval.clear();
                }

                level = explr.m_queueIDToUseNext;
            }
            if (auto potRes = eva(posToEval); potRes.has_value()) { return potRes; }

            m_uncoverableFrontierPoss.pop_front();
        }

        return std::nullopt;
    }

    std::optional<std::vector<std::vector<ConsideredShapeOption>>> findNextStep_regular() const {
        consideredOptionsByShape_t            toConsider(m_shapes_alterns.size());
        bool                                  anyFilled = false;
        typename SolverPolicy::SelectionState selectionState{};
        auto const                            perShpScoringAdj = compute_perShapeScoringAdjustments();

        Pos curPos{.y = -1, .x = -1};
        for (curPos.y = 0uz; curPos.y < m_frontierTiles_view.extent(0); ++curPos.y) {
            for (curPos.x = 0uz; curPos.x < m_frontierTiles_view.extent(1); ++curPos.x) {
                auto const &frontierPos = m_frontierTiles_view[curPos.y, curPos.x];
                if (frontierPos != std::nullopt) {
                    collect_consideredOptionsAt(toConsider, anyFilled, selectionState, curPos,
                                                frontierPos.value().get(), perShpScoringAdj,
                                                ConsideredShapeOption::Type::Gapless,
                                                [](auto const &item) { return item.ol_res.gapsCount < 2; });
                }
            }
        }
        if (! anyFilled) { return std::nullopt; }
        return toConsider;
    }

    std::optional<std::vector<std::vector<ConsideredShapeOption>>> findNextStep_withGap() const {
        consideredOptionsByShape_t            toConsider(m_shapes_alterns.size());
        bool                                  anyFilled = false;
        typename SolverPolicy::SelectionState selectionState{};
        auto const                            perShpScoringAdj = compute_perShapeScoringAdjustments();

        Pos curPos{.y = -1, .x = -1};

        for (auto const &frontierLine : m_frontierTiles) {
            curPos.y++;
            curPos.x = -1;
            for (auto const &frontierPos : frontierLine) {
                curPos.x++;
                if (frontierPos != std::nullopt) {
                    collect_consideredOptionsAt(toConsider, anyFilled, selectionState, curPos,
                                                frontierPos.value().get(), perShpScoringAdj,
                                                ConsideredShapeOption::Type::Dividing,
                                                [](auto const &item) { return item.ol_res.gapsCount > 1; });
                }
            }
        }

        if (! anyFilled) { return std::nullopt; }
        return toConsider;
    }

    std::optional<ConsideredShapeOption> select_oneCSO(
        std::vector<std::vector<ConsideredShapeOption>> const &VofV_csos) {
        size_t const optsCount = std::ranges::fold_left(
            VofV_csos, size_t{0}, [](size_t init, auto const &VofCSO) { return init + VofCSO.size(); });
        if (optsCount == 0) { return std::nullopt; }

        size_t numToConsider = m_fprng.pseudoRandom_0_to(optsCount - 1) + 1;

        for (auto const &VofCSO : VofV_csos) {
            if (VofCSO.size() < numToConsider) { numToConsider -= VofCSO.size(); }
            else { return std::optional<ConsideredShapeOption>{VofCSO.at(numToConsider - 1)}; }
        }
        assert(false);
        std::unreachable();
    }

    std::vector<Pos> verify_uncoverable(ConsideredShapeOption const &cso) const {
        std::vector<Pos> res{};
        long long const  halfCount = static_cast<long long>(m_shapeOLCount_border / 2);

        for (long long thisShpRow = cso.p.y; thisShpRow < (cso.p.y + static_cast<long long>(m_sqsz)); ++thisShpRow) {
            for (long long thisShpCol = cso.p.x; thisShpCol < (cso.p.x + static_cast<long long>(m_sqsz));
                 ++thisShpCol) {
                if (cso.pr_option.ol_res.res_matrix[thisShpRow - cso.p.y][thisShpCol - cso.p.x] != 0) { continue; }

                bool onePointCovered      = false;
                bool atLeastOneWithoutGap = false;

                for (long long influRow = thisShpRow - (static_cast<long long>(m_sqsz) - 2); influRow < thisShpRow;
                     ++influRow) {
                    for (long long influCol = thisShpCol - (static_cast<long long>(m_sqsz) - 2); influCol < thisShpCol;
                         ++influCol) {
                        if (! is_posValid(Pos{.y = influRow, .x = influCol})) { continue; }

                        if (! m_frontierTiles.at(influRow).at(influCol).has_value()) { continue; }
                        for (auto const &prLine : m_frontierTiles.at(influRow).at(influCol).value().get()) {
                            for (PastRes const &onePR : prLine) {
                                onePointCovered |=
                                    onePR.ol_res.res_matrix
                                        .at((m_sqsz - 2) -
                                            (influRow - (thisShpRow - (static_cast<long long>(m_sqsz) - 2))))
                                        .at((m_sqsz - 2) -
                                            (influCol - (thisShpCol - (static_cast<long long>(m_sqsz) - 2))));
                                atLeastOneWithoutGap |= (onePR.ol_res.gapsCount < 2);
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
        size_t const rows = m_area.size();
        size_t const cols = m_area.size() > 0 ? m_area.front().size() : 0;

        size_t const adj = (m_sqsz - 2 + static_cast<size_t>(INCLBorder));

        std::vector<Pos> res;

        for (long long row = shp_pos.y - static_cast<long long>(adj);
             row < (shp_pos.y + static_cast<long long>(INCLBorder)); ++row) {
            for (long long col = shp_pos.x - static_cast<long long>(adj);
                 col < (shp_pos.x + static_cast<long long>(INCLBorder)); ++col) {
                if (row < 0 || row > (static_cast<long long>(rows - m_sqsz)) || col < 0 ||
                    col > (static_cast<long long>(cols - m_sqsz))) {}
                else { res.push_back(Pos{.y = row, .x = col}); }
            }
        }

        return res;
    }

    std::vector<Pos> get_surrOverlappingPoss_forWindowsAt(Pos const &shp_pos, size_t olCount) const {
        assert(olCount % 2 == 1);

        size_t const rows = m_area.size();
        size_t const cols = m_area.size() > 0 ? m_area.front().size() : 0;

        long long const halfCount = static_cast<long long>(olCount / 2);

        std::vector<Pos> res;

        for (long long row = shp_pos.y - halfCount; row < (shp_pos.y + halfCount + 1); ++row) {
            for (long long col = shp_pos.x - halfCount; col < (shp_pos.x + halfCount + 1); ++col) {
                if (row < 0 || row > (static_cast<long long>(rows - m_sqsz)) || col < 0 ||
                    col > (static_cast<long long>(cols - m_sqsz))) {}
                else { res.push_back(Pos{.y = row, .x = col}); }
            }
        }

        return res;
    }

    std::optional<Shape> get_windowAtPos(Pos const &shapePos) const {
        size_t const rows = m_area.size();
        size_t const cols = m_area.size() > 0 ? m_area.front().size() : 0;

        if (shapePos.y >= 0 && shapePos.y <= static_cast<long long>(rows - m_sqsz) && shapePos.x >= 0 &&
            shapePos.x <= static_cast<long long>(cols - m_sqsz)) {
            Shape res(m_sqsz);
            for (long long row = shapePos.y; row < shapePos.y + static_cast<long long>(m_sqsz); ++row) {
                for (long long col = shapePos.x; col < (shapePos.x + static_cast<long long>(m_sqsz)); ++col) {
                    res.m_matrix[row - shapePos.y][col - shapePos.x] = m_area[row][col];
                }
            }
            return res;
        }
        return std::nullopt;
    }

    bool is_posValid(Pos const &p) const noexcept {
        long long const py = p.y;
        long long const px = p.x;
        if (py < 0 || py > (static_cast<long long>(m_area.size()) - static_cast<long long>(m_sqsz)) || px < 0 ||
            px > (m_area.size() == 0 ? 0 : static_cast<long long>(m_area.front().size() - m_sqsz))) {
            return false;
        }
        return true;
    }

    bool set_windowAtPos(Pos const &shapePos, PastRes const &pr) {
        if (! is_posValid(shapePos)) { return false; }
        for (long long r = shapePos.y; r < (shapePos.y + static_cast<long long>(m_sqsz)); ++r) {
            for (long long c = shapePos.x; c < (shapePos.x + static_cast<long long>(m_sqsz)); ++c) {
                m_area[r][c] = pr.ol_res.res_matrix[r - shapePos.y][c - shapePos.x];
            }
        }
        return true;
    }

    bool set_windowAtPos(Pos const &shapePos, Shape const &newWindow) {
        if (! is_posValid(shapePos)) { return false; }
        for (long long r = shapePos.y; r < (shapePos.y + static_cast<long long>(m_sqsz)); ++r) {
            for (long long c = shapePos.x; c < (shapePos.x + static_cast<long long>(m_sqsz)); ++c) {
                m_area[r][c] = newWindow.m_matrix[r - shapePos.y][c - shapePos.x];
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

        size_t const m_area_ySz = m_area.size();
        size_t const m_area_xSz = m_area.empty() ? 0 : m_area.front().size();
        XXH3_64bits_update(state, &m_sqsz, sizeof(size_t));
        XXH3_64bits_update(state, &m_area_ySz, sizeof(size_t));
        XXH3_64bits_update(state, &m_area_xSz, sizeof(size_t));
        XXH3_64bits_update(state, &m_firstTilePos.y, sizeof(long long));
        XXH3_64bits_update(state, &m_firstTilePos.x, sizeof(long long));

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

public:
    template <size_t N>
    static std::vector<std::array<std::array<bool, N>, N>> calculate_rotFlipped(
        std::array<std::array<bool, N>, N> input) {
        namespace incmatrix = incom::standard::matrix;

        ankerl::unordered_dense::set<decltype(input), standard::hashing::XXH3Hasher> hlprMP;
        hlprMP.insert(input);
        for (int rot_i = 0; rot_i < 3; ++rot_i) {
            incmatrix::matrixRotateLeft(input);
            hlprMP.insert(input);
        }

        for (size_t i = 0; i < (input.size() / 2); ++i) { std::swap(input.at(i), input.at(input.size() - 1 - i)); }

        hlprMP.insert(input);
        for (int rot_i = 0; rot_i < 3; ++rot_i) {
            incmatrix::matrixRotateLeft(input);
            hlprMP.insert(input);
        }
        return std::vector<decltype(input)>(hlprMP.begin(), hlprMP.end());
    }
};

} // namespace packing

} // namespace incom::standard::solvers_TEMP
