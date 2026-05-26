#pragma once

#include <cassert>
#include <concepts>
#include <fstream>
#include <functional>
#include <source_location>
#include <vector>

#include <ctre.hpp>

#include <incstd/incstd_all.hpp>

namespace incom {
namespace aoc {

class parseInputUsingCTRE {
private:
    struct oneLineProcessedHolder {
        std::vector<std::string> insideVofS;
        size_t                   cursor              = 0;
        int                      somethingNotFoundAt = -1;

        oneLineProcessedHolder &
        operator<<(auto &&toInsert) {
            if (toInsert) { insideVofS.push_back(toInsert.to_string()); }
            else { somethingNotFoundAt = cursor; }
            cursor++;
            return *this;
        }
    };

    /*
    Questionable necessity to use a template for fileProcessedHolder.
    Only did it to learn how to deal with parameter pack 'sizeof...' and constexpr context.

    Thought came to mind ... would it be a good idea to implement this using some sort of flux sequence?
    That might be nicer && might avoid using that crude 'cursor' and actually learn to use a well made cursors :-).
    ... we shall see some other day ...
    */
    template <size_t sz>
    struct fileProcessedHolder {
        std::vector<std::vector<std::string>> data =
            std::vector<std::vector<std::string>>(sz, std::vector<std::string>());
        size_t cursor              = 0;
        int    somethingNotFoundAt = -1;

        fileProcessedHolder &
        operator<<(auto &&toInsert) {
            if (toInsert) { data[cursor++].push_back(toInsert.to_string()); }
            else { somethingNotFoundAt = cursor++; }
            return *this;
        }

        void
        resetCursor() {
            cursor = 0;
        }
    };

    struct fileProcessedHolder_2 {
        std::vector<std::vector<std::string>> data                = std::vector<std::vector<std::string>>();
        int                                   somethingNotFoundAt = -1;

        fileProcessedHolder_2 &
        operator<<(auto &&toInsert) {
            if (toInsert) { data.back().push_back(toInsert.to_string()); }
            else { somethingNotFoundAt = data.size(); }
            return *this;
        }
    };

    static auto
    findNextWithinLine(auto &ctreSrchObj, std::string::iterator &begin, const std::string::iterator &end) {
        auto result = ctreSrchObj(begin, end);
        if (result) { begin = result.get_end_position(); }
        return result;
    }

public:
    /*
    It might be a good idea to implement some sort of concept restrictions on typename ctreSrch.
    Might get back to it at some point ... but for my usage this is not really necessary ... will learn Concepts some
    other day

    Also ... should probably learn how to use exceptions the right way :-)
    */

    template <typename... ctreSrch>
    static std::vector<std::string>
    processOneLine(std::string &line, ctreSrch &&...perItemInLine) {
        auto                   bg  = line.begin();
        auto                   end = line.end();
        oneLineProcessedHolder sink;
        (sink << ... << findNextWithinLine(perItemInLine, bg, end));
        return std::move(sink.insideVofS);
    }

    template <typename... ctreSrch>
    static std::vector<std::string>
    processOneLineRPToneVect(std::string &line, ctreSrch &&...perItemInLine) {
        oneLineProcessedHolder sink;

        auto bg  = line.begin();
        auto end = line.end();
        do {
            for (int i = 0; i < sizeof...(perItemInLine); ++i) {
                (sink << ... << findNextWithinLine(perItemInLine, bg, end));
            }
        } while (sink.somethingNotFoundAt == -1);
        return std::move(sink.insideVofS);
    }

    template <typename... ctreSrch>
    static std::vector<std::vector<std::string>>
    processOneLineRPT(std::string &line, ctreSrch &&...perItemInLine) {
        constexpr size_t                              searchForNumOfItems = sizeof...(perItemInLine);
        fileProcessedHolder<sizeof...(perItemInLine)> sink;

        auto bg  = line.begin();
        auto end = line.end();

        while (true) {
            for (int i = 0; i < searchForNumOfItems; ++i) {
                (sink << ... << findNextWithinLine(perItemInLine, bg, end));
                sink.resetCursor();
            }
            if (sink.somethingNotFoundAt != -1) { break; }
        }
        return sink.data;
    }

    template <typename... ctreSrch>
    static std::vector<std::vector<std::string>>
    processOneLineRPTinFile(std::string &dataFile, ctreSrch &&...perItemInLine) {
        std::ifstream iStream;
        iStream.clear();
        iStream.open(dataFile);
        if (not iStream.is_open()) {
            return std::vector<std::vector<std::string>>(1, std::vector<std::string>(1, "STREAM NOT OPEN"));
        }

        std::string oneStr;
        std::getline(iStream, oneStr);

        return processOneLineRPT(oneStr, perItemInLine...);
    }

    template <typename... ctreSrch>
    static std::vector<std::vector<std::string>>
    processFile(std::string &dataFile, ctreSrch &&...perItemInLine) {
        std::ifstream iStream;
        iStream.clear();
        iStream.open(dataFile);
        if (not iStream.is_open()) {
            return std::vector<std::vector<std::string>>(1, std::vector<std::string>(1, "STREAM NOT OPEN"));
        }

        fileProcessedHolder<sizeof...(perItemInLine)> sink;
        std::string                                   oneStr;

        while (std::getline(iStream, oneStr)) {
            auto bg  = oneStr.begin();
            auto end = oneStr.end();

            (sink << ... << findNextWithinLine(perItemInLine, bg, end));
            sink.resetCursor();
        }
        return sink.data;
    }
    template <typename... ctreSrch>
    static std::vector<std::vector<std::string>>
    processFileRPT(std::string &dataFile, ctreSrch &&...perItemInLine) {
        std::ifstream iStream;
        iStream.clear();
        iStream.open(dataFile);
        if (not iStream.is_open()) {
            return std::vector<std::vector<std::string>>(1, std::vector<std::string>(1, "STREAM NOT OPEN"));
        }

        fileProcessedHolder_2 sink;
        std::string           oneStr;
        constexpr size_t      searchForNumOfItems = sizeof...(perItemInLine);

        while (std::getline(iStream, oneStr)) {
            sink.data.push_back(std::vector<std::string>());
            auto bg  = oneStr.begin();
            auto end = oneStr.end();
            while (sink.somethingNotFoundAt == -1) { (sink << ... << findNextWithinLine(perItemInLine, bg, end)); }
            sink.somethingNotFoundAt = -1;
        }
        return std::move(sink.data);
    }
};

// ENTIRELY POSSIBLE THAT THIS HACKY NAMESPACE IS NOT REALLY PORTABLE ... BEWARE.
namespace PQA {
/*
Quasi compile time reflection for typenames
*/
template <typename T>
consteval auto
TypeToString() {
    auto EmbeddingSignature = std::string_view{std::source_location::current().function_name()};
    auto firstPos           = EmbeddingSignature.rfind("::") + 2;
    return EmbeddingSignature.substr(firstPos, EmbeddingSignature.size() - firstPos - 1);
}
struct _instrBase {
    std::reference_wrapper<long long> source;
    std::reference_wrapper<long long> target;
};
struct _instrBase_2018 {
    std::reference_wrapper<long long> A;
    std::reference_wrapper<long long> B;
    std::reference_wrapper<long long> C;

    _instrBase_2018(long long &aRef, long long &bRef, long long &cRef)
        : A{aRef}, B{bRef}, C{cRef} {};

    std::reference_wrapper<std::reference_wrapper<long long>>
    getMappedRef(const int &id) {
        if (id == 1) { return std::reference_wrapper<std::reference_wrapper<long long>>(A); }
        else if (id == 2) { return std::reference_wrapper<std::reference_wrapper<long long>>(B); }
        else if (id == 3) { return std::reference_wrapper<std::reference_wrapper<long long>>(C); }
        else { assert((void("ERROR"), false)); };
        std::unreachable();
    }
    virtual const std::vector<int>
    getRS() = 0;
};
struct _instrBase_INT {
    std::vector<std::reference_wrapper<long long>> m_refs;
    virtual constexpr long long
    get_numOfParams() = 0;
};

template <typename... instrT>
requires(std::derived_from<instrT, _instrBase> && ...)
struct ProgramQuasiAssembly {
    std::unordered_map<char, std::reference_wrapper<long long>, incstd::hashing::XXH3Hasher> mapping;
    unsigned long long                                                                       instructionID = 0;
    long long                                                                                fakeRegister  = LLONG_MIN;
    std::vector<long long>                                                                   registers;
    std::vector<std::variant<instrT...>>                                                     instrVect;

    // The one and only constructor of the 'prog' type
    ProgramQuasiAssembly(const std::vector<std::vector<std::string>> &input, const long long registersStartValue = 0) {
        assert((void("Prog type instantiated with an empty input"), input.size() > 0));
        for (auto &line : input) {
            assert((void("Prog type instantiated with an input that has more than 3 items on some instruction line"),
                    line.size() < 4));
            assert(
                (void("Prog type instantiated with an input that has some instruction line empty"), line.size() > 0));
        }

        // Mapping a type 'by string' to the same type inside a std::variant instance;
        // TypeToString uses a very crude form of 'reflection'.
        // ENTIRELY POSSIBLE THAT THIS HACK IS NOT REALLY PORTABLE ... BEWARE.
        std::unordered_map<std::string, std::variant<instrT...>, incstd::hashing::XXH3Hasher> instrTypeMap;
        (instrTypeMap.emplace(TypeToString<instrT>(), std::variant<instrT...>{instrT{fakeRegister, fakeRegister}}),
         ...);

        registers.reserve(input.size() * 2); // Must NEVER reallocate.

        for (auto &line : input) {
            assert((void("Some instructions name in input doesn't match any type in template parameter pack"),
                    instrTypeMap.contains(line.front())));

            for (long long i = 1; i < line.size(); ++i) {
                if (line[i].front() >= 'a' && line[i].front() <= 'z') {
                    // 'Named' registers are inserted into a vector named registers + UOmap is created to
                    // obtain the right reference later
                    if (not mapping.contains(line[i].front())) {
                        registers.push_back(registersStartValue);
                        mapping.emplace(line[i].front(), registers.back());
                    }
                }
            }
        }
        for (auto &line : input) {
            std::reference_wrapper<long long> firstR  = fakeRegister;
            std::reference_wrapper<long long> secondR = fakeRegister;
            if (line.size() >= 2) {
                if (line[1].front() >= 'a' && line[1].front() <= 'z') { firstR = mapping.at(line[1][0]); }
                else {
                    // Integer inputs in the instructions are treated 'as-if' it were another but 'unnamed' register
                    // used only in that instruction
                    registers.push_back(std::stoi(line[1]));
                    firstR = registers.back();
                }
            }
            if (line.size() >= 3) {
                if (line[2].front() >= 'a' && line[2].front() <= 'z') { secondR = mapping.at(line[2][0]); }
                else {
                    // Integer inputs in the instructions are treated 'as-if' it were another but 'unnamed' register
                    // used only in that instruction
                    registers.push_back(std::stoi(line[2]));
                    secondR = registers.back();
                }
            }

            // Update just the right variant item in the instrTypeMap.
            std::visit(
                [&](auto &&a) {
                    a.source = firstR;
                    a.target = secondR;
                },
                instrTypeMap.at(line.front()));

            // Push the right variant inside instrVect
            instrVect.push_back(std::variant<instrT...>(instrTypeMap.at(line.front())));
        }
        return;
    }

    // Other convenience member functions
    inline bool
    test_isInstructionIDvalid() {
        return instructionID < instrVect.size();
    }
    inline std::variant<instrT...> &
    getCurrentAndIncrement() {
        return instrVect[instructionID++];
    }
};

template <typename... instrT>
requires(std::derived_from<instrT, _instrBase_2018> && ...)
struct ProgramQuasiAssembly_2018 {
    // std::unordered_map<char, std::reference_wrapper<long long>, incstd::hashing::XXH3Hasher> mapping;
    unsigned long long                   instructionID = 0;
    std::vector<long long>               registers;
    std::vector<std::variant<instrT...>> instrVect;

    // Other convenience member functions
    inline bool
    test_isInstructionIDvalid() {
        return instructionID < instrVect.size();
    }
    inline std::variant<instrT...> &
    getCurrentAndIncrement() {
        return instrVect[instructionID++];
    }

    // In order to dynamically use the 'right' type for each instruction, one has to generate a map that maps the string
    // name representation (as found in the instructions) to the instantiation of std::variant<instrT...> with the
    // correct type
    static std::unordered_map<std::string, std::variant<instrT...>, incstd::hashing::XXH3Hasher>
    instrTypeMapCreator(const std::vector<std::vector<std::string>> &rawExampleInput, auto &overloadSet,
                        const int registersCount = 4, const std::vector<long long> regStartVal = {0, 0, 0, 0}) {

        std::vector<long long> registers_local(registersCount, 0); // Registers
        std::vector<long long> fakeRegisters(4, LLONG_MIN);        // Fake registers
        std::vector<long long> values(4, LLONG_MIN);               // Values
        long long              fakeLong = LLONG_MIN;

        // Parses 3 lines from the example (that is one sample: 1) start regState, 2) OpCode, 3) end regState) into
        // simple VofV of 'long long'. Skips the 4th line which is always empty in input data.
        std::vector<std::vector<long long>> parsed(4, std::vector<long long>());
        auto                                parseOneSet = [&, line = 0]() mutable -> bool {
            for (int i = 0; i < 4; ++i) { parsed[i].clear(); }
            for (int i = 0; i < 3; ++i) {
                if (line >= rawExampleInput.size()) {
                    assert((void("Exit of parsing one example set when the 'parsed' vector isn't empty"),
                            parsed[0].empty() && parsed[1].empty() && parsed[2].empty() && parsed[3].empty()));
                    return false;
                }

                for (int k = 0; k < 4; ++k) { parsed[i].push_back(std::stoll(rawExampleInput[line][k])); }
                line++;
            }
            line++;
            return true;
        };

        // Matcher attempts to execute a sample as if it were each OpCode.
        // Outputs a vector of valid instruction IDs (the IDs match the position in the parameter pack).
        auto matcher = [&]() -> std::vector<int> {
            std::vector<std::variant<instrT...>> variantVector;
            (variantVector.push_back(instrT{fakeLong, fakeLong, fakeLong}), ...);

            for (auto &var : variantVector) {
                auto regValInstructions = std::visit([](auto &&a) { return a.getRS(); }, var);
                for (int k = 1; auto &regOrVal : regValInstructions) {
                    auto refToRef = std::visit([&](auto &&a) { return a.getMappedRef(k); }, var);

                    if (regOrVal == 1) { refToRef.get() = values[k]; }
                    else if (parsed[1][k] >= registersCount) { refToRef.get() = fakeRegisters[k]; }
                    else { refToRef.get() = registers_local[parsed[1][k]]; }
                    k++;
                }
            }
            values[1] = parsed[1][1];
            values[2] = parsed[1][2];
            values[3] = parsed[1][3];

            std::vector<int> res;
            for (int j = 0; j < sizeof...(instrT); ++j) {
                for (int i = 0; i < registersCount; ++i) { registers_local[i] = parsed[0][i]; }
                std::visit(overloadSet, variantVector[j]);

                int innerCounter = 0;
                for (int k = 0; k < registersCount; ++k) {
                    if (registers_local[k] == parsed[2][k]) { innerCounter++; }
                }
                if (innerCounter == registersCount) { res.push_back(j); }
            }
            return res;
        };


        // Push_backs a vector of valid instrIDs to the right rawID position in the top-level vector of
        // rawIdToVofV_InstrIDs
        std::vector rawIdToVofV_InstrIDs(sizeof...(instrT), std::vector<std::vector<int>>());
        std::vector counter(sizeof...(instrT), std::vector<int>(sizeof...(instrT), 0));

        while (parseOneSet()) { rawIdToVofV_InstrIDs[parsed[1][0]].push_back(matcher()); }

        for (int i = 0; auto &rawIDgroup : rawIdToVofV_InstrIDs) {
            for (auto &validIDsGroup : rawIDgroup) {
                for (auto &validIDinGroup : validIDsGroup) { counter[i][validIDinGroup]++; }
            }
            i++;
        }


        // There can only be one 'real' matching 'instrID' per rawID and must match for all (ie. the size of
        // the vector). Depending on input might be necessary to gradually eliminate multiple matches.
        std::unordered_map<int, std::string, incstd::hashing::XXH3Hasher> IDsMap;

        while (IDsMap.size() < counter.size()) {
            for (int j = 0; auto &counterRangePerID : counter) {
                int idMatched = INT_MIN;
                for (int i = 0; auto &cnt : counterRangePerID) {
                    if (cnt == rawIdToVofV_InstrIDs[j].size()) {
                        if (idMatched != INT_MIN) {
                            idMatched = INT_MIN;
                            break;
                        }
                        idMatched = i;
                    }
                    i++;
                }
                if (idMatched != INT_MIN) {
                    IDsMap.emplace(idMatched, std::to_string(j));
                    for (auto &cntrRng : counter) { cntrRng[idMatched] = 0; }
                }
                j++;
            }
        }

        std::unordered_map<std::string, std::variant<instrT...>, incstd::hashing::XXH3Hasher> instrTypeMap;
        long long                                                                             cntr = 0;

        // Horrifying hack creating a dangling reference with the 'counter' being passed into Instr_T constructor
        (instrTypeMap.emplace(IDsMap.at(cntr++), std::variant<instrT...>{instrT{cntr, cntr, cntr}}), ...);
        return instrTypeMap;
    };

    // The one and only constructor of the 'prog' type
    ProgramQuasiAssembly_2018(
        const std::vector<std::vector<std::string>>                                           &rawInstrInput,
        std::unordered_map<std::string, std::variant<instrT...>, incstd::hashing::XXH3Hasher> &mapped,
        const std::vector<long long> regStartVal = {0, 0, 0, 0}) {

        assert((void("Prog type instantiated with an empty input"), rawInstrInput.size() > 0));
        for (auto &line : rawInstrInput) {
            assert((void("Prog type instantiated with an input that has more than 4 items on some instruction line"),
                    line.size() < 5));
            assert(
                (void("Prog type instantiated with an input that has some instruction line empty"), line.size() > 0));
        }

        registers.resize(rawInstrInput.size() * 3); // Must NEVER reallocate.

        // This loop finds out how many registers there actually are.
        int lastRegOccupied = INT_MIN;
        for (auto &line : rawInstrInput) {
            assert((void("Some instructions name in input doesn't match any type in template parameter pack"),
                    mapped.contains(line.front())));

            auto refVal_instructions =
                std::visit([&](auto &&a) -> std::vector<int> { return a.getRS(); }, mapped.at(line.front()));

            std::visit(
                [&](auto &&a) -> void {
                    if (refVal_instructions[0] == 0) {
                        lastRegOccupied = std::max(lastRegOccupied, std::stoi(line[1]));
                    }
                    if (refVal_instructions[1] == 0) {
                        lastRegOccupied = std::max(lastRegOccupied, std::stoi(line[2]));
                    }
                    if (refVal_instructions[2] == 0) {
                        lastRegOccupied = std::max(lastRegOccupied, std::stoi(line[3]));
                    }
                },
                mapped.at(line.front()));
        }


        assert((void("Inferred number of registers do not match the number of registers presumed by the programmer"),
                regStartVal.size() == (lastRegOccupied + 1)));

        // This loop set the right reference and copies the right variant into instrVect
        for (auto &line : rawInstrInput) {
            auto &variantInMap = mapped.at(line.front());
            auto  refVal_instructions_2 =
                std::visit([&](auto &&a) -> std::vector<int> { return a.getRS(); }, variantInMap);

            std::visit(
                [&](auto &&a) -> void {
                    if (refVal_instructions_2[0] == 0) { a.A = registers[std::stoi(line[1])]; }
                    else {
                        registers[++lastRegOccupied] = std::stoll(line[1]);
                        a.A                          = registers[lastRegOccupied];
                    }
                    if (refVal_instructions_2[1] == 0) { a.B = registers[std::stoi(line[2])]; }
                    else {
                        registers[++lastRegOccupied] = std::stoll(line[2]);
                        a.B                          = registers[lastRegOccupied];
                    }
                    if (refVal_instructions_2[2] == 0) { a.C = registers[std::stoi(line[3])]; }
                    else {
                        registers[++lastRegOccupied] = std::stoll(line[3]);
                        a.C                          = registers[lastRegOccupied];
                    }
                },
                variantInMap);
            // Push the right variant inside instrVect
            instrVect.push_back(std::variant<instrT...>(variantInMap));
        }
        // Set registers to their initial values (defaulted 4 registers to 0).
        for (int i = 0; auto &oneVal : regStartVal) { registers[i++] = oneVal; }
    }
};

template <typename... instrT>
requires(std::derived_from<instrT, _instrBase_INT> && ...)
class ProgramQuasiAssembly_INT {
private:
    std::unordered_map<long long, std::variant<instrT...>, incstd::hashing::XXH3Hasher> m_instrTypeMap;
    static std::unordered_map<long long, std::variant<instrT...>, incstd::hashing::XXH3Hasher>
    instrTypeMapCreator(std::vector<long long> const &instrCodes) {
        assert(sizeof...(instrT) == instrCodes.size());
        std::unordered_map<long long, std::variant<instrT...>, incstd::hashing::XXH3Hasher> res;

        long long id = 0;
        (res.insert({instrCodes[id++], instrT()}), ...);
        return res;
    }

public:
    long long              m_cursor             = 0;
    long long              m_relativeBaseOffset = 0;
    std::vector<long long> m_program;

    // CONSTRUCTION
    ProgramQuasiAssembly_INT() = default;
    ProgramQuasiAssembly_INT(std::vector<long long> const &instrCodes, std::vector<long long> const &programCodes,
                             long long cursor = 0)
        : m_instrTypeMap(instrTypeMapCreator(instrCodes)), m_program(programCodes), m_cursor(cursor) {};

    // IS FUNCTIONS
    bool
    is_cursorValid() {
        return ((m_cursor >= 0) && (m_cursor < m_program.size()));
    }

    // CONSTRUCTING VARIANTS
    auto
    get_externalCursorInstr(long long const &progCursor) -> std::variant<instrT...> {
        long long diviRes = (m_program[progCursor] / 100);
        long long instrID = (m_program[progCursor] % 100);

        std::vector<int> params;
        while (diviRes != 0) {
            params.push_back(diviRes % 10);
            diviRes /= 10;
        }

        std::variant<instrT...> constructedVar(m_instrTypeMap.at(instrID));

        auto finishTheVar = incstd::variant_utils::Overloads{
            [&](auto &a) -> void {
                long long              paramIDx  = 1;
                long long              maxCursor = 0;
                std::vector<long long> cursors;
                for (auto &param : params) {

                    // Position mode
                    if (param == 0) { cursors.push_back(m_program[progCursor + paramIDx]); }

                    // Immediate mode
                    else if (param == 1) { cursors.push_back(progCursor + paramIDx); }

                    // Relative mode
                    else if (param == 2) { cursors.push_back(m_relativeBaseOffset + m_program[progCursor + paramIDx]); }

                    // Can't do undefined parameter ID
                    else { assert(false); }

                    maxCursor = std::max(maxCursor, cursors.back());
                    paramIDx++;
                }

                for (; paramIDx < a.get_numOfParams() + 1; ++paramIDx) {
                    // Position mode - defaulted because of missing explicit param
                    cursors.push_back(m_program[progCursor + paramIDx]);
                    maxCursor = std::max(maxCursor, cursors.back());
                }
                // Resize the program memory so that the cursors to all current parameters are valid
                if (not(maxCursor < m_program.size())) { m_program.resize(maxCursor + 1, 0LL); }

                for (auto &cur : cursors) { a.m_refs.push_back(m_program[cur]); }
            },
        };

        std::visit(finishTheVar, constructedVar);
        return constructedVar;
    }
    auto
    get_pointedToInstr() -> std::variant<instrT...> {
        return get_externalCursorInstr(m_cursor);
    }

    // EXECUTION OF INSTRUCTIONS
    auto
    exe_externalPointedToInstr(long long &externalID, auto const &ol_set) -> long long {
        auto instruction = get_externalCursorInstr(externalID);

        auto ol_set_numOfParams =
            incstd::variant_utils::Overloads{[](auto &instr) { return instr.get_numOfParams() + 1; }};

        std::visit(ol_set, instruction);
        return externalID += std::visit(ol_set_numOfParams, instruction);
    }
    auto
    exe_pointedToInstr(auto const &ol_set) -> long long {
        return exe_externalPointedToInstr(m_cursor, std::forward<decltype(ol_set)>(ol_set));
    }
};

} // namespace PQA
} // namespace aoc
} // namespace incom
