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
    float rounding = 6.0f;
    float density = 1.0f;
    float font_size = 16.0f;
    float border = 1.0f;
    float panel_alpha = 0.94f;
};

namespace themes {

inline constexpr Theme Midnight{.name = "midnight"};

inline constexpr Theme Paper{.name = "paper",
                             .ink = {0.996f, 0.996f, 0.992f},
                             .panel = {0.949f, 0.949f, 0.941f},
                             .sunken = {0.906f, 0.906f, 0.894f},
                             .field = {0.855f, 0.859f, 0.847f},
                             .edge = {0.749f, 0.753f, 0.741f},
                             .text = {0.129f, 0.137f, 0.149f},
                             .text_dim = {0.435f, 0.447f, 0.459f},
                             .accent = {0.086f, 0.396f, 0.706f},
                             .series = {{0.086f, 0.396f, 0.706f},
                                        {0.686f, 0.443f, 0.047f},
                                        {0.043f, 0.494f, 0.435f},
                                        {0.741f, 0.180f, 0.290f},
                                        {0.435f, 0.310f, 0.706f},
                                        {0.365f, 0.510f, 0.106f},
                                        {0.106f, 0.514f, 0.639f},
                                        {0.667f, 0.310f, 0.169f}},
                             .panel_alpha = 0.97f};

inline constexpr Theme Contrast{.name = "contrast",
                                .ink = {0.0f, 0.0f, 0.0f},
                                .panel = {0.055f, 0.055f, 0.063f},
                                .sunken = {0.020f, 0.020f, 0.024f},
                                .field = {0.157f, 0.161f, 0.176f},
                                .edge = {0.443f, 0.455f, 0.482f},
                                .text = {1.0f, 1.0f, 1.0f},
                                .text_dim = {0.694f, 0.706f, 0.729f},
                                .accent = {0.353f, 0.706f, 1.0f},
                                .series = {{0.353f, 0.706f, 1.0f},
                                           {1.0f, 0.800f, 0.290f},
                                           {0.290f, 0.937f, 0.784f},
                                           {1.0f, 0.510f, 0.596f},
                                           {0.757f, 0.639f, 1.0f},
                                           {0.749f, 0.937f, 0.353f},
                                           {0.451f, 0.902f, 1.0f},
                                           {1.0f, 0.643f, 0.451f}},
                                .rounding = 3.0f,
                                .font_size = 17.0f,
                                .panel_alpha = 1.0f};

}

}
