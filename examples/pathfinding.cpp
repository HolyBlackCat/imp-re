#include "graph/pathfinding.h"
const ivec2 screen_size = ivec2(480, 270);
const std::string_view window_name = "Pathfinding";

Interface::Window window(std::string(window_name), screen_size * 2, Interface::windowed, adjust_(Interface::WindowSettings{}, .min_size = screen_size));
static Graphics::DummyVertexArray dummy_vao = nullptr;

const Graphics::ShaderConfig shader_config = Graphics::ShaderConfig::Core();
Interface::ImGuiController gui_controller(Poly::derived<Interface::ImGuiController::GraphicsBackend_Modern>, adjust_(Interface::ImGuiController::Config{}, .shader_header = shader_config.common_header, .store_state_in_file = {}));

GameUtils::AdaptiveViewport adaptive_viewport(shader_config, screen_size);
Render r = adjust_(Render(0x2000, shader_config), .SetMatrix(adaptive_viewport.GetDetails().MatrixCentered()));

Input::Mouse mouse;

Random::DefaultGenerator random_generator = Random::MakeGeneratorFromRandomDevice();
Random::DefaultInterfaces<Random::DefaultGenerator> ra(random_generator);

struct Application : Program::DefaultBasicState
{
    GameUtils::FpsCounter fps_counter;

    void Resize()
    {
        adaptive_viewport.Update();
        mouse.SetMatrix(adaptive_viewport.GetDetails().MouseMatrixCentered());
    }

    Metronome metronome = Metronome(60);

    Metronome *GetTickMetronome() override
    {
        return &metronome;
    }

    int GetFpsCap() override
    {
        return 60 * NeedFpsCap();
    }

    void EndFrame() override
    {
        fps_counter.Update();
        window.SetTitle(STR((window_name), " TPS:", (fps_counter.Tps()), " FPS:", (fps_counter.Fps()), " SOUNDS:"));
    }

    void Tick() override
    {
        // window.ProcessEvents();
        window.ProcessEvents({gui_controller.EventHook()});

        if (window.ExitRequested())
            Program::Exit();
        if (window.Resized())
        {
            Resize();
            Graphics::Viewport(window.Size());
        }

        gui_controller.PreTick();

        DemoTick();
    }

    void Render() override
    {
        gui_controller.PreRender();
        adaptive_viewport.BeginFrame();
        adaptive_viewport.FinishFrame();
        gui_controller.PostRender();
        Graphics::CheckErrors();

        window.SwapBuffers();
    }


    void Init()
    {
        // Initialize ImGui.
        ImGui::StyleColorsDark();
        Graphics::Blending::Enable();
        Graphics::Blending::FuncNormalPre();
    }


    int num_iters = 5;

    Array2D<char/*bool*/> grid{ivec2(30,20)};
    ivec2 start = ivec2(5,16);
    ivec2 goal = ivec2(25,4);

    enum class MouseMode
    {
        none,
        wall,
        empty,
        start,
        goal,
    };
    MouseMode mouse_mode = MouseMode::none;

    void DemoTick()
    {
        ImGui::SetNextWindowPos(fvec2{});
        ImGui::SetNextWindowSize(window.Size());
        ImGui::Begin( "Pathfinding demo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize );
        FINALLY{ImGui::End();};

        ImGui::SetNextItemWidth( 60 );
        ImGui::DragInt("Num iterations", &num_iters, 0.5f, 0, 10000);

        Graph::Pathfinding::Pathfinder_4Way<ivec2> pf(start, goal, 1000);
        // Graph::Pathfinding::Pathfinder<ivec2, int, std::pair<int, int>> pf(start, goal, 1000);
        Graph::Pathfinding::Result res{};

        phmap::flat_hash_set<ivec2> pf_true_visited;
        for (int i = 0; i < num_iters; i++)
        {
            if (!pf.GetRemainingNodesHeap().empty())
                pf_true_visited.insert(pf.GetRemainingNodesHeap().front().coord);
            res = pf.Step(Graph::Pathfinding::Flags::can_continue_after_goal, [&](ivec2 pos) -> bool {return grid.pos_in_range(pos) ? grid.safe_nonthrowing_at(pos) : true;});
            // res = pf.Step(Graph::Pathfinding::Flags::can_continue_after_goal,
            //     [&](ivec2 pos, auto func)
            //     {
            //         for (int i = 0; i < 4; i++)
            //         {
            //             ivec2 next_pos = pos + ivec2::dir4(i);
            //             if (grid.pos_in_range(next_pos) && !grid.safe_nonthrowing_at(pos))
            //                 func(next_pos, 1);
            //         }
            //     },
            //     [&](int cost, ivec2 pos) -> std::pair<int, int>
            //     {
            //         (void)cost;
            //         return {(pos - pf.GetGoal()).len_sq(), 0};
            //     }
            //     );
            // if (res != Graph::Pathfinding::Result::incomplete)
            //     break;
        }

        phmap::flat_hash_map<ivec2, std::vector<decltype(pf)::estimated_cost_t>> pf_heap;
        pf_heap.reserve(pf.GetRemainingNodesHeap().size());
        for (const auto &elem : pf.GetRemainingNodesHeap())
            pf_heap[elem.coord].push_back(elem.estimated_total_cost);

        phmap::flat_hash_map<ivec2, int> pf_path;
        if (res == Graph::Pathfinding::Result::success)
        {
            pf_path.reserve(num_iters + 1);
            pf.DumpPathBackwards([&](ivec2 pos){pf_path.try_emplace(pos, pf_path.size());});
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("Result:");
        ImGui::SameLine();
        ImGui::TextUnformatted(res == Graph::Pathfinding::Result::incomplete ? "incomplete" : res == Graph::Pathfinding::Result::success ? "success" : "fail");
        ImGui::SameLine();
        ImGui::TextUnformatted(FMT("Remaining nodes in heap: {}", pf.GetRemainingNodesHeap().size()).c_str());

        ImGui::Separator();

        ivec2 cell_size((ivec2(ImGui::GetContentRegionAvail()) / ivec2(grid.size())).min());

        ivec2 base_pos = ImGui::GetCursorScreenPos();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,0,0,1));
        FINALLY{ImGui::PopStyleColor();};

        for (ivec2 pos : vector_range(ivec2(grid.size())))
        {
            ivec2 cell_pos = base_pos + pos * cell_size;
            ImGui::SetCursorScreenPos(cell_pos);

            bool is_start = pos == start;
            bool is_goal = pos == goal;
            bool is_path = pf_path.contains(pos);
            bool is_heap = pf_heap.contains(pos);
            bool is_visited = pf.GetNodeInfoMap().contains(pos);
            bool is_wall = grid.safe_nonthrowing_at(pos);

            ImVec4 cell_color =
                is_start ? ImVec4(0,1,0,1) :
                is_goal ? ImVec4(1,0,0,1) :
                is_path ? ImVec4(0,0.7f,0.9f,1) :
                is_heap ? (pf_true_visited.contains(pos) ? ImVec4(0.6f,0.2f,0.2f,1) : ImVec4(0.6f,0.6f,0.6f,1)) :
                is_visited ? ImVec4(0.8f,0.8f,0.8f,1) :
                is_wall ? ImVec4(0.1f,0.1f,0.1f,1) : ImVec4(1,1,1,1);

            ImGui::PushStyleColor(ImGuiCol_Button, cell_color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cell_color);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, cell_color);
            FINALLY{ImGui::PopStyleColor(3);};

            ImGui::Button(FMT("##{}x{}", pos.x, pos.y).c_str(), cell_size - 1);

            if (ImGui::IsItemClicked())
            {
                if (is_start)
                    mouse_mode = MouseMode::start;
                else if (is_goal)
                    mouse_mode = MouseMode::goal;
                else if (is_wall)
                    mouse_mode = MouseMode::empty;
                else
                    mouse_mode = MouseMode::wall;
            }
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                {
                    switch (mouse_mode)
                    {
                      case MouseMode::none:
                        // Nothng.
                        break;
                      case MouseMode::start:
                        start = pos;
                        break;
                      case MouseMode::goal:
                        goal = pos;
                        break;
                      case MouseMode::wall:
                        grid.safe_nonthrowing_at(pos) = 1;
                        break;
                      case MouseMode::empty:
                        grid.safe_nonthrowing_at(pos) = 0;
                        break;
                    }
                }
            }
            else
            {
                mouse_mode = MouseMode::none;
            }
        }
    }
};

IMP_MAIN(,)
{
    Application app;
    app.Init();
    app.Resize();
    app.RunMainLoop();
    return 0;
}
