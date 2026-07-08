#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <format>
#include <mdspan>
#include <stdexec/__detail/__execution_fwd.hpp>
#include <utility>

#include <boxpacker_private/incom_commons.hpp>
#include <incstd/incstd_all.hpp>
#include <boxpacker_private/solvers.hpp>
#include <readerwriterqueue.h>

#include <exec/async_scope.hpp>
#include <exec/execute.hpp>
#include <exec/repeat_n.hpp>
#include <exec/repeat_until.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/task.hpp>
#include <stdexec/execution.hpp>

#include <imgui.h>


namespace incom::box_packer {

namespace incpack = incom::standard::solvers_TEMP::packing;
using BP_Pos      = incpack::BoxPacker_2D::Pos;
using BP_PastRes  = incpack::BoxPacker_2D::PastRes;

struct Shape {
    std::vector<uint32_t> m_data;

    template <std::size_t... Extents>
    auto get_viewInto() {
        return std::mdspan(m_data.data(), std::extents<uint32_t, Extents...>{});
    };

    template <std::size_t... Extents>
    auto get_viewInto() const {
        return std::mdspan(m_data.data(), std::extents<uint32_t, Extents...>{});
    };

    auto get_viewInto(std::convertible_to<size_t> auto const... ids) {
        return std::mdspan(m_data.data(), std::dextents<uint32_t, sizeof...(ids)>{});
    };

    auto get_viewInto(std::convertible_to<size_t> auto const... ids) const {
        return std::mdspan(m_data.data(), std::dextents<uint32_t, sizeof...(ids)>{});
    };


    template <std::size_t... Extents>
    requires(sizeof...(Extents) > 0)
    auto conv_intoArr_bools() const {
        return [&]<size_t... IDx>(std::index_sequence<IDx...>) {
            return typename c_generateNestedArray<bool, Extents...>::type{(static_cast<bool>(m_data[IDx]))...};
        }(std::make_index_sequence<(Extents * ...)>{});
    };


private:
    template <typename T, size_t First, size_t... IDs>
    struct c_generateNestedArray {
        static_assert(false, "Cannot do this");
    };

    template <typename T, size_t First, size_t... IDs>
    requires(sizeof...(IDs) > 0)
    struct c_generateNestedArray<T, First, IDs...> {
        using type = typename std::array<typename c_generateNestedArray<T, IDs...>::type, First>;
    };

    template <typename T, size_t First, size_t... IDs>
    requires(sizeof...(IDs) == 0)
    struct c_generateNestedArray<T, First, IDs...> {
        using type = typename std::array<T, First>;
    };
};


struct ShapesStorage {
    std::vector<Shape> m_shapes;

    bool swap(size_t cursorA, size_t cursorB) {
        if (m_shapes.size() < cursorA || m_shapes.size() < cursorB) { return false; }
        std::swap(m_shapes[cursorA], m_shapes[cursorB]);
        return true;
    }


    bool removeErase_oneID(size_t const idToRemove) {
        if (idToRemove >= m_shapes.size()) { return false; }
        m_shapes.erase(m_shapes.begin() + static_cast<std::ptrdiff_t>(idToRemove));
        return true;
    }
};

struct Tree {
    int                      yDim;
    int                      xDim;
    std::vector<std::size_t> reqdShapes;

    bool removeErase_oneID(size_t const idToRemove) {
        if (idToRemove >= reqdShapes.size()) { return false; }
        reqdShapes.erase(reqdShapes.begin() + static_cast<std::ptrdiff_t>(idToRemove));
        return true;
    }


    bool set_reqdShape(size_t cursor, int newValue) {
        if (cursor >= reqdShapes.size()) { return false; }
        reqdShapes[cursor] = std::max(0, newValue);
        return true;
    }

    int make_szFit(size_t newSz) {
        if (reqdShapes.size() == newSz) { return 0; }
        else if (reqdShapes.size() < newSz) {
            do { reqdShapes.push_back(0ull); } while (reqdShapes.size() < newSz);
            return -1;
        }
        else {
            do { reqdShapes.pop_back(); } while (reqdShapes.size() < newSz);
            return 1;
        }
    }

    bool operator==(Tree const &) const = default;

    // ADL for hashing using XXH3Hasher
    friend constexpr void XXH3Hash(Tree const &input, XXH3_state_t *state) {
        XXH3_64bits_update(state, &input.yDim, sizeof(int));
        XXH3_64bits_update(state, &input.xDim, sizeof(int));
        XXH3_64bits_update(state, input.reqdShapes.data(), sizeof(std::uint64_t) * input.reqdShapes.size());
    }
};

struct SolveResStore {

    std::vector<Tree>                                                                              m_trees;
    std::vector<std::vector<std::array<std::array<bool, 3>, 3>>> const                             m_shpsAlterns;
    std::vector<std::vector<std::tuple<incom::box_packer::BP_Pos, incom::box_packer::BP_PastRes>>> vecOfRes = {};
    std::vector<size_t>                                                                            endOfVisible;

    std::vector<std::vector<size_t>> m_curPlacedCount;

    std::vector<std::vector<std::uint8_t>>               m_reaAreaMaps;
    std::vector<std::mdspan<std::uint8_t, std::dims<2>>> accs;

    std::vector<ImU32> colorsToUse;

    SolveResStore(std::vector<Tree> const                                            &trees,
                  std::vector<std::vector<std::array<std::array<bool, 3>, 3>>> const &shpsAlterns,
                  std::array<incom::standard::color::inc_sRGB, 256> const            &palette =
                      incom::standard::console::color_schemes::windows_terminal::dimidium256.palette)
        : m_trees(trees), m_shpsAlterns(shpsAlterns), vecOfRes(trees.size()), endOfVisible(trees.size(), 0uz),
          m_curPlacedCount(trees.size(), std::vector<size_t>(m_shpsAlterns.size(), 0)),
          m_reaAreaMaps(std::from_range, std::views::transform(trees,
                                                               [](auto const &item) {
                                                                   return std::vector<std::uint8_t>(
                                                                       (item.yDim + 2) * (item.xDim + 2), 0);
                                                               })),
          accs(std::from_range,
               std::views::transform(std::views::zip(m_reaAreaMaps, m_trees),
                                     [](auto const &onePair) {
                                         return std::mdspan(std::get<0>(onePair).data(),
                                                            std::dims<2>{std::get<1>(onePair).yDim + 2,
                                                                         std::get<1>(onePair).xDim + 2});
                                     })),
          colorsToUse(std::from_range, std::views::transform(palette, [](auto const &oneCol) {
                          return ImU32(ImColor(oneCol.r, oneCol.g, oneCol.b));
                      })) {}


    void update_oneShape(size_t const resID, size_t const vecOfRes_ID) {
        auto const &[itemPos, itemPR] = vecOfRes.at(resID).at(vecOfRes_ID);
        for (size_t r = itemPos.y + 1; r < itemPos.y + 1 + 3; ++r) {
            for (size_t c = itemPos.x + 1; c < itemPos.x + 1 + 3; ++c) {
                if (m_shpsAlterns.at(itemPR.ol_shpID.shpID)
                        .at(itemPR.ol_shpID.alternID)
                        .at(r - (itemPos.y + 1))
                        .at(c - (itemPos.x + 1))) {
                    accs.at(resID)[r, c] = itemPR.ol_shpID.shpID + 1;
                }
            }
        }
        m_curPlacedCount.at(resID).at(itemPR.ol_shpID.shpID)++;
    }

    void remove_oneShape(size_t const resID, size_t const vecOfRes_ID) {
        auto const &[itemPos, itemPR] = vecOfRes.at(resID).at(vecOfRes_ID);
        for (size_t r = itemPos.y + 1; r < itemPos.y + 1 + 3; ++r) {
            for (size_t c = itemPos.x + 1; c < itemPos.x + 1 + 3; ++c) {
                if (m_shpsAlterns.at(itemPR.ol_shpID.shpID)
                        .at(itemPR.ol_shpID.alternID)
                        .at(r - (itemPos.y + 1))
                        .at(c - (itemPos.x + 1))) {
                    accs.at(resID)[r, c] = 0;
                }
            }
        }
        m_curPlacedCount.at(resID).at(itemPR.ol_shpID.shpID)--;
    }

    bool moveInTime_area(size_t const resID, int const moveInTimeBy) {
        if ((endOfVisible.at(resID) + moveInTimeBy) < 0 or
            (endOfVisible.at(resID) + moveInTimeBy) > vecOfRes.at(resID).size()) {
            return false;
        }

        int const end = (endOfVisible.at(resID) + moveInTimeBy);
        if (moveInTimeBy < 0) {
            for (int i = endOfVisible.at(resID) - 1; i >= end; --i) { remove_oneShape(resID, i); }
        }
        else {
            for (int i = endOfVisible.at(resID); i < end; ++i) { update_oneShape(resID, i); }
        }

        return true;
    }
};


inline std::tuple<std::string, ShapesStorage, std::vector<Tree>> get_integratedSampleData(std::string &df) {
    static int counter = 0;

    std::tuple<std::string, ShapesStorage, std::vector<Tree>> res;

    std::get<0>(res)         = std::format("Integrated: {}", counter++);
    ShapesStorage     &shps  = std::get<1>(res);
    std::vector<Tree> &trees = std::get<2>(res);

    auto any_ctre    = ctre::search<R"(.+)">;
    auto d_ctre      = ctre::search<R"(\d+)">;
    auto shapeHeader = ctre::search<R"(^\d+:)">;
    auto treeHeader  = ctre::search<R"(^\d+x\d+)">;
    auto input       = incom::aoc::parseInputUsingCTRE::processFile(df, any_ctre).front();

    std::optional<size_t> lastShapeLine = std::nullopt;
    size_t                shapeCount    = 0uz;


    for (size_t lineID = 0; lineID < input.size(); ++lineID) {
        if (shapeHeader(input.at(lineID).begin(), input.at(lineID).end())) {
            shapeCount++;
            lastShapeLine = 0;
            shps.m_shapes.emplace_back();
        }
        else if (treeHeader(input.at(lineID).begin(), input.at(lineID).end())) {
            auto prsRes = incom::aoc::parseInputUsingCTRE::processOneLineRPT(input.at(lineID), d_ctre).front();
            trees.push_back(Tree{.yDim       = std::stoi(prsRes.at(0)),
                                 .xDim       = std::stoi(prsRes.at(1)),
                                 .reqdShapes = (std::views::iota(2uz) | std::views::take(shapeCount) |
                                                std::views::transform([&](auto inputID) {
                                                    return static_cast<std::size_t>(std::stoull(prsRes.at(inputID)));
                                                }) |
                                                std::ranges::to<std::vector>())});
        }
        else if (lastShapeLine) {
            for (auto oneChr : input.at(lineID)) {
                if (oneChr == '#') { shps.m_shapes.back().m_data.push_back(1); }
                else if (oneChr == '.') { shps.m_shapes.back().m_data.push_back(0); }
            }
        }
        else { lastShapeLine = std::nullopt; }
    }

    return res;
}
inline std::tuple<std::string, ShapesStorage, std::vector<Tree>> get_externalSampleData(std::string &data_asString) {
    std::tuple<std::string, ShapesStorage, std::vector<Tree>> res;

    // std::string       &name  = std::get<0>(res);
    ShapesStorage     &shps  = std::get<1>(res);
    std::vector<Tree> &trees = std::get<2>(res);

    auto untilNewLine = ctre::search<R"([^\n]+)">;
    auto any_ctre     = ctre::search<R"(.+)">;
    auto d_ctre       = ctre::search<R"(\d+)">;
    auto shapeHeader  = ctre::search<R"(^\d+:)">;
    auto treeHeader   = ctre::search<R"(^\d+x\d+)">;
    auto input        = incom::aoc::parseInputUsingCTRE::processOneLineRPT(data_asString, untilNewLine).front();


    std::optional<size_t> lastShapeLine = std::nullopt;
    size_t                shapeCount    = 0uz;

    for (size_t lineID = 0; lineID < input.size(); ++lineID) {
        if (shapeHeader(input.at(lineID).begin(), input.at(lineID).end())) {
            shapeCount++;
            lastShapeLine = 0;
            shps.m_shapes.emplace_back();
        }
        else if (treeHeader(input.at(lineID).begin(), input.at(lineID).end())) {
            auto prsRes = incom::aoc::parseInputUsingCTRE::processOneLineRPT(input.at(lineID), d_ctre).front();
            trees.push_back(Tree{.yDim       = std::stoi(prsRes.at(0)),
                                 .xDim       = std::stoi(prsRes.at(1)),
                                 .reqdShapes = (std::views::iota(2uz) | std::views::take(shapeCount) |
                                                std::views::transform([&](auto inputID) {
                                                    return static_cast<std::size_t>(std::stoull(prsRes.at(inputID)));
                                                }) |
                                                std::ranges::to<std::vector>())});
        }
        else if (lastShapeLine) {
            for (auto oneChr : input.at(lineID)) {
                if (oneChr == '#') { shps.m_shapes.back().m_data.push_back(1); }
                else if (oneChr == '.') { shps.m_shapes.back().m_data.push_back(0); }
            }
        }
        else { lastShapeLine = std::nullopt; }
    }

    // for (size_t lineID = 0; lineID < input.size(); ++lineID) {
    //     if (shapeHeader(input.at(lineID).begin(), input.at(lineID).end())) {
    //         // if (input.at(lineID).size() == 3) {
    //         shps.m_shapes.emplace_back();
    //         for (size_t shape_line = 0; shape_line < 3; ++shape_line) {
    //             lineID++;
    //             for (auto oneChr : std::views::take(input.at(lineID), 3)) {
    //                 shps.m_shapes.back().m_data.push_back(oneChr == '#' ? 1 : 0);
    //             }
    //         }
    //     }
    //     if (input.at(lineID).size() > 5) {
    //         auto prsRes = incom::aoc::parseInputUsingCTRE::processOneLine(input.at(lineID), d_ctre, d_ctre,
    //         d_ctre,
    //                                                                       d_ctre, d_ctre, d_ctre, d_ctre,
    //                                                                       d_ctre);
    //         trees.push_back(
    //             Tree{.yDim = std::stoi(prsRes.at(0)),
    //                  .xDim = std::stoi(prsRes.at(1)),
    //                  .reqdShapes{std::stoull(prsRes.at(2)), std::stoull(prsRes.at(3)), std::stoull(prsRes.at(4)),
    //                              std::stoull(prsRes.at(5)), std::stoull(prsRes.at(6)),
    //                              std::stoull(prsRes.at(7))}});
    //     }
    // }

    return res;
}


inline constexpr auto bp_asyncExecute =
    [](auto &sch, std::vector<incom::box_packer::Tree> const trees, auto const shpsToUse,
       moodycamel::ReaderWriterQueue<std::tuple<size_t, incpack::BoxPacker_2D::Pos,
                                                incpack::BoxPacker_2D::PastRes>> &q)
    -> exec::basic_task<void, experimental::execution::__task::inline_task_context<void>> {
    co_await stdexec::schedule(sch);
    incpack::BoxPacker_2D solver(5, trees.front().yDim, trees.front().xDim, shpsToUse,
                                 trees.front().reqdShapes);

    auto stopTokOpt = co_await stdexec::stopped_as_optional(stdexec::get_stop_token());

    for (size_t treeID = 0uz; auto const &oneTree : trees) {
        solver.reset_allButNotPastComputed(oneTree.yDim, oneTree.xDim, oneTree.reqdShapes);
        solver.add_toFrontier_allCorners();

        while (auto solvRes = solver.solve_oneStep()) {
            size_t sleepFor = 0;
            while (not q.try_enqueue(std::tuple_cat(std::make_tuple(treeID), solvRes.value()))) {
                std::this_thread::sleep_for(std::chrono::milliseconds(std::min(sleepFor++, 100uz)));
                if (sleepFor > 1000) { co_await stdexec::just_error(99); }
            }
            if (stopTokOpt && stopTokOpt->stop_requested()) { co_await stdexec::just_stopped(); }
        }

        ++treeID;
    }

    co_return;
};

} // namespace incom::box_packer