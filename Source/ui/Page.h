#pragma once
#include "Layout.h"
#include "../PluginProcessor.h"

namespace huldra::ui
{
    // Base of all pages: tick() is called at 30 Hz while the page is visible (and by hand for screenshots).
    class Page : public fjord::base::ui::Page
    {
    public:
        explicit Page (HuldraProcessor& p) : proc (p) {}

    protected:
        HuldraProcessor& proc;
    };
}
