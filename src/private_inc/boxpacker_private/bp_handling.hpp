#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <incstd/incstd_all.hpp>
#include <mdspan>


#include <boxpacker_private/incom_commons.h>


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

    bool operator==(Tree const&) const = default;

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

} // namespace incom::box_packer