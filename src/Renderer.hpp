#pragma once

#include "SFML/Graphics/PrimitiveType.hpp"
#include "SFML/System/Vector2.hpp"
#include "SFML/Window/Event.hpp"
#include <SFML/Graphics.hpp>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cassert>

class Renderer {
public:
    using CellMap = std::unordered_map<uint64_t, uint8_t>;
    using ShipArray = std::vector<uint64_t>;

    Renderer(sf::RenderWindow& window, uint32_t gridW, uint32_t gridH, unsigned int cellSizePx = 8u, float initialZoom = 1.0f);

    void handleEvent(const std::optional<sf::Event>& event);

    void drawScene(const CellMap& activeCells, const ShipArray& ships);

    void setViewCenter(const sf::Vector2f& worldCenter);
    void setZoom(float zoom);
    float getZoom() const noexcept { return m_zoom; }
    sf::View getView() const noexcept { return m_view; }

private:
    void ensureCellVertexCapacity(std::size_t cellsCount);
    sf::Color fishColorFromAmount(uint8_t fish) const noexcept;
    bool isCellVisible(uint64_t x, uint64_t y, const sf::FloatRect& worldRect) const noexcept;

    sf::RenderWindow& m_window;
    sf::View m_view;

    uint32_t m_gridW;
    uint32_t m_gridH;
    unsigned int m_baseCellSizePx;

    sf::VertexArray m_cellsVA;

    sf::CircleShape m_shipDot; // точка для плывущего корабля
    sf::RectangleShape m_shipSquare; // квадрат для рыбачущего
    sf::CircleShape m_shipTriangle; // треугольник для уплывающего с карты

    float m_zoom = 1.0f;
    const float m_zoomMin = 0.000000001f;
    const float m_zoomMax = 1000000000.0f;
    const float m_zoomFactorPerWheel = 2.5f;

    bool m_rightDragging = false;
    sf::Vector2i m_lastMousePixel;
};

inline Renderer::Renderer(sf::RenderWindow& window, uint32_t gridW, uint32_t gridH, unsigned int cellSizePx, float initialZoom)
    : m_window(window)
    , m_gridW(gridW)
    , m_gridH(gridH)
    , m_baseCellSizePx(cellSizePx)
    , m_cellsVA(sf::PrimitiveType::Triangles)
{
    assert(gridW > 0 && gridH > 0);
    m_zoom = std::max(0.0001f, initialZoom);
    float fullW = static_cast<float>(gridW * cellSizePx);
    float fullH = static_cast<float>(gridH * cellSizePx);
    m_view.setCenter(sf::Vector2f(fullW * 0.5f, fullH * 0.5f));

    // Начальный размер в мировых координатах – с учётом окна
    sf::Vector2u winSize = m_window.getSize();
    float winAspect = static_cast<float>(winSize.x) / static_cast<float>(winSize.y);
    float gridAspect = fullW / fullH;

    if (gridAspect > winAspect) {
        // сетка шире окна – ширину берем полностью, высоту корректируем
        m_view.setSize(sf::Vector2f(fullW, fullW / winAspect));
    } else {
        // сетка выше окна – высоту берем полностью, ширину корректируем
        m_view.setSize(sf::Vector2f(fullH * winAspect, fullH));
    }

    if (m_zoom != 1.0f)
        m_view.zoom(1.0f / m_zoom);

    float r = std::max(1.f, cellSizePx * 0.18f);
    m_shipDot.setRadius(r);
    m_shipDot.setOrigin(sf::Vector2f(r, r));
    m_shipDot.setPointCount(30);
    m_shipDot.setFillColor(sf::Color::Black);

    float sq = std::max(1.f, cellSizePx * 0.5f);
    m_shipSquare.setSize({ sq, sq });
    m_shipSquare.setOrigin(sf::Vector2f(sq * 0.5f, sq * 0.5f));
    m_shipSquare.setFillColor(sf::Color::Black);

    float triR = std::max(1.f, cellSizePx * 0.35f);
    m_shipTriangle.setRadius(triR);
    m_shipTriangle.setOrigin(sf::Vector2f(triR, triR));
    m_shipTriangle.setPointCount(3);
    m_shipTriangle.setFillColor(sf::Color::Black);

    m_cellsVA.resize(0);
    m_cellsVA.setPrimitiveType(sf::PrimitiveType::Triangles);
}

inline void Renderer::handleEvent(const std::optional<sf::Event>& event)
{
    if (!event)
        return;

    if (event->is<sf::Event::Closed>()) {
        m_window.close();
        return;
    }

    if (event->is<sf::Event::Resized>()) {
        if (const auto* size = event->getIf<sf::Event::Resized>()) {
            float newWinAspect = static_cast<float>(size->size.x) / size->size.y;
            float fullW = static_cast<float>(m_gridW * m_baseCellSizePx);
            float fullH = static_cast<float>(m_gridH * m_baseCellSizePx);

            if ((fullW / fullH) > newWinAspect)
                m_view.setSize(sf::Vector2f(fullW, fullW / newWinAspect));
            else
                m_view.setSize(sf::Vector2f(fullH * newWinAspect, fullH));
        }

        return;
    }

    if (event->is<sf::Event::MouseWheelScrolled>()) {
        if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
            float delta = -wheel->delta;
            if (delta == 0.f)
                return;

            sf::Vector2i mp = sf::Mouse::getPosition(m_window);

            float factor = (delta > 0.f) ? (1.0f / m_zoomFactorPerWheel) : m_zoomFactorPerWheel;
            float newZoom = std::clamp(m_zoom * factor, m_zoomMin, m_zoomMax);
            factor = newZoom / m_zoom;
            if (std::abs(factor - 1.f) < 1e-6f)
                return;

            sf::Vector2f worldBefore = m_window.mapPixelToCoords(mp, m_view);
            m_view.zoom(1.0f / factor);
            m_zoom = newZoom;
            sf::Vector2f worldAfter = m_window.mapPixelToCoords(mp, m_view);
            m_view.move(worldBefore - worldAfter);
        }

        return;
    }
}

inline void Renderer::drawScene(const CellMap& activeCells, const ShipArray& ships)
{
    bool rightDown = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    sf::Vector2i mousePix = sf::Mouse::getPosition(m_window);

    if (rightDown) {
        if (!m_rightDragging) {
            m_rightDragging = true;
            m_lastMousePixel = mousePix;
        } else {
            sf::Vector2f worldLast = m_window.mapPixelToCoords(m_lastMousePixel, m_view);
            sf::Vector2f worldNow = m_window.mapPixelToCoords(mousePix, m_view);
            sf::Vector2f delta = worldLast - worldNow;
            if (delta.x != 0.f || delta.y != 0.f)
                m_view.move(delta);
            m_lastMousePixel = mousePix;
        }
    } else {
        m_rightDragging = false;
    }

    m_window.setView(m_view);

    sf::Vector2f viewSize = m_view.getSize();
    sf::Vector2f viewCenter = m_view.getCenter();
    sf::FloatRect viewRect(viewCenter - viewSize * 0.5f, viewSize);

    m_cellsVA.clear();

    float cellSizeWorld = static_cast<float>(m_baseCellSizePx);
    float inset = std::max(0.f, cellSizeWorld * 0.12f);

    m_cellsVA.setPrimitiveType(sf::PrimitiveType::Triangles);
    std::size_t visibleEstimate = std::min<std::size_t>(activeCells.size(), 1024);
    std::vector<sf::Vertex> verts;
    verts.reserve(std::min<std::size_t>(activeCells.size() * 6, 65536));

    for (const auto& kv : activeCells) {
        uint64_t pos = kv.first;
        uint8_t fish = kv.second;
        uint64_t x = pos % m_gridW;
        uint64_t y = pos / m_gridW;

        float px = static_cast<float>(x) * cellSizeWorld;
        float py = static_cast<float>(y) * cellSizeWorld;
        float left = px + inset;
        float top = py + inset;
        float right = px + cellSizeWorld - inset;
        float bottom = py + cellSizeWorld - inset;

        float viewLeft = viewRect.position.x;
        float viewTop = viewRect.position.y;
        float viewRight = viewLeft + viewRect.size.x;
        float viewBottom = viewTop + viewRect.size.y;

        if (right < viewLeft || left > viewRight || bottom < viewTop || top > viewBottom) {
            continue;
        }

        sf::Color col = fishColorFromAmount(fish);

        verts.emplace_back(sf::Vector2f(left, top), col);
        verts.emplace_back(sf::Vector2f(right, top), col);
        verts.emplace_back(sf::Vector2f(right, bottom), col);

        verts.emplace_back(sf::Vector2f(left, top), col);
        verts.emplace_back(sf::Vector2f(right, bottom), col);
        verts.emplace_back(sf::Vector2f(left, bottom), col);
    }

    if (!verts.empty()) {
        m_cellsVA.resize(verts.size());
        for (std::size_t i = 0; i < verts.size(); ++i)
            m_cellsVA[i] = verts[i];
    }

    m_window.clear(sf::Color(230, 230, 230));

    if (m_cellsVA.getVertexCount() > 0) {
        m_window.draw(m_cellsVA);
    }

    for (std::size_t i = 0; i < ships.size(); ++i) {
        uint64_t ship = ships[i];

        uint64_t shipPosition = (ship >> 20) & 0x3FFFFFFFFULL;
        uint8_t shipState = (ship >> 2) & 0x3;
        uint64_t sx = shipPosition % m_gridW;
        uint64_t sy = shipPosition / m_gridW;

        float cx = static_cast<float>(sx) * cellSizeWorld + cellSizeWorld * 0.5f;
        float cy = static_cast<float>(sy) * cellSizeWorld + cellSizeWorld * 0.5f;

        float viewLeft = viewRect.position.x;
        float viewTop = viewRect.position.y;
        float viewRight = viewLeft + viewRect.size.x;
        float viewBottom = viewTop + viewRect.size.y;

        if (cx < viewLeft || cx > viewRight || cy < viewTop || cy > viewBottom) {
            continue;
        }

        if (shipState == 0 /*FLOATING*/) {
            m_shipDot.setPosition(sf::Vector2f(cx, cy));
            m_window.draw(m_shipDot);
        } else if (shipState == 1 /*FISHING*/) {
            m_shipSquare.setPosition(sf::Vector2f(cx, cy));
            m_window.draw(m_shipSquare);
        } else if (shipState == 2 /*FINISHING*/) {
            m_shipTriangle.setPosition(sf::Vector2f(cx, cy));
            m_window.draw(m_shipTriangle);
        } else {
        }
    }

    // Границы карты.
    sf::VertexArray border(sf::PrimitiveType::LineStrip, 5);
    float w = static_cast<float>(m_gridW * m_baseCellSizePx);
    float h = static_cast<float>(m_gridH * m_baseCellSizePx);
    border[0].position = { 0.f, 0.f };
    border[1].position = { w, 0.f };
    border[2].position = { w, h };
    border[3].position = { 0.f, h };
    border[4].position = { 0.f, 0.f };
    for (int i = 0; i < 5; ++i)
        border[i].color = sf::Color::Black;
    m_window.draw(border);
}

inline void Renderer::setViewCenter(const sf::Vector2f& worldCenter)
{
    m_view.setCenter(worldCenter);
}

inline void Renderer::setZoom(float zoom)
{
    if (zoom <= 0.f)
        return;
    float clamped = std::clamp(zoom, m_zoomMin, m_zoomMax);
    float factor = clamped / m_zoom;
    if (std::abs(factor - 1.f) < 1e-6f)
        return;
    m_view.zoom(1.0f / factor);
    m_zoom = clamped;
}

// Хелпер для получения цвета клетки из количества рыбы.
inline sf::Color Renderer::fishColorFromAmount(uint8_t fish) const noexcept
{
    constexpr uint8_t minG = 50; // минимальная яркость
    constexpr uint8_t maxG = 255; // максимальная яркость
    constexpr uint8_t maxFish = 15;

    uint8_t clamped = (fish > maxFish) ? maxFish : fish;

    // Пропорционально масштабируем значение в диапазон [minG, maxG]
    uint8_t g = static_cast<uint8_t>(
        minG + (static_cast<int>(clamped) * (maxG - minG)) / maxFish);

    return sf::Color(0, g, 0);
}