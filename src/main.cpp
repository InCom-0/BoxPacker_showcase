

#include <cstddef>
#include <iostream>
#include <mdspan>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <vector>


#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_internal.h>

#include <exec/async_scope.hpp>
#include <exec/execute.hpp>
#include <exec/repeat_n.hpp>
#include <exec/repeat_until.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/task.hpp>
#include <stdexec/execution.hpp>


#include <ankerl/unordered_dense.h>
#include <incstd/console/colorschemes.hpp>
#include <incstd/core/solvers.hpp>
#include <readerwriterqueue.h>


#define SDL_MAIN_HANDLED
#include <SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif
#ifdef _WIN32
#include <windows.h> // SetProcessDPIAware()
#endif

// This example can also compile and run with Emscripten! See 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include <boxpacker_private/emscripten_mainloop_stub.hpp>
#endif

#include <boxpacker_private/bp_handling.hpp>
// #include <boxpacker_private/emscripten_file_picker_async.hpp>
#include <boxpacker_private/emscripten_browser_file.hpp>
#include <boxpacker_private/incom_async.hpp>
#include <boxpacker_private/incom_commons.hpp>


#ifdef __EMSCRIPTEN__
#define BOXPACKER_SAMPLE_INPUT "/data/BoxPacker_sample_input.txt"
#else
#define BOXPACKER_SAMPLE_INPUT "../../../data/BoxPacker_sample_input.txt"
#endif

struct UploadedFile {
    std::string filename;
    std::string mime_type;
    std::string data;
};

static std::deque<UploadedFile> g_uploaded_files;

void handle_upload_file(std::string const &filename, std::string const &mime_type, std::string_view buffer,
                        void *callback_data) {
    auto &cb_data{*reinterpret_cast<
        std::vector<std::tuple<std::string, incom::box_packer::ShapesStorage, std::vector<incom::box_packer::Tree>>> *>(
        callback_data)};
    g_uploaded_files.push_back(UploadedFile{filename, mime_type, std::string(buffer.data(), buffer.size())});
    cb_data.push_back(incom::box_packer::get_externalSampleData(g_uploaded_files.back().data));
    std::get<0>(cb_data.back()) = filename;
}

// Main code
int main(int, char **) {

#pragma region SDL2_setup
#ifdef _WIN32
    ::SetProcessDPIAware();
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("Error: %s\n", SDL_GetError());
        return 1;
    }

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    const char *glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
    const char *glsl_version = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    const char *glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char *glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif


    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    float           main_scale = ImGui_ImplSDL2_GetContentScaleForDisplay(0);
    SDL_WindowFlags window_flags =
        (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window *window = SDL_CreateWindow("AOC 2025 Day 12 solver", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
    if (window == nullptr) {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr) {
        printf("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_MakeCurrent(window, gl_context);
#ifndef __EMSCRIPTEN__
    SDL_GL_SetSwapInterval(1); // Enable vsync
#endif

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale); // Bake a fixed style scale. (until we have a solution for dynamic style scaling,
                                     // changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale; // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true
                                     // automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    io.Fonts->AddFontDefaultVector();

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

#pragma endregion SDL2_setup

    // ##################################
    // ### Setting up data structures
    // ##################################

    namespace incpack = incom::standard::solvers::packing;
    using namespace std::chrono_literals;
    std::string df{BOXPACKER_SAMPLE_INPUT};

    std::vector<std::tuple<std::string, incom::box_packer::ShapesStorage, std::vector<incom::box_packer::Tree>>>
        sampleInputs{incom::box_packer::get_integratedSampleData(df)};

    auto [_, shps, trees] = sampleInputs.front();

    if (trees.empty()) {
        std::cerr << "Failed to load sample input from: " << df << '\n';
        return 1;
    }

    auto shpsForBoxPacker_view = std::views::transform(shps.m_shapes, [](auto const &item) {
        return incpack::BoxPacker_2D<5>::calculate_rotFlipped(item.template conv_intoArr_bools<3uz, 3uz>());
    });


    // Pre-computing the 'example' labels
    std::vector<std::string> treeLabels;
    auto                     updateTreeLabels = [&]() {
        if (not treeLabels.empty()) { treeLabels.clear(); }
        for (size_t id = 0uz; auto const &oneTree : trees) {
            treeLabels.push_back(std::format("{0:}: {1:}x{2:} (", id, oneTree.yDim, oneTree.xDim));
            treeLabels.back().append(std::format("{:n}", oneTree.reqdShapes));
            treeLabels.back().push_back(')');
            ++id;
        }
    };
    updateTreeLabels();


    size_t                               oneShape_sideSize = 3uz; // Change later so that it can adjusted manually
    auto                                 oneTree           = trees.front();
    std::vector<incom::box_packer::Tree> planTrees;


    exec::static_thread_pool tPool_work{8};
    auto                     tPool_workSch = tPool_work.get_scheduler();

    std::vector<std::pair<std::unique_ptr<decltype(incom::standard::async::spawn(
                              incom::box_packer::bp_asyncExecute, tPool_workSch, trees,
                              std::vector(std::from_range, shpsForBoxPacker_view), incom::standard::async::Separator{},
                              moodycamel::ReaderWriterQueue<
                                  std::tuple<size_t, incom::box_packer::BP_Pos, incom::box_packer::BP_PastRes>>{}))>,
                          incom::box_packer::SolveResStore>>
        jobs;


    // ##################################
    // ### MAIN LOOP START
    // ##################################
    bool done                = false;
    bool waitOnCancelledJobs = false;
    bool waitOnStoppedJobs   = false;

    static std::optional<size_t> sel_jobID = std::nullopt;
    static std::optional<size_t> sel_resID = std::nullopt;

#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the
    // imgui.ini file. You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    while (! done)
#endif
    {
        // Handle messages from all jobs and store them locally in main thread
        for (auto &[jr, resStore] : jobs) {
            std::tuple<size_t, incom::box_packer::BP_Pos, incom::box_packer::BP_PastRes> dq;
            while (std::get<0>(jr->m_qs).peek() != nullptr) {
                auto const &[resID, itemPos, itemPR] = *std::get<0>(jr->m_qs).peek();
                resStore.vecOfRes.at(resID).push_back({itemPos, itemPR});
                resStore.endOfVisible.at(resID) = resStore.vecOfRes.at(resID).size();

                resStore.update_oneShape(resID, resStore.vecOfRes.at(resID).size() - 1);
                std::get<0>(jr->m_qs).pop();
            }
        }

        // All jobs were cancelled
        if (waitOnCancelledJobs) {
            // If cancal of all jobs requested then we wait for them to sync
            for (auto const &[job, _] : jobs) { stdexec::sync_wait(job->m_ascope.on_empty()); }
            waitOnCancelledJobs = false;
            jobs.clear();
            sel_jobID = std::nullopt;
            sel_resID = std::nullopt;
        }

        // All jobs were stopped
        if (waitOnStoppedJobs) {
            // If cancal of all jobs requested then we wait for them to sync
            for (auto const &[job, _] : jobs) { stdexec::sync_wait(job->m_ascope.on_empty()); }
            waitOnStoppedJobs = false;
        }


#pragma region SDL2_loopEventFrameHandling
        // Event handling
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) { done = true; }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window)) {
                done = true;
            }
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
#pragma endregion SDL2_loopEventFrameHandling

        {
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

            ImGui::Begin("AOC 2025 day 12 solver", 0,
                         ImGuiWindowFlags_NoTitleBar); // Create a window called "Hello, world!" and append into it.

            // ImGui::Text("%zu", sampleInputs.size());
            // if (sampleInputs.size() > 1) {
            //     auto const &uploadedShapes = std::get<1>(sampleInputs.at(1));
            //     auto const &uploadedTree   = std::get<2>(sampleInputs.at(1));
            //     ImGui::Text("%zu", uploadedShapes.m_shapes.size());
            //     ImGui::Text("%zu", uploadedTree.size());
            // }

            ImGui::TextWrapped("Interactive heuristic solver and solution explorer for Advent of Code 2025 day 12.");
            ImGui::TextWrapped("It does not find the 'provably best' solution. It finds 'pretty good' solutions at "
                               "lightning speed using a heuristic process resembling how a human might approach this.");
            ImGui::Dummy(ImGui::GetItemRectSize());

            struct AnimControl {
                std::chrono::nanoseconds m_oneIterDuration = std::chrono::nanoseconds::max();
                std::chrono::nanoseconds m_elapsedDuration = std::chrono::nanoseconds::zero();
                bool                     m_beingAnimated   = false;

                std::chrono::time_point<std::chrono::high_resolution_clock> m_start =
                    std::chrono::high_resolution_clock::now();

                bool can_step() {
                    return (m_oneIterDuration == std::chrono::nanoseconds::max()
                                ? false
                                : ((m_start + m_elapsedDuration + m_oneIterDuration) <=
                                   std::chrono::high_resolution_clock::now()));
                }
                void do_oneStep() { m_elapsedDuration += m_oneIterDuration; }

                void start() {
                    m_start           = std::chrono::high_resolution_clock::now();
                    m_elapsedDuration = std::chrono::nanoseconds::zero();
                    m_beingAnimated   = true;
                }

                void terminate() { m_beingAnimated = false; }

                void set_speed(int itersPerSec) {
                    m_oneIterDuration = itersPerSec ? std::chrono::nanoseconds(1'000'000'000 / itersPerSec)
                                                    : std::chrono::nanoseconds::max();
                }
            };
            static AnimControl animC{.m_oneIterDuration = std::chrono::nanoseconds(1'000'000'000 / 40)};

            // ##################################
            // ### Main Controls
            // ##################################
            {

                ImGui::BeginChild("MainControls_window",
                                  ImVec2(ImGui::GetContentRegionAvail().x * 0.33f, 13 * 26.0f + 20.f),
                                  ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);

                static size_t treeSelectedID  = 0uz;
                static size_t inputSelectedID = 0uz;
                // ### Plan control
                {
                    ImGui::SeparatorText("Main Controls");

                    if (ImGui::BeginCombo("<- Select input", std::get<0>(sampleInputs[inputSelectedID]).data(), 0)) {
                        for (int n = 0; n < sampleInputs.size(); n++) {
                            const bool is_selected = (inputSelectedID == n);
                            if (ImGui::Selectable(std::get<0>(sampleInputs.at(n)).data(), is_selected)) {
                                inputSelectedID = n;
                                shps            = std::get<1>(sampleInputs.at(n));
                                trees           = std::get<2>(sampleInputs.at(n));
                                updateTreeLabels();
                                treeSelectedID = 0uz;
                            }
                        }
                        ImGui::EndCombo();
                    }

                    if (ImGui::BeginCombo("<- Select sample", treeLabels[treeSelectedID].data(), 0)) {
                        static ImGuiTextFilter filter;
                        if (ImGui::IsWindowAppearing()) {
                            ImGui::SetKeyboardFocusHere();
                            filter.Clear();
                        }
                        ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F);
                        filter.Draw("##Filter", -FLT_MIN);

                        for (int n = 0; n < treeLabels.size(); n++) {
                            const bool is_selected = (treeSelectedID == n);
                            if (filter.PassFilter(treeLabels[n].data())) {
                                if (ImGui::Selectable(treeLabels[n].data(), is_selected)) { treeSelectedID = n; }
                            }
                        }
                        ImGui::EndCombo();
                    }


                    if (ImGui::Button("Add to plan")) { planTrees.push_back(trees.at(treeSelectedID)); }
                    ImGui::SameLine();
                    if (ImGui::Button("Add all to plan")) { planTrees.append_range(trees); }
                    ImGui::SameLine();
                    if (ImGui::Button("Upload input")) {

                        std::string my_data{"hello world"};
                        auto        my_data_ptr{reinterpret_cast<void *>(&my_data)};


#if defined(__EMSCRIPTEN__)
                        // pass callback data to the handler
                        emscripten_browser_file::upload(".txt", handle_upload_file, &sampleInputs);
#endif
                        // emscripten_file_picker_async::upload(".png,.jpg,.jpeg", handle_upload_file);
                    }

                    ImGui::Dummy(ImGui::GetItemRectSize());
                }

                // ### Convenience helpers
                {
                    ImGui::SeparatorText("Convenience helpers");
                    if (ImGui::Button("Selected sample -> custom")) {
                        shps    = std::get<1>(sampleInputs.at(inputSelectedID));
                        oneTree = trees.at(treeSelectedID);
                    }
                    if (ImGui::Button("Clear plan")) { planTrees.clear(); }
                    ImGui::SameLine();
                    if (ImGui::Button("De-duplicate plan")) {

                        ankerl::unordered_dense::set<incom::box_packer::Tree, incom::standard::hashing::XXH3Hasher> st;

                        std::vector<char> deleteMarked;
                        for (auto &oneTree : planTrees) {
                            auto [_, inserted] = st.insert(oneTree);
                            deleteMarked.push_back(not inserted);
                        }
                        auto removed = std::ranges::remove_if(
                            planTrees, std::identity{}, // predicate sees projected value (bool-like)
                            [base = planTrees.data(), &deleteMarked](incom::box_packer::Tree const &t) {
                                auto idx = static_cast<size_t>(&t - base);
                                return deleteMarked[idx] != 0;
                            });
                        planTrees.erase(removed.begin(), removed.end());
                    }


                    // ### Job Control
                    ImGui::Dummy(ImGui::GetItemRectSize());
                }

                // ### Job control
                {
                    ImGui::SeparatorText("Job control");
                    if (ImGui::Button("Execute plan") and not planTrees.empty()) {

                        // Repair planTrees so the shapeCounts for each tree inside match the shapes that will be used
                        for (auto &oneTree : planTrees) {
                            while (oneTree.reqdShapes.size() > shps.m_shapes.size()) { oneTree.reqdShapes.pop_back(); }
                            while (oneTree.reqdShapes.size() < shps.m_shapes.size()) {
                                oneTree.reqdShapes.push_back(0uz);
                            }
                        }

                        jobs.push_back(std::make_pair(
                            incom::standard::async::spawn_uptr(
                                incom::box_packer::bp_asyncExecute, tPool_workSch, planTrees,
                                std::vector(std::from_range, shpsForBoxPacker_view),
                                incom::standard::async::Separator{},
                                moodycamel::ReaderWriterQueue<
                                    std::tuple<size_t, incom::box_packer::BP_Pos, incom::box_packer::BP_PastRes>>{
                                    4096}),
                            incom::box_packer::SolveResStore(planTrees,
                                                             std::vector(std::from_range, shpsForBoxPacker_view))));

                        sel_jobID = jobs.size() - 1;
                        sel_resID = std::nullopt;
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Cancel all jobs")) {
                        waitOnCancelledJobs = true;
                        for (auto const &[job, _] : jobs) { job->m_ascope.request_stop(); }
                        // cancel all jobs in this scope ... need to somehow iterate
                        // stdexec::sync_wait(asyncScope.on_empty());
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Stop all jobs")) {
                        waitOnStoppedJobs = true;
                        for (auto const &[job, _] : jobs) { job->m_ascope.request_stop(); }
                    }

                    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, 4);
                }

                ImGui::EndChild();
            }

            // ##################################
            // ### Shapes selector
            // ##################################
            {
                ImGui::SameLine();
                ImGui::BeginChild("CustomAdjust_window", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0),
                                  ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::SeparatorText("Custom shapes, counts and sizes");

                {
                    size_t const rowCount  = (shps.m_shapes.size() + 2uz) / 3uz;
                    size_t       curShpIDX = 0uz;
                    ImGui::BeginGroup();
                    for (int r = 0; r < rowCount; ++r) {
                        size_t const colCount = std::min(3uz, shps.m_shapes.size() - curShpIDX);
                        for (int c = 0; c < colCount; ++c) {
                            ImGui::BeginGroup();

                            // Shape table header (count of shapes DragInt)
                            int curVal = oneTree.reqdShapes.at(curShpIDX);
                            ImGui::PushItemWidth(81.0);
                            ImGui::PushID(r * 3 + c);
                            ImGui::DragInt("", &curVal, 0.1f, 0, 100, "%d");
                            oneTree.set_reqdShape(curShpIDX, curVal);
                            ImGui::PopID();
                            ImGui::PopItemWidth();

                            // Shape table begin
                            ImGui::PushID(r * rowCount + c);
                            if (ImGui::BeginTable("OneShape_table", 3,
                                                  ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV |
                                                      ImGuiTableFlags_BordersH | ImGuiTableFlags_SizingFixedSame |
                                                      ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_NoPadOuterX |
                                                      ImGuiTableFlags_NoPadInnerX,
                                                  ImVec2(0.0f, 0.0f))) {


                                for (int c = 0; c < 3; ++c) {
                                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 26.0f);
                                };

                                auto spn = shps.m_shapes.at(curShpIDX).get_viewInto<3uz, 3uz>();
                                // auto spn = shps.m_shapes.at(curShpIDX).get_viewInto(3uz, 3uz);
                                for (int tRow = 0; tRow < 3; tRow++) {
                                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 26);
                                    for (int tCol = 0; tCol < 3; tCol++) {

                                        ImGui::PushID(tRow * 3 + tCol);
                                        ImGui::TableSetColumnIndex(tCol);
                                        if (ImGui::Selectable("", false)) {
                                            spn[tRow, tCol] = not static_cast<bool>(spn[tRow, tCol]);
                                        }

                                        ImGui::TableSetBgColor(
                                            ImGuiTableBgTarget_CellBg,
                                            ImGui::GetColorU32(spn[tRow, tCol] != 0 ? ImVec4(0.0f, 1.0f, 0.0f, 0.65f)
                                                                                    : ImVec4(0.0f, 0.0f, 0.0f, 0.65f)));

                                        // Drag and drop functionality (swapping of shapes)
                                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                            ImGui::SetDragDropPayload("OneShape", &curShpIDX, sizeof(size_t));
                                            ImGui::EndDragDropSource();
                                        }
                                        if (ImGui::BeginDragDropTarget()) {
                                            if (const ImGuiPayload *payload =
                                                    ImGui::AcceptDragDropPayload("OneShape")) {
                                                IM_ASSERT(payload->DataSize == sizeof(size_t));
                                                size_t const payload_n = *(const size_t *)payload->Data;
                                                if (payload_n != curShpIDX) { shps.swap(payload_n, curShpIDX); }
                                            }
                                            ImGui::EndDragDropTarget();
                                        }
                                        ImGui::PopID();
                                    }
                                }
                                ImGui::EndTable();
                            }
                            ImGui::PopID();

                            ++curShpIDX;
                            ImGui::EndGroup();
                            if (c != (colCount - 1)) { ImGui::SameLine(); }
                        }
                    }
                    ImGui::Dummy(ImVec2{0, ImGui::GetItemRectSize().y / 8});
                    if (ImGui::Button("Counts, sizes -> plan",
                                      ImVec2(9 * 26 + ImGui::GetStyle().FramePadding.x * 6, 0))) {
                        planTrees.push_back(oneTree);
                    }
                }
                ImGui::EndGroup();

                ImGui::SameLine();
                ImGui::Dummy({26, 0});
                ImGui::SameLine();

                ImGui::BeginGroup();

                ImGui::PushID(0);
                if (ImGui::Button("  ")) {
                    shps.m_shapes.push_back(
                        {.m_data = std::vector<uint32_t>(oneShape_sideSize * oneShape_sideSize, 0)});
                    oneTree.reqdShapes.push_back(0);
                }
                ImGui::PopID();
                ImGui::SameLine();
                ImGui::Text("Add shape");


                ImGui::PushID(1);
                if (ImGui::Button("  ")) {
                    if (shps.m_shapes.empty() || oneTree.reqdShapes.empty()) {}
                    else {
                        shps.m_shapes.pop_back();
                        oneTree.reqdShapes.pop_back();
                    }
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("OneShape")) {
                        IM_ASSERT(payload->DataSize == sizeof(size_t));
                        size_t const payload_n = *(const size_t *)payload->Data;

                        shps.removeErase_oneID(payload_n);
                        oneTree.removeErase_oneID(payload_n);
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();
                ImGui::SameLine();
                ImGui::Text("Remove shape");


                ImGui::Dummy(ImGui::GetItemRectSize());

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7);

                int curVal = oneTree.xDim;
                ImGui::DragInt("xSize", &curVal, 0.2f, 0, 100, "%d");
                oneTree.xDim = std::max(0, curVal);

                curVal = oneTree.yDim;
                ImGui::DragInt("ySize", &curVal, 0.2f, 0, 100, "%d");
                oneTree.yDim = std::max(0, curVal);

                ImGui::PopItemWidth();

                ImGui::TextWrapped("The runner always grabs the shape 'types' from here when when queued.");
                ImGui::EndGroup();
                ImGui::EndChild();
            }

            // ##################################
            // ### Runner Plan
            // ##################################
            {
                auto const prevGroupSize_v = ImGui::GetItemRectSize().y;
                ImGui::SameLine();
                ImGui::BeginChild("RunnerPlan", ImVec2(ImGui::GetContentRegionAvail().x, 0),
                                  ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_HorizontalScrollbar);

                ImGui::SeparatorText("Plan");
                if (ImGui::BeginTable("table_context_menu_2", 3, ImGuiTableFlags_ScrollY, {0, prevGroupSize_v})) {
                    ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                    ImGui::TableSetupColumn("Size");
                    ImGui::TableSetupColumn("Shape counts");
                    ImGui::TableSetupColumn("");
                    ImGui::TableHeadersRow();

                    std::optional<size_t> idToDel = std::nullopt;

                    ImGuiListClipper clipper;
                    clipper.Begin(planTrees.size());
                    while (clipper.Step()) {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%dx%d", planTrees[row].yDim, planTrees[row].xDim);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("(%s)", std::format("{:n}", planTrees[row].reqdShapes).data());

                            ImGui::TableSetColumnIndex(2);
                            ImGui::PushID(row * 3 + 2);
                            if (ImGui::SmallButton("D")) { idToDel = row; }
                            ImGui::PopID();
                        }
                        if (idToDel) { planTrees.erase(planTrees.begin() + idToDel.value()); }
                    }
                    clipper.End();
                    ImGui::EndTable();
                }


                ImGui::EndChild();
            }

            // ##################################
            // ### Current runners
            // ##################################
            {
                static int rewindSlider = 0;


                // ##################################
                // ### Runners to view
                // ##################################
                ImGui::SeparatorText("Current runners");
                {
                    ImGui::BeginGroup();
                    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.15f);
                    if (ImGui::BeginCombo(
                            "##SelectJob",
                            sel_jobID ? std::format("Job {}", sel_jobID.value()).data() : std::string("").data(), 0)) {

                        if (ImGui::Selectable("##", not sel_jobID)) {
                            sel_jobID = std::nullopt;
                            sel_resID = std::nullopt;
                            animC.terminate();
                        }

                        for (size_t n = 0; n < jobs.size(); n++) {
                            bool const selected = sel_jobID ? sel_jobID.value() == n : false;
                            if (ImGui::Selectable(std::format("Job {}", n).data(), selected)) {
                                sel_jobID = n;
                                sel_resID = std::nullopt;
                            }
                        }
                        ImGui::EndCombo();
                    }

                    if (ImGui::BeginListBox("##ResListBox", ImVec2(0, 20 * ImGui::GetTextLineHeightWithSpacing()))) {
                        if (ImGui::Selectable("##", not sel_resID)) { sel_resID = std::nullopt; }
                        if (sel_jobID) {
                            for (size_t n = 0; n < jobs.at(sel_jobID.value()).second.vecOfRes.size(); n++) {

                                bool const finished =
                                    jobs.at(sel_jobID.value()).second.vecOfRes.at(n).size() != 0 ? true : false;
                                bool const is_selected = sel_resID ? sel_resID.value() == n : false;
                                if (ImGui::Selectable(std::format("Task {} {}", n, finished ? "... Done" : "").data(),
                                                      is_selected)) {
                                    sel_resID = n;
                                    animC.terminate();
                                    rewindSlider = static_cast<int>(
                                        jobs.at(sel_jobID.value()).second.endOfVisible.at(sel_resID.value()));
                                }

                                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                                if (is_selected) { ImGui::SetItemDefaultFocus(); }
                            }
                        }
                        ImGui::EndListBox();
                    }
                    ImGui::PopItemWidth();
                    ImGui::EndGroup();
                }


                // ##################################
                // ### Views of the runner
                // ##################################
                {
                    if (sel_jobID && sel_resID) {
                        ImGui::SameLine();
                        ImGui::BeginChild("CurrentRunners_child", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                                          ImGuiWindowFlags_HorizontalScrollbar);
                        ImGui::Dummy(
                            ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing()));

                        ImGui::SameLine();


                        ImGui::BeginGroup();

                        auto const &selJobSolvRes = std::get<1>(jobs.at(sel_jobID.value()));

                        if (animC.m_beingAnimated) {
                            while (animC.can_step() &&
                                   (rewindSlider < selJobSolvRes.vecOfRes.at(sel_resID.value()).size())) {
                                rewindSlider++;
                                animC.do_oneStep();
                            }
                            if (rewindSlider == selJobSolvRes.vecOfRes.at(sel_resID.value()).size()) {
                                animC.terminate();
                            }
                        }

                        // Rewind slider
                        ImGui::PushItemWidth(
                            std::get<1>(jobs.at(sel_jobID.value())).m_trees.at(sel_resID.value()).xDim * 17.0f);
                        if (ImGui::SliderInt("##int", &rewindSlider, 0,
                                             jobs.at(sel_jobID.value()).second.vecOfRes.at(sel_resID.value()).size())) {
                            rewindSlider = std::clamp(
                                rewindSlider, 0,
                                static_cast<int>(
                                    jobs.at(sel_jobID.value()).second.vecOfRes.at(sel_resID.value()).size()));
                        }

                        // Moving the displayed data based on the slider (or animation)
                        if (jobs.at(sel_jobID.value()).second.endOfVisible.at(sel_resID.value()) != rewindSlider) {
                            int const difference =
                                (rewindSlider - jobs.at(sel_jobID.value()).second.endOfVisible.at(sel_resID.value()));

                            jobs.at(sel_jobID.value()).second.moveInTime_area(sel_resID.value(), difference);
                            jobs.at(sel_jobID.value()).second.endOfVisible.at(sel_resID.value()) = rewindSlider;
                        }
                        ImGui::PopItemWidth();

                        auto const rewindSize = ImGui::GetItemRectSize();
                        ImGui::Dummy(rewindSize);


                        // ##################################
                        // ### Result view
                        // ##################################
                        static float cellSize = 16.0f;
                        if (ImGui::BeginTable("OneResTable", selJobSolvRes.m_trees.at(sel_resID.value()).xDim,
                                              ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV |
                                                  ImGuiTableFlags_BordersH | ImGuiTableFlags_SizingFixedSame |
                                                  ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_NoPadOuterX |
                                                  ImGuiTableFlags_NoPadInnerX,
                                              ImVec2(0.0f, 0.0f))) {

                            // Setup each column
                            for (int c = 0; c < selJobSolvRes.m_trees.at(sel_resID.value()).xDim; ++c) {
                                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, cellSize);
                            };

                            // Draw each square (ie. set the right background color)
                            for (int tRow = 1; tRow < selJobSolvRes.m_trees.at(sel_resID.value()).yDim + 1; tRow++) {
                                ImGui::TableNextRow(ImGuiTableRowFlags_None, cellSize);
                                for (int tCol = 1; tCol < selJobSolvRes.m_trees.at(sel_resID.value()).xDim + 1;
                                     tCol++) {

                                    ImGui::TableSetColumnIndex(tCol - 1);

                                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                                           selJobSolvRes.colorsToUse.at(
                                                               selJobSolvRes.accs.at(sel_resID.value())[tRow, tCol]));
                                }
                            }
                            ImGui::EndTable();
                        }
                        ImGui::EndGroup();

                        ImGui::SameLine();
                        ImGui::Dummy(ImVec2{16.0f, rewindSize.y});

                        ImGui::SameLine();
                        ImGui::BeginGroup();
                        ImGui::Dummy(ImVec2{16.0f, rewindSize.y});
                        ImGui::Dummy(ImVec2{16.0f, rewindSize.y});


                        // ##################################
                        // ### Result animation controls
                        // ##################################
                        {
                            ImGui::BeginGroup();
                            {
                                bool animatedAtBeg = false;
                                if (animC.m_beingAnimated) {
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
                                    animatedAtBeg = true;
                                }
                                if (ImGui::Button("Run")) { animC.start(); }

                                if (animatedAtBeg) { ImGui::PopStyleColor(); }
                            }

                            {
                                bool pausedAtBeg = false;
                                if (not animC.m_beingAnimated) {
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
                                    pausedAtBeg = true;
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Pause")) { animC.terminate(); }
                                if (not animC.m_beingAnimated && pausedAtBeg) { ImGui::PopStyleColor(); }
                            }

                            ImGui::SameLine();
                            if (ImGui::Button("Rewind")) {
                                rewindSlider = 0;
                                animC.terminate();
                            }
                            ImGui::EndGroup();

                            ImGui::PushItemWidth(ImGui::GetItemRectSize().x);
                            static int animSpeedPerSec = 40;
                            if (ImGui::DragInt("##AnimSpeed", &animSpeedPerSec, 0.2f, 0, 1000, "iter/sec:  %d")) {
                                animSpeedPerSec = std::max(animSpeedPerSec, 0);
                                animC.set_speed(animSpeedPerSec);
                            }

                            if (ImGui::DragFloat("##CellSize", &cellSize, 0.02f, 1.0f, 64.0f, "cell size:  %.0f")) {
                                cellSize = std::max(cellSize, 1.0f);
                            }

                            ImGui::PopItemWidth();

                            ImGui::Dummy(ImVec2{16.0f, rewindSize.y});
                        }

                        // ##################################
                        // ### Sahpes used by the result
                        // ##################################
                        for (int r = 0; r < selJobSolvRes.m_shpsAlterns.size(); ++r) {
                            ImGui::BeginGroup();

                            // Shape table begin
                            ImGui::PushID(r);
                            if (ImGui::BeginTable("OneShape_table", 3,
                                                  ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV |
                                                      ImGuiTableFlags_BordersH | ImGuiTableFlags_SizingFixedSame |
                                                      ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_NoPadOuterX |
                                                      ImGuiTableFlags_NoPadInnerX,
                                                  ImVec2(0.0f, 0.0f))) {


                                for (int c = 0; c < 3; ++c) {
                                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 26.0f);
                                };

                                auto &spn = selJobSolvRes.m_shpsAlterns.at(r).at(0);
                                // auto spn = shps.m_shapes.at(curShpIDX).get_viewInto(3uz, 3uz);
                                for (int tRow = 0; tRow < 3; tRow++) {
                                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 26);
                                    for (int tCol = 0; tCol < 3; tCol++) {

                                        ImGui::PushID(tRow * 3 + tCol);
                                        ImGui::TableSetColumnIndex(tCol);

                                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                                               spn[tRow][tCol] != 0
                                                                   ? selJobSolvRes.colorsToUse.at(r + 1)
                                                                   : selJobSolvRes.colorsToUse.at(0));

                                        ImGui::PopID();
                                    }
                                }
                                ImGui::EndTable();
                            }
                            ImGui::PopID();

                            // Shape table header (count of shapes DragInt)
                            ImGui::SameLine();
                            ImGui::BeginGroup();
                            ImGui::PushItemWidth(81.0);
                            ImGui::PushID(r);
                            int curVal = 0;

                            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, selJobSolvRes.colorsToUse.at(r + 1));
                            ImGui::Text("Requested: %lu", selJobSolvRes.m_trees.at(sel_resID.value()).reqdShapes.at(r));
                            ImGui::Text("Shape alterns: %lu", selJobSolvRes.m_shpsAlterns.at(r).size());
                            ImGui::Dummy(ImGui::GetItemRectSize());


                            size_t const totalToPlace = selJobSolvRes.m_trees.at(sel_resID.value()).reqdShapes.at(r);
                            size_t const remainingToPlace =
                                totalToPlace - selJobSolvRes.m_curPlacedCount.at(sel_resID.value()).at(r);

                            char buf[32];
                            sprintf(buf, "%lu/%lu", remainingToPlace, totalToPlace);
                            ImGui::ProgressBar(static_cast<float>(remainingToPlace) / totalToPlace,
                                               ImVec2(ImGui::GetItemRectSize().x, 0.f), buf);

                            ImGui::PopStyleColor();

                            ImGui::PopID();
                            ImGui::PopItemWidth();
                            ImGui::EndGroup();

                            ImGui::EndGroup();
                            // if (c != (colCount - 1)) { ImGui::SameLine(); }
                        }

                        ImGui::EndGroup();
                        ImGui::EndChild();
                    }
                }
            }

            // ##################################
            // ### Logging
            // ##################################
            ImGui::SeparatorText("Event Log");

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }
#pragma region SDL2_Rendering
        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w,
                     clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
#ifndef __EMSCRIPTEN__
        SDL_Delay(6);
#endif
#pragma endregion SDL2_Rendering
    }
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
#endif

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
