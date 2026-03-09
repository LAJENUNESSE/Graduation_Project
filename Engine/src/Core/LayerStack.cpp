#include "engpch.h"
#include "Core/LayerStack.h"

namespace Engine
{

    LayerStack::~LayerStack()
    {
        for (const auto& layer : m_Layers)
            layer->OnDetach();
    }

    void LayerStack::PushLayer(Scope<Layer> layer)
    {
        m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, std::move(layer));
        m_LayerInsertIndex++;
    }

    void LayerStack::PushOverlay(Scope<Layer> overlay)
    {
        m_Layers.emplace_back(std::move(overlay));
    }

    void LayerStack::PopLayer(Layer* layer)
    {
        auto it = std::find_if(m_Layers.begin(), m_Layers.begin() + m_LayerInsertIndex,
                               [layer](const Scope<Layer>& candidate) { return candidate.get() == layer; });
        if (it != m_Layers.begin() + m_LayerInsertIndex)
        {
            (*it)->OnDetach();
            m_Layers.erase(it);
            m_LayerInsertIndex--;
        }
    }

    void LayerStack::PopOverlay(Layer* overlay)
    {
        auto it = std::find_if(m_Layers.begin() + m_LayerInsertIndex, m_Layers.end(),
                               [overlay](const Scope<Layer>& candidate) { return candidate.get() == overlay; });
        if (it != m_Layers.end())
        {
            (*it)->OnDetach();
            m_Layers.erase(it);
        }
    }

} // namespace Engine