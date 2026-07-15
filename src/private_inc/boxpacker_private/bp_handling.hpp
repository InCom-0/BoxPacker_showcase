#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexec/__detail/__execution_fwd.hpp>
#include <utility>

#include <boxpacker_private/incom_commons.hpp>
#include <boxpacker_private/solvers.hpp>
#include <incstd/incstd_all.hpp>
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


struct ShapesStorage {
    std::vector<incpack::BoxPacker_2D::Shape> m_shapes;

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
    std::size_t                                                                                    m_sqsz;
    std::vector<std::vector<incpack::BoxPacker_2D::Shape>> const                                   m_shpsAlterns;
    std::vector<std::vector<std::tuple<incom::box_packer::BP_Pos, incom::box_packer::BP_PastRes>>> vecOfRes = {};
    std::vector<size_t>                                                                            endOfVisible;

    std::vector<std::vector<size_t>> m_curPlacedCount;

    std::vector<std::vector<std::uint8_t>> m_reaAreaMaps;

    std::vector<ImU32> colorsToUse;

    SolveResStore(std::vector<Tree> const                                      &trees,
                  std::vector<std::vector<incpack::BoxPacker_2D::Shape>> const &shpsAlterns,
                  std::array<incom::standard::color::inc_sRGB, 256> const      &palette =
                      incom::standard::console::color_schemes::windows_terminal::dimidium256.palette)
        : m_trees(trees), m_sqsz(shpsAlterns.front().front().m_sqsz), m_shpsAlterns(shpsAlterns),
          vecOfRes(trees.size()), endOfVisible(trees.size(), 0uz),
          m_curPlacedCount(trees.size(), std::vector<size_t>(m_shpsAlterns.size(), 0)),
          m_reaAreaMaps(std::from_range, std::views::transform(trees,
                                                               [](auto const &item) {
                                                                   return std::vector<std::uint8_t>(
                                                                       (item.yDim + 2) * (item.xDim + 2), 0);
                                                               })),
          colorsToUse(std::from_range, std::views::transform(palette, [](auto const &oneCol) {
                          return ImU32(ImColor(oneCol.r, oneCol.g, oneCol.b));
                      })) {
        // auto rrr = std::ranges::fold_left(std::views::transform(std::views::take(shpsAlterns, 1),
        //                                                         [](auto const &shpAlterns) {
        //                                                             return std::ranges::fold_left(
        //                                                                 std::views::take(shpAlterns, 1), 0uz,
        //                                                                 [](size_t &&init, auto const &oneShp) {
        //                                                                     return std::max(init, oneShp.m_sqsz);
        //                                                                 });
        //                                                         }),
        //                                   0uz, std::plus{});
    }


    auto get_mdspan_areaTree(size_t const id) {
        return incpack::BoxPacker_2D::pf_mdspan<std::uint8_t, incpack::BoxPacker_2D::pf_dextents<size_t, 2>>(
            m_reaAreaMaps.at(id).data(), m_trees.at(id).yDim + 2, m_trees.at(id).xDim + 2);
    }
    auto get_mdspan_areaTree(size_t const id) const {
        return incpack::BoxPacker_2D::pf_mdspan<const std::uint8_t, incpack::BoxPacker_2D::pf_dextents<size_t, 2>>(
            m_reaAreaMaps.at(id).data(), m_trees.at(id).yDim + 2, m_trees.at(id).xDim + 2);
    }

    auto get_mdspans_areasTrees() {
        return std::vector(
            std::from_range, std::views::transform(std::views::zip(m_reaAreaMaps, m_trees), [](auto const &onePair) {
                return incpack::BoxPacker_2D::pf_mdspan<std::uint8_t, incpack::BoxPacker_2D::pf_dextents<size_t, 2>>(
                    std::get<0>(onePair).data(), std::get<1>(onePair).yDim + 2, std::get<1>(onePair).xDim + 2);
            }));
    }

    auto get_mdspans_areasTrees() const {
        return std::vector(
            std::from_range, std::views::transform(std::views::zip(m_reaAreaMaps, m_trees), [](auto const &onePair) {
                return incpack::BoxPacker_2D::pf_mdspan<const std::uint8_t,
                                                        incpack::BoxPacker_2D::pf_dextents<size_t, 2>>(
                    std::get<0>(onePair).data(), std::get<1>(onePair).yDim + 2, std::get<1>(onePair).xDim + 2);
            }));
    }


    void update_oneShape(size_t const resID, size_t const vecOfRes_ID) {

        auto areaView                 = get_mdspan_areaTree(resID);
        auto const &[itemPos, itemPR] = vecOfRes.at(resID).at(vecOfRes_ID);
        auto const shapeView = m_shpsAlterns.at(itemPR.ol_shpID.shpID).at(itemPR.ol_shpID.alternID).get_mdspanOfSelf();
        auto const sqsz_loc  = m_shpsAlterns.at(itemPR.ol_shpID.shpID).at(itemPR.ol_shpID.alternID).m_sqsz;

        for (size_t r = itemPos.y + 1; r < itemPos.y + (sqsz_loc - 1); ++r) {
            for (size_t c = itemPos.x + 1; c < itemPos.x + (sqsz_loc - 1); ++c) {
                if (shapeView[r - (itemPos.y + 1) + 1, c - (itemPos.x + 1) + 1]) {
                    areaView[r, c] = itemPR.ol_shpID.shpID + 1;
                }
            }
        }
        m_curPlacedCount.at(resID).at(itemPR.ol_shpID.shpID)++;
    }

    void remove_oneShape(size_t const resID, size_t const vecOfRes_ID) {

        auto areaView                 = get_mdspan_areaTree(resID);
        auto const &[itemPos, itemPR] = vecOfRes.at(resID).at(vecOfRes_ID);
        auto const shapeView = m_shpsAlterns.at(itemPR.ol_shpID.shpID).at(itemPR.ol_shpID.alternID).get_mdspanOfSelf();
        auto const sqsz_loc  = m_shpsAlterns.at(itemPR.ol_shpID.shpID).at(itemPR.ol_shpID.alternID).m_sqsz;

        for (size_t r = itemPos.y + 1; r < itemPos.y + (sqsz_loc - 1); ++r) {
            for (size_t c = itemPos.x + 1; c < itemPos.x + (sqsz_loc - 1); ++c) {
                if (shapeView[r - (itemPos.y + 1) + 1, c - (itemPos.x + 1) + 1]) { areaView[r, c] = 0; }
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


inline std::tuple<std::string, ShapesStorage, std::vector<Tree>> parse_inputData(std::vector<std::string> &input) {

    static int counter = 0;

    std::tuple<std::string, ShapesStorage, std::vector<Tree>> res;

    std::get<0>(res)         = std::format("Integrated: {}", counter++);
    ShapesStorage     &shps  = std::get<1>(res);
    std::vector<Tree> &trees = std::get<2>(res);

    auto d_ctre      = ctre::search<R"(\d+)">;
    auto shapeHeader = ctre::search<R"(^\d+:)">;
    auto treeHeader  = ctre::search<R"(^\d+x\d+)">;

    std::optional<size_t> lastShapeLine = std::nullopt;
    size_t                shapeCount    = 0uz;

    std::vector<std::vector<std::vector<unsigned char>>> tempShapes;

    for (size_t lineID = 0; lineID < input.size(); ++lineID) {
        if (shapeHeader(input.at(lineID).begin(), input.at(lineID).end())) {
            shapeCount++;
            lastShapeLine = 0;
            tempShapes.emplace_back();
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
            tempShapes.back().emplace_back();
            for (auto oneChr : input.at(lineID)) {
                if (oneChr == '#') { tempShapes.back().back().push_back(1); }
                else if (oneChr == '.') { tempShapes.back().back().push_back(0); }
            }
        }
        else { lastShapeLine = std::nullopt; }
    }


    size_t shapesMaxRows = std::ranges::fold_left(
        tempShapes, 0uz, [](size_t init, auto const &rawShp) { return std::max(init, rawShp.size()); });
    size_t       shapesMaxCols = std::ranges::fold_left(tempShapes, 0uz, [](size_t init, auto const &rawShp) {
        return std::max(init, std::ranges::fold_left(rawShp, 0uz, [](size_t init, auto const &oneShpRow) {
                            return std::max(init, oneShpRow.size());
                        }));
    });
    size_t const desiredSqsz   = std::max(shapesMaxRows, shapesMaxCols);

    for (auto &shp : tempShapes) {
        for (auto &shpRow : shp) {
            while (shpRow.size() < desiredSqsz) { shpRow.push_back(0); }
        }
    }
    for (auto &shp : tempShapes) {
        while (shp.size() < desiredSqsz) { shp.emplace_back(desiredSqsz, 0); }
        auto oneShp_matrix = std::ranges::fold_left(shp, std::vector<unsigned char>(desiredSqsz + 2, 0),
                                                    [](std::vector<unsigned char> &&init, auto const &shpLine) {
                                                        init.push_back(0);
                                                        init.append_range(shpLine);
                                                        init.push_back(0);
                                                        return init;
                                                    });
        oneShp_matrix.append_range(std::vector<unsigned char>(desiredSqsz + 2, 0));
        shps.m_shapes.push_back({.m_sqsz = desiredSqsz + 2, .m_matrix = std::move(oneShp_matrix)});
    }


    return res;
}

inline std::tuple<std::string, ShapesStorage, std::vector<Tree>> parse_integratedData(std::string &df) {
    auto any_ctre = ctre::search<R"(.+)">;
    return parse_inputData(incom::aoc::parseInputUsingCTRE::processFile(df, any_ctre).front());
}

inline std::tuple<std::string, ShapesStorage, std::vector<Tree>> parse_externalData(std::string &data_asString) {
    auto untilNewLine = ctre::search<R"([^\r\n]+)">;
    return parse_inputData(incom::aoc::parseInputUsingCTRE::processOneLineRPT(data_asString, untilNewLine).front());
}


inline constexpr auto bp_asyncExecute =
    [](auto &sch, std::vector<incom::box_packer::Tree> const trees, auto const shpsToUse,
       moodycamel::ReaderWriterQueue<std::tuple<size_t, incpack::BoxPacker_2D::Pos, incpack::BoxPacker_2D::PastRes>> &q)
    -> exec::basic_task<void, experimental::execution::__task::inline_task_context<void>> {
    co_await stdexec::schedule(sch);
    incpack::BoxPacker_2D solver(shpsToUse.front().front().m_sqsz, trees.front().yDim, trees.front().xDim, shpsToUse,
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