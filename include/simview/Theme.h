#pragma once

#include "Types.h"

namespace sv {

struct Theme {
    const char *name = "custom";

    float ink[3] = {0.043f, 0.047f, 0.059f};
    float panel[3] = {0.086f, 0.094f, 0.114f};
    float sunken[3] = {0.063f, 0.069f, 0.086f};
    float field[3] = {0.137f, 0.149f, 0.180f};
    float edge[3] = {0.196f, 0.212f, 0.255f};
    float text[3] = {0.867f, 0.886f, 0.925f};
    float text_dim[3] = {0.435f, 0.463f, 0.522f};
    float accent[3] = {0.310f, 0.573f, 0.855f};
    float series[8][3] = {{0.310f, 0.573f, 0.855f}, {0.878f, 0.659f, 0.235f},
                          {0.247f, 0.749f, 0.659f}, {0.910f, 0.412f, 0.490f},
                          {0.608f, 0.494f, 0.871f}, {0.624f, 0.788f, 0.290f},
                          {0.322f, 0.780f, 0.910f}, {0.851f, 0.502f, 0.353f}};

    float panel_rounding = 6.0f;
    float control_rounding = 4.0f;
    float padding = 1.0f;
    float gap = 1.0f;
    float window_border = 1.0f;
    float control_border = 0.0f;
    float panel_alpha = 0.94f;
    float title_align = 0.0f;

    float font_size = 16.0f;
    bool mono_ui = false;

    float plot_border = 0.0f;
    float grid_alpha = 0.18f;
    float tick_len = 6.0f;
    float line_weight = 1.75f;
    float marker_size = 3.5f;
    float fill_alpha = 0.60f;
};

namespace themes {

inline constexpr Theme Midnight{.name = "midnight"};

inline constexpr Theme Paper{.name = "paper",
                             .ink = {0.996f, 0.996f, 0.992f},
                             .panel = {0.965f, 0.965f, 0.957f},
                             .sunken = {0.996f, 0.996f, 0.992f},
                             .field = {0.996f, 0.996f, 0.992f},
                             .edge = {0.667f, 0.671f, 0.655f},
                             .text = {0.114f, 0.118f, 0.129f},
                             .text_dim = {0.404f, 0.412f, 0.424f},
                             .accent = {0.086f, 0.353f, 0.643f},
                             .series = {{0.086f, 0.353f, 0.643f},
                                        {0.639f, 0.404f, 0.031f},
                                        {0.024f, 0.443f, 0.388f},
                                        {0.702f, 0.153f, 0.263f},
                                        {0.396f, 0.271f, 0.659f},
                                        {0.325f, 0.463f, 0.086f},
                                        {0.075f, 0.463f, 0.584f},
                                        {0.616f, 0.271f, 0.137f}},
                             .panel_rounding = 2.0f,
                             .control_rounding = 2.0f,
                             .padding = 1.15f,
                             .gap = 1.15f,
                             .control_border = 1.0f,
                             .panel_alpha = 1.0f,
                             .plot_border = 1.0f,
                             .grid_alpha = 0.45f,
                             .tick_len = 8.0f,
                             .line_weight = 1.4f,
                             .marker_size = 3.0f,
                             .fill_alpha = 0.35f};

inline constexpr Theme Contrast{.name = "contrast",
                                .ink = {0.0f, 0.0f, 0.0f},
                                .panel = {0.043f, 0.043f, 0.051f},
                                .sunken = {0.0f, 0.0f, 0.0f},
                                .field = {0.145f, 0.149f, 0.165f},
                                .edge = {0.549f, 0.561f, 0.588f},
                                .text = {1.0f, 1.0f, 1.0f},
                                .text_dim = {0.769f, 0.780f, 0.800f},
                                .accent = {0.400f, 0.729f, 1.0f},
                                .series = {{0.400f, 0.729f, 1.0f},
                                           {1.0f, 0.812f, 0.298f},
                                           {0.318f, 0.949f, 0.788f},
                                           {1.0f, 0.522f, 0.612f},
                                           {0.780f, 0.663f, 1.0f},
                                           {0.769f, 0.949f, 0.365f},
                                           {0.475f, 0.914f, 1.0f},
                                           {1.0f, 0.663f, 0.475f}},
                                .panel_rounding = 0.0f,
                                .control_rounding = 0.0f,
                                .padding = 1.2f,
                                .gap = 1.1f,
                                .window_border = 2.0f,
                                .control_border = 2.0f,
                                .panel_alpha = 1.0f,
                                .font_size = 18.0f,
                                .plot_border = 2.0f,
                                .grid_alpha = 0.40f,
                                .tick_len = 9.0f,
                                .line_weight = 2.75f,
                                .marker_size = 5.0f,
                                .fill_alpha = 0.45f};

inline constexpr Theme Terminal{.name = "terminal",
                                .ink = {0.020f, 0.031f, 0.027f},
                                .panel = {0.043f, 0.063f, 0.055f},
                                .sunken = {0.027f, 0.043f, 0.039f},
                                .field = {0.078f, 0.110f, 0.098f},
                                .edge = {0.157f, 0.239f, 0.204f},
                                .text = {0.800f, 0.878f, 0.831f},
                                .text_dim = {0.435f, 0.545f, 0.486f},
                                .accent = {0.322f, 0.855f, 0.545f},
                                .series = {{0.322f, 0.855f, 0.545f},
                                           {0.937f, 0.788f, 0.310f},
                                           {0.400f, 0.780f, 0.847f},
                                           {0.918f, 0.478f, 0.427f},
                                           {0.749f, 0.647f, 0.918f},
                                           {0.694f, 0.855f, 0.361f},
                                           {0.400f, 0.898f, 0.780f},
                                           {0.902f, 0.616f, 0.400f}},
                                .panel_rounding = 0.0f,
                                .control_rounding = 0.0f,
                                .padding = 0.8f,
                                .gap = 0.7f,
                                .window_border = 1.0f,
                                .control_border = 1.0f,
                                .panel_alpha = 1.0f,
                                .font_size = 14.0f,
                                .mono_ui = true,
                                .plot_border = 1.0f,
                                .grid_alpha = 0.30f,
                                .tick_len = 4.0f,
                                .line_weight = 1.25f,
                                .marker_size = 2.5f,
                                .fill_alpha = 0.28f};

}

}
