#pragma once

#include "Types.h"
#include "sync/Sync.h"

#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace sv {

namespace impl {

struct App;

enum class WidgetKind : std::int32_t {
    Text,
    Separator,
    Help,
    Button,
    IconButton,
    IconToggle,
    Slider,
    SliderInt,
    SliderVec3,
    Drag,
    InputFloat,
    InputInt,
    Choice,
    Color,
    Checkbox,
    Value,
    Progress,
    Transport,
    GroupBegin,
    GroupEnd
};

enum class Group : std::int32_t { Section, Tabs, Tab, Row, Enabled };

struct Panel {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

struct WidgetDesc {
    const char *label = nullptr;
    WidgetKind kind = WidgetKind::Text;
    Group group = Group::Section;
    Icon icon = Icon::Gear;
    void *target = nullptr;
    float min = 0.0f, max = 1.0f;
    float speed = 0.0f;
    Scale scale = Scale::Linear;
    const char *fmt = "%.3g";
    const char *const *options = nullptr;
    int option_count = 0;
    double (*value)(void *) = nullptr;
    void (*on_click)(void *) = nullptr;
    void *user = nullptr;
    void (*free)(void *) = nullptr;
};

Panel panel_create(App *, const char *title);
bool panel_widget(Panel, const WidgetDesc &);

}

class Panel {
  public:
    Panel() = default;
    explicit Panel(impl::Panel p) : p_(p) {}

    explicit operator bool() const { return bool(p_); }
    impl::Panel Raw() const { return p_; }

    Panel &Text(const char *label) {
        return add(impl::WidgetKind::Text, label, nullptr);
    }

    Panel &Separator() { return add(impl::WidgetKind::Separator, "", nullptr); }

    Panel &Separator(const char *label) {
        return add(impl::WidgetKind::Separator, label, nullptr);
    }

    Panel &Help(const char *text) {
        return add(impl::WidgetKind::Help, text, nullptr);
    }

    Panel &Slider(const char *label, float &value, float min, float max,
                  Scale scale = Scale::Linear) {
        impl::panel_widget(p_,
                           impl::WidgetDesc{.label = label,
                                            .kind = impl::WidgetKind::Slider,
                                            .target = &value,
                                            .min = min,
                                            .max = max,
                                            .scale = scale});
        return *this;
    }

    Panel &Slider(const char *label, int &value, int min, int max) {
        impl::panel_widget(p_,
                           impl::WidgetDesc{.label = label,
                                            .kind = impl::WidgetKind::SliderInt,
                                            .target = &value,
                                            .min = float(min),
                                            .max = float(max)});
        return *this;
    }

    Panel &Slider(const char *label, float (&value)[3], float min, float max) {
        impl::panel_widget(
            p_, impl::WidgetDesc{.label = label,
                                 .kind = impl::WidgetKind::SliderVec3,
                                 .target = value,
                                 .min = min,
                                 .max = max});
        return *this;
    }

    Panel &Drag(const char *label, float &value, float speed, float min,
                float max) {
        impl::panel_widget(p_, impl::WidgetDesc{.label = label,
                                                .kind = impl::WidgetKind::Drag,
                                                .target = &value,
                                                .min = min,
                                                .max = max,
                                                .speed = speed});
        return *this;
    }

    Panel &Input(const char *label, float &value) {
        return add(impl::WidgetKind::InputFloat, label, &value);
    }

    Panel &Input(const char *label, int &value) {
        return add(impl::WidgetKind::InputInt, label, &value);
    }

    Panel &Choice(const char *label, int &value,
                  std::initializer_list<const char *> options) {
        impl::panel_widget(
            p_, impl::WidgetDesc{.label = label,
                                 .kind = impl::WidgetKind::Choice,
                                 .target = &value,
                                 .options = options.begin(),
                                 .option_count = int(options.size())});
        return *this;
    }

    Panel &Color(const char *label, float (&rgb)[3]) {
        return add(impl::WidgetKind::Color, label, rgb);
    }

    Panel &Checkbox(const char *label, bool &value) {
        return add(impl::WidgetKind::Checkbox, label, &value);
    }

    Panel &Value(const char *label, float &value, const char *fmt = "%.3g") {
        impl::panel_widget(p_, impl::WidgetDesc{.label = label,
                                                .kind = impl::WidgetKind::Value,
                                                .target = &value,
                                                .fmt = fmt});
        return *this;
    }

    template <std::invocable F>
    Panel &Value(const char *label, F pull, const char *fmt = "%.3g") {
        impl::panel_widget(
            p_,
            impl::WidgetDesc{
                .label = label,
                .kind = impl::WidgetKind::Value,
                .fmt = fmt,
                .value =
                    [](void *u) { return double((*static_cast<F *>(u))()); },
                .user = new F(std::move(pull)),
                .free = [](void *u) { delete static_cast<F *>(u); }});
        return *this;
    }

    template <std::invocable F>
    Panel &Progress(const char *label, F pull, float min = 0.0f,
                    float max = 1.0f) {
        impl::panel_widget(
            p_,
            impl::WidgetDesc{
                .label = label,
                .kind = impl::WidgetKind::Progress,
                .min = min,
                .max = max,
                .value =
                    [](void *u) { return double((*static_cast<F *>(u))()); },
                .user = new F(std::move(pull)),
                .free = [](void *u) { delete static_cast<F *>(u); }});
        return *this;
    }

    Panel &Transport(Executor &sim) {
        return add(impl::WidgetKind::Transport, "", sim.Raw().p);
    }

    template <class F> Panel &Button(const char *label, F on_click) {
        impl::panel_widget(
            p_, impl::WidgetDesc{
                    .label = label,
                    .kind = impl::WidgetKind::Button,
                    .on_click = [](void *u) { (*static_cast<F *>(u))(); },
                    .user = new F(std::move(on_click)),
                    .free = [](void *u) { delete static_cast<F *>(u); }});
        return *this;
    }

    template <class F> Panel &IconButton(Icon ic, const char *tip, F on_click) {
        impl::panel_widget(
            p_, impl::WidgetDesc{
                    .label = tip,
                    .kind = impl::WidgetKind::IconButton,
                    .icon = ic,
                    .on_click = [](void *u) { (*static_cast<F *>(u))(); },
                    .user = new F(std::move(on_click)),
                    .free = [](void *u) { delete static_cast<F *>(u); }});
        return *this;
    }

    Panel &IconToggle(Icon ic, const char *tip, bool &value) {
        impl::panel_widget(
            p_, impl::WidgetDesc{.label = tip,
                                 .kind = impl::WidgetKind::IconToggle,
                                 .icon = ic,
                                 .target = &value});
        return *this;
    }

    template <class F> Panel &Section(const char *title, F body) {
        return scoped(impl::Group::Section, title, std::move(body));
    }

    template <class F> Panel &Tabs(F body) {
        return scoped(impl::Group::Tabs, "", std::move(body));
    }

    template <class F> Panel &Tab(const char *title, F body) {
        return scoped(impl::Group::Tab, title, std::move(body));
    }

    template <class F> Panel &Row(F body) {
        return scoped(impl::Group::Row, "", std::move(body));
    }

    template <class F> Panel &Enabled(bool &cond, F body) {
        impl::panel_widget(
            p_, impl::WidgetDesc{.kind = impl::WidgetKind::GroupBegin,
                                 .group = impl::Group::Enabled,
                                 .target = &cond});
        body(*this);
        return close(impl::Group::Enabled);
    }

    template <std::invocable F, class G> Panel &Enabled(F cond, G body) {
        impl::panel_widget(
            p_, impl::WidgetDesc{
                    .kind = impl::WidgetKind::GroupBegin,
                    .group = impl::Group::Enabled,
                    .value =
                        [](void *u) {
                            return double(bool((*static_cast<F *>(u))()));
                        },
                    .user = new F(std::move(cond)),
                    .free = [](void *u) { delete static_cast<F *>(u); }});
        body(*this);
        return close(impl::Group::Enabled);
    }

  private:
    template <class F> Panel &scoped(impl::Group g, const char *title, F body) {
        impl::panel_widget(
            p_, impl::WidgetDesc{.label = title,
                                 .kind = impl::WidgetKind::GroupBegin,
                                 .group = g});
        body(*this);
        return close(g);
    }

    Panel &close(impl::Group g) {
        impl::panel_widget(
            p_,
            impl::WidgetDesc{.kind = impl::WidgetKind::GroupEnd, .group = g});
        return *this;
    }

    Panel &add(impl::WidgetKind k, const char *label, void *target) {
        impl::panel_widget(
            p_, impl::WidgetDesc{.label = label, .kind = k, .target = target});
        return *this;
    }

    impl::Panel p_;
};

}
