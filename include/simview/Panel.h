#pragma once

#include "Types.h"
#include "sync/Sync.h"

#include <concepts>
#include <cstdint>
#include <utility>

namespace sv {

namespace impl {

struct App;

enum class WidgetKind : std::int32_t {
    Text,
    Separator,
    Button,
    Slider,
    Checkbox,
    Value,
    Transport
};

struct Panel {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

struct WidgetDesc {
    const char *label = nullptr;
    WidgetKind kind = WidgetKind::Text;
    void *target = nullptr;
    float min = 0.0f, max = 1.0f;
    const char *fmt = "%.3g";
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

    Panel &Slider(const char *label, float &value, float min, float max) {
        impl::panel_widget(p_,
                           impl::WidgetDesc{.label = label,
                                            .kind = impl::WidgetKind::Slider,
                                            .target = &value,
                                            .min = min,
                                            .max = max});
        return *this;
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

  private:
    Panel &add(impl::WidgetKind k, const char *label, void *target) {
        impl::panel_widget(
            p_, impl::WidgetDesc{.label = label, .kind = k, .target = target});
        return *this;
    }

    impl::Panel p_;
};

}
