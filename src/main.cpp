// Dear ImGui: standalone example application for SDL2 + OpenGL
// (SDL is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context
// creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_internal.h>
#include <stdio.h>

#include <string>
#include <vector>

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
#include <boxpacker_private/emscripten_mainloop_stub.h>
#endif

#include <boxpacker_private/background_work_sketch.h>
#include <boxpacker_private/bp_handling.hpp>
#include <boxpacker_private/incom_commons.h>


#define BOXPACKER_SAMPLE_INPUT "../../../data/BoxPacker_sample_input.txt"

// Main code
int main(int, char **) {
    std::string df{BOXPACKER_SAMPLE_INPUT};

    auto any_ctre = ctre::search<R"(.+)">;
    auto d_ctre   = ctre::search<R"(\d+)">;
    auto input    = incom::aoc::parseInputUsingCTRE::processFile(df, any_ctre).front();

    struct Shape_LOC {
        std::array<std::array<bool, 3>, 3> matrices;
    };
    struct Tree {
        int                 yDim;
        int                 xDim;
        std::vector<size_t> reqdShapes;
    };

    // std::vector<Shape_LOC> shapes_ORIG;
    // std::vector<Tree>      trees_ORIG;

    auto const [shapes_ORIG, trees_ORIG] = incom::box_packer::get_integratedSampleData(df);

    auto shapes       = shapes_ORIG;
    auto trees        = trees_ORIG;
    namespace incpack = incom::standard::solvers::packing;


    // Setup SDL
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
    SDL_Window *window =
        SDL_CreateWindow("Dear ImGui SDL2+OpenGL3 example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
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
    SDL_GL_SetSwapInterval(1); // Enable vsync

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
    ImVec4                                  clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    boxpacker_private::BackgroundWorkSketch background_work;
    std::vector<std::string>                recent_events;

    incom::box_packer::ShapesStorage shps{};

    // Main loop
    bool done = false;
#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the
    // imgui.ini file. You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    while (! done)
#endif
    {
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

        for (std::string message : background_work.drain_completed_messages()) {
            recent_events.push_back(std::move(message));
        }
        if (recent_events.size() > 6) { recent_events.erase(recent_events.begin(), recent_events.end() - 6); }


        {
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

            ImGui::Begin("Hello, world!", 0,
                         ImGuiWindowFlags_NoTitleBar); // Create a window called "Hello, world!" and append into it.

            ImGui::TextWrapped("Sketch: background work runs on worker threads, progress is polled by the main thread "
                               "every frame, and completion messages are drained into UI state.");

            if (ImGui::Button("Reset shapes")) { shapes = shapes_ORIG; }

            if (ImGui::Button("Queue background job")) { background_work.start_demo_job(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel all jobs")) { background_work.cancel_all(); }
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, 4);

            ImGui::SeparatorText("Background Jobs");
            const auto job_snapshots = background_work.snapshot_jobs();
            if (job_snapshots.empty()) { ImGui::TextDisabled("No work scheduled yet."); }
            else {
                for (const auto &job : job_snapshots) {
                    ImGui::PushID(static_cast<int>(job.id));
                    ImGui::Text("Job #%llu", static_cast<unsigned long long>(job.id));
                    ImGui::ProgressBar(job.progress, ImVec2(0.f, 0.f));
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                    ImGui::Text("%s", job.status.c_str());
                    if (job.cancelled) { ImGui::TextDisabled("Cancelled before completion."); }
                    else if (job.done) { ImGui::TextDisabled("Finished and ready for result handoff."); }
                    else { ImGui::TextDisabled("Running on a worker thread."); }
                    ImGui::PopID();
                }
            }

            {
                ImGui::BeginChild("ChildL", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 15 * 26.0f + 20.f),
                                  ImGuiChildFlags_None, 0);

                size_t curShpIDX = 0uz;
                for (int r = 0; r < 3; ++r) {
                    for (int c = 0; c < 3; ++c) {
                        static int drag_i = 0;
                        ImGui::PushItemWidth(81.0);
                        if (c != 0) { ImGui::SameLine(); }
                        ImGui::PushID(r * 3 + c);
                        ImGui::DragInt("", &drag_i, 0.1f, 0, 100, "%d");
                        ImGui::PopID();
                        ImGui::PopItemWidth();
                    }
                    for (int c = 0; c < 3; ++c) {
                        ImGui::PushID(r * 3 + c);


                        ImGui::BeginTable("MyTable", 3,
                                          ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV |
                                              ImGuiTableFlags_BordersH | ImGuiTableFlags_SizingFixedSame |
                                              ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_NoPadOuterX |
                                              ImGuiTableFlags_NoPadInnerX,
                                          ImVec2(0.0f, 0.0f));


                        for (int c = 0; c < 3; ++c) {
                            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 26.0f);
                        };


                        for (int tRow = 0; tRow < 3; tRow++) {
                            ImGui::TableNextRow(ImGuiTableRowFlags_None, 26);
                            for (int tCol = 0; tCol < 3; tCol++) {

                                ImGui::PushID(tRow * 3 + tCol);
                                bool tf = false;
                                ImGui::TableSetColumnIndex(tCol);
                                ImGui::Selectable("", tf);
                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                    // Set payload to carry the index of our item (could be anything)
                                    ImGui::SetDragDropPayload("DND_DEMO_CELL", &curShpIDX, sizeof(size_t));

                                    // Display preview (could be anything, e.g. when dragging an image we could decide
                                    // to display the filename and a small preview of the image, etc.) if (mode ==
                                    // Mode_Copy) { ImGui::Text("Copy %s", names[n]); } if (mode == Mode_Move) {
                                    // ImGui::Text("Move %s", names[n]); } if (mode == Mode_Swap) { ImGui::Text("Swap
                                    // %s", names[n]); }
                                    ImGui::EndDragDropSource();
                                }
                                if (ImGui::BeginDragDropTarget()) {
                                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_DEMO_CELL")) {
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


                        ImGui::PopID();
                        if (c < 2) { ImGui::SameLine(); }

                        ++curShpIDX;
                    }
                }


                ImGui::EndChild();
            }

            ImGui::SeparatorText("Main Thread Event Log");
            if (recent_events.empty()) { ImGui::TextDisabled("No completion messages received yet."); }
            else {
                for (auto it = recent_events.rbegin(); it != recent_events.rend(); ++it) {
                    ImGui::BulletText("%s", it->c_str());
                }
            }

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w,
                     clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
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
