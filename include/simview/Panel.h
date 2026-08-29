#pragma once

#include "Types.h"

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
    Checkbox
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
        impl::WidgetDesc d;
        d.label = label;
        d.kind = impl::WidgetKind::Slider;
        d.target = &value;
        d.min = min;
        d.max = max;
        impl::panel_widget(p_, d);
        return *this;
    }

    Panel &Checkbox(const char *label, bool &value) {
        return add(impl::WidgetKind::Checkbox, label, &value);
    }

    template <class F> Panel &Button(const char *label, F on_click) {
        impl::WidgetDesc d;
        d.label = label;
        d.kind = impl::WidgetKind::Button;
        d.user = new F(std::move(on_click));
        d.on_click = [](void *u) { (*static_cast<F *>(u))(); };
        d.free = [](void *u) { delete static_cast<F *>(u); };
        impl::panel_widget(p_, d);
        return *this;
    }

  private:
    Panel &add(impl::WidgetKind k, const char *label, void *target) {
        impl::WidgetDesc d;
        d.label = label;
        d.kind = k;
        d.target = target;
        impl::panel_widget(p_, d);
        return *this;
    }

    impl::Panel p_;
};

}
