#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <mdspan>
#include <utility>

#include <boxpacker_private/incom_commons.h>
#include <incstd/incstd_all.hpp>
#include <readerwriterqueue.h>

#include <exec/async_scope.hpp>
#include <exec/execute.hpp>
#include <exec/repeat_n.hpp>
#include <exec/repeat_until.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/task.hpp>
#include <stdexec/execution.hpp>


namespace incom::box_packer {
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
    int                        yDim;
    int                        xDim;
    std::vector<std::uint64_t> reqdShapes;

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


inline std::tuple<ShapesStorage, std::vector<Tree>> get_integratedSampleData(std::string &df) {
    std::tuple<ShapesStorage, std::vector<Tree>> res;

    ShapesStorage     &shps  = std::get<0>(res);
    std::vector<Tree> &trees = std::get<1>(res);

    auto any_ctre = ctre::search<R"(.+)">;
    auto d_ctre   = ctre::search<R"(\d+)">;
    auto input    = incom::aoc::parseInputUsingCTRE::processFile(df, any_ctre).front();


    for (size_t lineID = 0; lineID < input.size(); ++lineID) {
        if (input.at(lineID).size() == 2) {
            shps.m_shapes.emplace_back();
            for (size_t shape_line = 0; shape_line < 3; ++shape_line) {
                lineID++;
                for (auto oneChr : input.at(lineID)) { shps.m_shapes.back().m_data.push_back(oneChr == '#' ? 1 : 0); }
            }
        }
        if (input.at(lineID).size() > 5) {
            auto prsRes = incom::aoc::parseInputUsingCTRE::processOneLine(input.at(lineID), d_ctre, d_ctre, d_ctre,
                                                                          d_ctre, d_ctre, d_ctre, d_ctre, d_ctre);
            trees.push_back(
                Tree{.yDim = std::stoi(prsRes.at(0)),
                     .xDim = std::stoi(prsRes.at(1)),
                     .reqdShapes{std::stoull(prsRes.at(2)), std::stoull(prsRes.at(3)), std::stoull(prsRes.at(4)),
                                 std::stoull(prsRes.at(5)), std::stoull(prsRes.at(6)), std::stoull(prsRes.at(7))}});
        }
    }

    return res;
}


namespace incpack = incom::standard::solvers::packing;
using BP_Pos      = incpack::BoxPacker_2D<5>::Pos;
using BP_PastRes  = incpack::BoxPacker_2D<5>::PastRes;

inline constexpr auto bp_asyncExecute =
    [](auto &sch, std::vector<incom::box_packer::Tree> const trees, auto const shpsToUse,
       moodycamel::ReaderWriterQueue<std::tuple<size_t, incom::standard::solvers::packing::BoxPacker_2D<5>::Pos,
                                                incom::standard::solvers::packing::BoxPacker_2D<5>::PastRes>> &q)
    -> exec::basic_task<void, experimental::execution::__task::inline_task_context<void>> {
    co_await stdexec::schedule(sch);
    incom::standard::solvers::packing::BoxPacker_2D<5> solver(trees.front().yDim, trees.front().xDim, shpsToUse,
                                                              trees.front().reqdShapes);

    auto stopTokOpt = co_await stdexec::stopped_as_optional(stdexec::get_stop_token());

    for (size_t treeID = 0uz; auto const &oneTree : trees) {
        solver.reset_allButNotPastComputed(oneTree.yDim, oneTree.xDim, oneTree.reqdShapes);
        solver.add_toFrontier_allCorners();

        while (auto solvRes = solver.solve_oneStep()) {
            if (stopTokOpt && stopTokOpt->stop_requested()) { co_await stdexec::just_stopped(); }

            size_t sleepFor = 0;
            auto   rrr      = std::tuple_cat(std::make_tuple(treeID), solvRes.value());
            while (not q.try_enqueue(std::tuple_cat(std::make_tuple(treeID), solvRes.value()))) {
                std::this_thread::sleep_for(std::chrono::milliseconds(std::min(sleepFor++, 100uz)));
            };
        }

        ++treeID;
    }

    co_return;
};

} // namespace incom::box_packer