#include <filesystem>
#include <iostream>
#include <vector>

#include <GL/glew.h>
#include <imgui.h>
#include <portable-file-dialogs.h>

#include <btrc/builtin/register.h>
#include <btrc/core/object_dag.h>
#include <btrc/core/scene.h>
#include <btrc/factory/context.h>
#include <btrc/factory/node/parser.h>
#include <btrc/factory/scene.h>
#include <btrc/utils/cuda/context.h>
#include <btrc/utils/optix/context.h>
#include <btrc/utils/exception.h>
#include <btrc/utils/file.h>

#include "camera_controller.h"
#include "reporter.h"
#include "window.h"

using namespace btrc;

struct BtrcScene
{
    Box<ScopedPropertyPool> property_pool;
    Box<factory::Context>   object_context;

    int width  = 0;
    int height = 0;

    RC<factory::Node> root;

    RC<Scene>    scene;
    RC<Camera>   camera;
    RC<Reporter> reporter;
    RC<Renderer> renderer;
};

struct Rect2D
{
    Vec2f lower;
    Vec2f upper;
};

BtrcScene initialize_btrc_scene(const std::string &filename, optix::Context &optix_context)
{
    BtrcScene result;

    std::cout << "create btrc context" << std::endl;

    result.property_pool = newBox<ScopedPropertyPool>();

    result.object_context = newBox<factory::Context>(optix_context);
    builtin::register_builtin_creators(*result.object_context);

    const auto scene_dir = std::filesystem::path(filename).parent_path();
    result.object_context->add_path_mapping("scene_directory", scene_dir.string());

    std::cout << "parse scene" << std::endl;

    factory::JSONParser parser;
    parser.set_source(read_txt_file(filename));
    parser.add_include_directory(scene_dir);
    parser.parse();
    result.root = parser.get_result();

    std::cout << "create scene" << std::endl;

    result.scene = create_scene(result.root->child_node("scene"), *result.object_context);

    std::cout << "create camera" << std::endl;

    result.width = result.root->parse_child<int>("width");
    result.height = result.root->parse_child<int>("height");

    result.camera = result.object_context->create<Camera>(result.root->child_node("camera"));
    result.camera->set_w_over_h(static_cast<float>(result.width) / result.height);

    std::cout << "create renderer" << std::endl;

    result.renderer = result.object_context->create<Renderer>(result.root->child_node("renderer"));
    result.renderer->set_camera(result.camera);
    result.renderer->set_film(result.width, result.height);
    result.renderer->set_scene(result.scene);

    std::cout << "commit objects" << std::endl;

    ObjectDAG dag(result.renderer);

    result.scene->precommit();
    dag.commit();
    result.scene->postcommit();

    std::cout << "compile kernel" << std::endl;

    if(dag.need_recompile())
    {
        result.renderer->recompile(false);
        dag.clear_recompile_flag();
    }

    std::cout << "update properties" << std::endl;

    dag.update_properties();

    return result;
}

Rect2D compute_preview_image_rect(const Vec2f &window_size, const Vec2f &scene_size)
{
    if(window_size.x < 50 || window_size.y < 50)
        return Rect2D{ { 0, 0 }, { 0, 0 } };

    const float avail_width  = (std::max)(window_size.x * 0.9f, window_size.x - 50);
    const float avail_height = (std::max)(window_size.y * 0.9f, window_size.y - 50);

    const float window_ratio = avail_width / avail_height;
    const float image_ratio = static_cast<float>(scene_size.x) / scene_size.y;

    float display_size_x, display_size_y;
    if(window_ratio > image_ratio)
    {
        display_size_y = avail_height;
        display_size_x = image_ratio * display_size_y;
    }
    else
    {
        display_size_x = avail_width;
        display_size_y = display_size_x / image_ratio;
    }

    Rect2D result;
    result.lower = {
            0.5f * (window_size.x - display_size_x),
            0.5f * (window_size.y - display_size_y)
    };
    result.upper = result.lower + Vec2f(display_size_x, display_size_y);
    return result;
}

void display_image(GLuint tex_handle, const Rect2D &rect)
{
    if(rect.lower.x == rect.upper.x)
        return;
    const auto im_tex = reinterpret_cast<ImTextureID>(static_cast<size_t>(tex_handle));
    ImGui::SetCursorPos({ rect.lower.x, rect.lower.y });
    ImGui::Image(im_tex, ImVec2({ rect.upper.x - rect.lower.x, rect.upper.y - rect.lower.y }));
}

void run(const std::string &config_filename)
{
    cuda::Context cuda_context(0);
    optix::Context optix_context(nullptr);

    auto scene = initialize_btrc_scene(config_filename, optix_context);

    Window window("BtrcGUI", 1024, 768);

    auto reporter = newRC<GUIPreviewer>();
    reporter->set_preview_interval(0);
    reporter->set_fast_preview(true);

    scene.renderer->set_reporter(reporter);
    scene.renderer->render_async();

    GLuint tex_handle = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex_handle);
    if(!tex_handle)
        throw std::runtime_error("failed to create gl texture");
    glTextureStorage2D(tex_handle, 1, GL_RGBA32F, scene.width, scene.height);

    CameraController camera_controller(std::dynamic_pointer_cast<builtin::PinholeCamera>(scene.camera));

    constexpr int MIN_UPDATED_IMAGE_COUNT = 2;
    int updated_image_count = 0;

    auto update_image = [&](const Image<Vec4f> &image)
    {
        if(image && ++updated_image_count >= MIN_UPDATED_IMAGE_COUNT)
        {
            glTextureSubImage2D(
                tex_handle, 0, 0, 0, scene.width, scene.height,
                GL_RGBA, GL_FLOAT, image.data());
        }
    };

    while(!window.should_close())
    {
        window.begin_frame();

        if(ImGui::IsKeyDown(ImGuiKey_Escape))
            window.set_close(true);

        if(scene.renderer->is_rendering())
            reporter->access_dirty_image(update_image);
        else
        {
            updated_image_count = MIN_UPDATED_IMAGE_COUNT;
            reporter->access_image(update_image);
        }

        const auto imgui_viewport = ImGui::GetMainViewport();
        const Rect2D display_rect = compute_preview_image_rect(
            {
                imgui_viewport->WorkSize.x,
                imgui_viewport->WorkSize.y
            },
            {
                static_cast<float>(scene.width),
                static_cast<float>(scene.height)
            });

        if(updated_image_count >= MIN_UPDATED_IMAGE_COUNT)
        {
            const auto mouse_pos = ImGui::GetMousePos();
            const auto relative_mouse_pos = Vec2f(
                (mouse_pos.x - display_rect.lower.x) / (display_rect.upper.x - display_rect.lower.x),
                (mouse_pos.y - display_rect.lower.y) / (display_rect.upper.y - display_rect.lower.y));

            const bool update = camera_controller.update(CameraController::InputParams{
                .cursor_pos = relative_mouse_pos,
                .wheel_offset = ImGui::GetIO().MouseWheel,
                .button_down = {
                    ImGui::IsMouseDown(ImGuiMouseButton_Left),
                    ImGui::IsMouseDown(ImGuiMouseButton_Middle),
                    ImGui::IsMouseDown(ImGuiMouseButton_Right)
                }
            });

            if(update)
            {
                if(scene.renderer->is_waitable())
                    scene.renderer->stop_async();

                ObjectDAG dag(scene.renderer);
                const bool need_recompile = dag.need_recompile();

                if(need_recompile)
                    scene.scene->clear_device_data();

                scene.scene->precommit();
                dag.commit();
                scene.scene->postcommit();

                if(need_recompile)
                {
                    scene.renderer->recompile(false);
                    dag.clear_recompile_flag();
                }

                dag.update_properties();

                reporter->progress(0);
                reporter->set_preview_interval(0);
                reporter->set_fast_preview(true);

                updated_image_count = 0;
                scene.renderer->render_async();
            }
            else if(updated_image_count == MIN_UPDATED_IMAGE_COUNT)
            {
                reporter->set_preview_interval(100);
                reporter->set_fast_preview(false);
            }
        }

        if(reporter->get_percentage() > 5)
            reporter->set_preview_interval(1000);

        /*if(!scene.renderer->is_rendering() && scene.renderer->is_waitable())
        {
            auto result = scene.renderer->wait_async();

            const auto value_filename = scene.root->parse_child_or<std::string>("value_filename", "output.exr");
            std::cout << "write value to " << value_filename << std::endl;
            result.value.save(value_filename);

            if(result.albedo)
            {
                const auto albedo_filename = scene.root->parse_child_or<std::string>("albedo_filename", "output_albedo.png");
                std::cout << "write albedo to " << albedo_filename << std::endl;
                result.albedo.save(albedo_filename);
            }

            if(result.normal)
            {
                const auto normal_filename = scene.root->parse_child_or<std::string>("normal_filename", "output_normal.png");
                std::cout << "write normal to " << normal_filename << std::endl;
                result.normal.save(normal_filename);
            }
        }*/

        {
            auto viewport = ImGui::GetMainViewport();

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

            ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);

            constexpr ImGuiWindowFlags window_flags =
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus;
            ImGui::Begin("display", nullptr, window_flags);
            ImGui::PopStyleVar(3);
            {
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.1f, 0.8f, 0.1f, 1));
                ImGui::ProgressBar(reporter->get_percentage() / 100.0f);
                ImGui::PopStyleColor();
                display_image(tex_handle, display_rect);
            }
            ImGui::End();
        }

        window.end_frame();
    }

    glDeleteTextures(1, &tex_handle);

    if(scene.renderer->is_waitable())
        scene.renderer->stop_async();
}

int main(int argc, char *argv[])
{
    std::cout << ">>> Btrc Renderer <<<" << std::endl;

    if(argc > 2)
    {
        std::cout << "usage: BtrcGUI (optional config.json)" << std::endl;
        return 0;
    }

    std::string filename;
    if(argc == 2)
        filename = argv[1];
    else
    {
        auto open = pfd::open_file(
            "Select Scene Configuration FIle",
            std::filesystem::current_path().string(),
            {".json" });
        if(open.result().size() != 1)
            return 0;
        filename = open.result()[0];
    }

    try
    {
        run(filename);
    }
    catch(const std::exception &err)
    {
        std::vector<std::string> err_msgs;
        extract_hierarchy_exceptions(err, std::back_inserter(err_msgs));
        for(auto &s : err_msgs)
            std::cerr << s << std::endl;
        return -1;
    }
}
