#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_ctx.h"
#include "hud_layout.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include "vk.h"
#include "egl.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

static void draw_text(const HudText& run, ImVec2 origin) {
    if (run.text.empty())
        return;
    auto& draw = *ImGui::GetWindowDrawList();
    const auto pos = origin + run.pos;
    const auto* begin = run.text.data();
    const auto* end = begin + run.text.size();
    const auto outline = IM_COL32(0, 0, 0, 255);
    for (const auto offset : {ImVec2{-hud_outline, 0}, ImVec2{hud_outline, 0},
                              ImVec2{0, -hud_outline}, ImVec2{0, hud_outline}})
        draw.AddText(run.font, run.font_size, pos + offset, outline, begin, end);
    draw.AddText(run.font, run.font_size, pos, ImGui::ColorConvertFloat4ToU32(run.color), begin,
                 end);
}

static void draw_graph(const TextCell& cell, const ImRect& content, ImVec2 origin) {
    ImGui::SetCursorScreenPos(origin + content.Min);
    ImGui::PushID(&cell);
    const ImVec2 size{content.GetWidth(), std::min(content.GetHeight(), 50.0f)};
    if (size.x > 0.0f && size.y > 0.0f) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        if (ImGui::BeginChild("graph", size, false,
                              ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground)) {
            ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, {0, 0});
            ImPlot::PushStyleColor(ImPlotCol_PlotBg, {0, 0, 0, 0});
            ImPlot::PushStyleColor(ImPlotCol_FrameBg, {0, 0, 0, 0});
            if (ImPlot::BeginPlot("frametimes", size,
                                  ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs)) {
                ImPlot::SetupAxes(nullptr, nullptr,
                                  ImPlotAxisFlags_NoDecorations,
                                  ImPlotAxisFlags_NoDecorations);
                if (cell.graph_min && cell.graph_max) {
                    ImPlot::SetupAxisLimits(
                        ImAxis_Y1,
                        *cell.graph_min,
                        *cell.graph_max,
                        ImGuiCond_Always);
                }
                if (!cell.timestamps.empty() &&
                    cell.timestamps.size() == cell.data.size()) {
                    // Historical scalar metrics: use elapsed time as the X axis.
                    double x_min = cell.timestamps.front();
                double x_max = cell.timestamps.back();

                // Give a single-sample graph a visible X range.
                if (x_min == x_max) {
                    x_min -= 250.0;
                    x_max += 250.0;
                }

                ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max, ImGuiCond_Always);
                ImPlot::SetNextLineStyle({0, 1, 0, 1}, 2);
                ImPlot::PlotLine("metric", cell.timestamps.data(),
                                 cell.data.data(), static_cast<int>(cell.data.size()));
                    } else {
                        // Existing frametime graph behavior.
                        ImPlot::SetupAxisScale(
                            ImAxis_Y1, [](double v, void*) { return std::min(v, 49.9); },
                                               [](double v, void*) { return v; });
                        ImPlot::SetupAxesLimits(0, 200, 0, 50, ImGuiCond_Always);
                        ImPlot::SetNextLineStyle({0, 1, 0, 1}, 2);
                        ImPlot::PlotLine("frametime", cell.data.data(), cell.data.size());
                    }
                ImPlot::EndPlot();
            }
            ImPlot::PopStyleColor(2);
            ImPlot::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    ImGui::PopID();
}


static void draw_graph(const GraphCell& cell, const ImRect& bounds, ImVec2 origin) {
    ImGui::SetCursorScreenPos(origin + bounds.Min);
    ImGui::PushID(&cell);

    const ImVec2 size{bounds.GetWidth(), bounds.GetHeight()};

    if (size.x > 0.0f && size.y > 0.0f) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});

        if (ImGui::BeginChild(
            "metric_graph", size, false,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground)) {
            constexpr float label_width = 42.0f;
        const ImVec2 plot_size{
            std::max(0.0f, size.x - label_width),
            size.y
        };

        ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, {0, 0});
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, {0, 0, 0, 0});
        ImPlot::PushStyleColor(ImPlotCol_FrameBg, {0, 0, 0, 0});

        double display_y_min = 0.0;
        double display_y_max = 0.0;
        bool has_y_limits = false;

        if (ImPlot::BeginPlot(
            "metric_graph_plot", plot_size,
            ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs)) {
            ImPlot::SetupAxes(
                nullptr,
                nullptr,
                ImPlotAxisFlags_NoDecorations,
                ImPlotAxisFlags_NoLabel |
                ImPlotAxisFlags_NoTickMarks |
                ImPlotAxisFlags_NoTickLabels);

            if (!cell.data.empty()) {
                const auto [data_min_it, data_max_it] =
                std::minmax_element(cell.data.begin(), cell.data.end());

                const double data_min = *data_min_it;
                const double data_max = *data_max_it;
                const double range = data_max - data_min;

                const double padding =
                range > 0.0
                ? range * 0.05
                : std::max(std::abs(data_min) * 0.05, 1.0);

                const double y_min =
                cell.min.value_or(static_cast<float>(data_min - padding));
                const double y_max =
                cell.max.value_or(static_cast<float>(data_max + padding));

                display_y_min = y_min;
                display_y_max = y_max;
                has_y_limits = y_min < y_max;

                if (has_y_limits) {
                    ImPlot::SetupAxisLimits(
                        ImAxis_Y1, y_min, y_max, ImGuiCond_Always);
                }
            }

            if (!cell.timestamps.empty() &&
                cell.timestamps.size() == cell.data.size()) {
                double x_min = cell.timestamps.front();
            double x_max = cell.timestamps.back();

            if (x_min == x_max) {
                x_min -= 250.0;
                x_max += 250.0;
            }

            ImPlot::SetupAxisLimits(
                ImAxis_X1, x_min, x_max, ImGuiCond_Always);
            ImPlot::SetNextLineStyle({0, 1, 0, 1}, 2);
            ImPlot::PlotLine(
                "metric", cell.timestamps.data(), cell.data.data(),
                             static_cast<int>(cell.data.size()));
                } else if (!cell.data.empty()) {
                    // Vector-based metrics, such as FRAMETIMES.
                    ImPlot::SetupAxesLimits(0, 200, 0, 50, ImGuiCond_Always);
                    display_y_min = 0.0;
                    display_y_max = 50.0;
                    has_y_limits = true;

                    ImPlot::SetNextLineStyle({0, 1, 0, 1}, 2);
                    ImPlot::PlotLine(
                        "metric", cell.data.data(),
                                     static_cast<int>(cell.data.size()));
                }

                ImPlot::EndPlot();

                if (has_y_limits) {
                    char min_label[32];
                    char max_label[32];

                    std::snprintf(
                        min_label, sizeof(min_label), "%.3g", display_y_min);
                    std::snprintf(
                        max_label, sizeof(max_label), "%.3g", display_y_max);

                    auto* draw = ImGui::GetWindowDrawList();
                    const float font_size = ImGui::GetFontSize() * 0.8f;
                    const ImVec2 padding{4.0f, 2.0f};
                    const ImVec2 child_pos = ImGui::GetWindowPos();
                    const ImVec2 child_size = ImGui::GetWindowSize();

                    const ImVec2 max_text_size =
                    ImGui::GetFont()->CalcTextSizeA(
                        font_size, FLT_MAX, 0.0f, max_label);
                    const ImVec2 min_text_size =
                    ImGui::GetFont()->CalcTextSizeA(
                        font_size, FLT_MAX, 0.0f, min_label);

                    const ImU32 text_color = IM_COL32(255, 255, 255, 190);
                    const ImU32 outline_color = IM_COL32(0, 0, 0, 220);

                    const ImVec2 max_pos{
                        child_pos.x + child_size.x - max_text_size.x - padding.x,
                        child_pos.y + padding.y
                    };

                    const ImVec2 min_pos{
                        child_pos.x + child_size.x - min_text_size.x - padding.x,
                        child_pos.y + child_size.y - min_text_size.y - padding.y
                    };

                    for (const auto& [label, pos] :
                    {std::pair{max_label, max_pos},
                    std::pair{min_label, min_pos}}) {
                        draw->AddText(
                            ImGui::GetFont(), font_size,
                                      pos + ImVec2{1.0f, 1.0f}, outline_color, label);
                        draw->AddText(
                            ImGui::GetFont(), font_size, pos, text_color, label);
                    }
                }
            }

            ImPlot::PopStyleColor(2);
            ImPlot::PopStyleVar();
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
    }

    ImGui::PopID();
}

static void draw_content(const HudLayout& layout, ImVec2 origin) {
    auto& draw = *ImGui::GetWindowDrawList();

    for (const auto& row : layout.rows) {
        if (row.graph && row.graph->graph)
            draw_graph(*row.graph->graph, row.graph->bounds, origin);

        for (const auto& cell : row.cells) {
            if (const auto* progress = std::get_if<ProgressCell>(cell.cell)) {
                auto bar = cell.progress;
                bar.Translate(origin);
                const float range = progress->max_value - progress->min_value;
                const float fraction =
                    range == 0.0f
                        ? 0.0f
                        : std::clamp((progress->value - progress->min_value) / range, 0.0f, 1.0f);
                bar.Max.x = bar.Min.x + bar.GetWidth() * fraction;
                if (bar.GetWidth() > 0.0f && bar.GetHeight() > 0.0f)
                    draw.AddRectFilled(bar.Min, bar.Max,
                                       ImGui::ColorConvertFloat4ToU32(progress->vec));
            }
            for (const auto& run : cell.text)
                draw_text(run, origin);
            if (cell.nested)
                draw_content(*cell.nested, origin + cell.content.Min);
            if (const auto* text = std::get_if<TextCell>(cell.cell); text && !text->data.empty())
                draw_graph(*text, cell.content, origin);
            if (layout.debug) {
                draw.AddRect(cell.bounds.Min + origin, cell.bounds.Max + origin,
                             IM_COL32(255, 220, 0, 255));
                draw.AddRect(cell.content.Min + origin, cell.content.Max + origin,
                             IM_COL32(0, 220, 255, 255));
            }
        }
    }
}

static void draw_separators(const HudLayout& layout, ImVec2 origin) {
    auto& draw = *ImGui::GetWindowDrawList();
    for (const auto& row : layout.rows)
        for (const auto& cell : row.cells) {
            if (cell.nested)
                draw_separators(*cell.nested, origin + cell.content.Min);
            if (cell.separator_text)
                draw_text(*cell.separator_text, origin);
            if (!cell.separator)
                continue;
            const auto& line = *cell.separator;
            const auto& style = hud_cell_style(*cell.cell);
            draw.AddLine(origin + line.Min, origin + line.Max, IM_COL32(0, 0, 0, 255),
                         style.separator_thickness + hud_outline * 2.0f);
            draw.AddLine(origin + line.Min, origin + line.Max,
                         ImGui::ColorConvertFloat4ToU32(style.separator_vec),
                         style.separator_thickness);
        }
}

static void fill_background(ImRect bounds, const ImRect& clip, ImVec4 color) {
    bounds.ClipWith(clip);
    if (color.w > 0.0f && bounds.GetWidth() > 0.0f && bounds.GetHeight() > 0.0f) {
        const auto origin = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled(bounds.Min + origin, bounds.Max + origin,
                                                  ImGui::ColorConvertFloat4ToU32(color));
    }
}

static void draw_backgrounds(const HudLayout& layout, ImVec2 origin, ImVec4 inherited,
                             const ImRect& clip) {
    float previous_width = 0.0f;
    float previous_bottom = layout.rows.empty() ? 0.0f : layout.rows.front().bounds.Min.y;
    for (const auto& row : layout.rows) {
        auto bounds = row.bounds;
        if (!layout.trim_background)
            bounds.Max.x = layout.size.x;
        const float split = std::min(previous_width, bounds.Max.x);
        for (auto strip : {ImRect(0.0f, previous_bottom, split, bounds.Max.y),
                           ImRect(split, bounds.Min.y, bounds.Max.x, bounds.Max.y)}) {
            strip.Translate(origin);
            strip.ClipWith(clip);
            if (strip.GetWidth() <= 0.0f || strip.GetHeight() <= 0.0f)
                continue;
            float x = strip.Min.x;
            for (const auto& cell : row.cells) {
                auto bounds = cell.bounds;
                bounds.Translate(origin);
                bounds.Min.y = strip.Min.y;
                if (bounds.Min.x >= strip.Max.x)
                    break;
                fill_background({{x, strip.Min.y}, {bounds.Min.x, strip.Max.y}}, strip, inherited);
                fill_background({{bounds.Min.x, bounds.Max.y}, {bounds.Max.x, strip.Max.y}}, strip,
                                inherited);
                const auto& style = hud_cell_style(*cell.cell);
                auto color = style.background_color.empty() ? inherited : style.background_vec;
                if (const auto* progress = std::get_if<ProgressCell>(cell.cell))
                    color = progress->background_vec;
                if (cell.nested && cell.nested->size.x > 0.0f && cell.nested->size.y > 0.0f) {
                    const ImRect inner(origin + cell.content.Min,
                                       origin + cell.content.Min + cell.nested->size);
                    fill_background({bounds.Min, {bounds.Max.x, inner.Min.y}}, strip, color);
                    fill_background({{bounds.Min.x, inner.Max.y}, bounds.Max}, strip, color);
                    fill_background({{bounds.Min.x, inner.Min.y}, {inner.Min.x, inner.Max.y}}, strip,
                                    color);
                    fill_background({{inner.Max.x, inner.Min.y}, {bounds.Max.x, inner.Max.y}}, strip,
                                    color);
                    auto child_clip = inner;
                    child_clip.ClipWith(strip);
                    draw_backgrounds(*cell.nested, inner.Min, color, child_clip);
                } else
                    fill_background(bounds, strip, color);
                x = bounds.Max.x;
            }
            fill_background({{x, strip.Min.y}, strip.Max}, strip, inherited);
        }
        previous_width = bounds.Max.x;
        previous_bottom = bounds.Max.y;
    }
}

void ImGuiCtx::init_vk(VkCtx* vk_) {
    if (!vk)
        vk = std::make_shared<ImGuiVK>(vk_, this);
}

void ImGuiCtx::init_egl() {
    if (!egl)
        egl = std::make_shared<ImGuiEGL>(this);
}

bool ImGuiCtx::draw(uint32_t w, uint32_t h, Resolution& size, BufferSet* buf, Backend backend,
                    std::shared_ptr<HudConfig> hud, std::mutex& hud_m) {
    size = {w, h};

    std::unique_lock<std::mutex> global_lock(global_m);
    std::unique_lock<std::mutex> imgui_lock(m);
    std::unique_lock<std::mutex> vk_lock;
    ImGuiContext* imgui = nullptr;
    ImPlotContext* implot = nullptr;
    Font* fonts = nullptr;
    if (backend == Backend::VULKAN) {
        if (!vk) SPDLOG_ERROR("vulkan backend is not initalized");
        vk_lock = std::unique_lock<std::mutex>(vk->mutex());
        imgui = vk->imgui;
        implot = vk->implot;
        fonts = vk->fonts.get();
    } else if (backend == Backend::EGL) {
        if (!egl) SPDLOG_ERROR("egl backend is not initalized");
        imgui = egl->imgui;
        implot = egl->implot;
        fonts = egl->fonts.get();
    }

    std::vector<HudWindow> windows;
    {
        std::lock_guard lock(hud_m);
        windows = hud->windows;
    }
    ImGui::SetCurrentContext(imgui);
    ImPlot::SetCurrentContext(implot);
    ImVec2 font_extent{};
    for (auto& window : windows)
        font_extent = ImMax(font_extent, prepare_hud_fonts(window.table, *fonts));

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {float(w), float(h)};

    ImGui::NewFrame();
    if (backend == Backend::EGL)
        ImGui_ImplOpenGL3_NewFrame();

    float max_x = 0.0f;
    float max_y = 0.0f;
    for (std::size_t i = 0; i < windows.size(); i++) {
        HudWindow& window = windows[i];
        const auto layout = layout_hud(window.table, *fonts);
        const uint32_t window_w = std::ceil(layout.size.x);
        const uint32_t window_h = std::ceil(layout.size.y);
        const std::string name = "HUD##" + std::to_string(i);
        ImGui::SetNextWindowSize({float(window_w), float(window_h)});
        ImGui::SetNextWindowPos(window.position, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::Begin(name.c_str(), nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
        const auto origin = ImGui::GetWindowPos();
        auto background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        background.w = window.background ? 0.5f : 0.0f;
        draw_backgrounds(layout, {}, background, ImRect({}, layout.size));
        draw_content(layout, origin);
        draw_separators(layout, origin);
        ImGui::End();
        ImGui::PopStyleVar(2);

        max_x = std::max(max_x, window.position.x + (float)window_w);
        max_y = std::max(max_y, window.position.y + (float)window_h);
    }

    // Leave room for small layout changes without reallocating the buffers.
    if (max_x > w)
        size.w = static_cast<uint32_t>(std::ceil(max_x + font_extent.x * 4.0f));
    if (max_y > h)
        size.h = static_cast<uint32_t>(std::ceil(max_y + font_extent.y * 4.0f));

    ImGui::Render();

    if (size != Resolution{w, h})
        return false;

    if (backend == Backend::VULKAN)
        return vk->record_cmd(*buf, w, h);

    if (backend == Backend::EGL)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return true;
}

ImGuiCtx::~ImGuiCtx() {
    vk.reset();
    egl.reset();
}
