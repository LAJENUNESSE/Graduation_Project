#pragma once

#include "Core/Base.h"
#include <vector>

#include "Core/Layer.h"

namespace Engine
{

    class LayerStack
    {
    public:
        LayerStack() = default;
        ~LayerStack();

        void PushLayer(Scope<Layer> layer);
        void PushOverlay(Scope<Layer> overlay);
        void PopLayer(Layer* layer);
        void PopOverlay(Layer* overlay);

        std::vector<Scope<Layer>>::iterator         begin() { return m_Layers.begin(); }
        std::vector<Scope<Layer>>::iterator         end() { return m_Layers.end(); }
        std::vector<Scope<Layer>>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
        std::vector<Scope<Layer>>::reverse_iterator rend() { return m_Layers.rend(); }

        std::vector<Scope<Layer>>::const_iterator         begin() const { return m_Layers.begin(); }
        std::vector<Scope<Layer>>::const_iterator         end() const { return m_Layers.end(); }
        std::vector<Scope<Layer>>::const_reverse_iterator rbegin() const { return m_Layers.rbegin(); }
        std::vector<Scope<Layer>>::const_reverse_iterator rend() const { return m_Layers.rend(); }

    private:
        std::vector<Scope<Layer>> m_Layers;
        unsigned int              m_LayerInsertIndex = 0;
    };

} // namespace Engine