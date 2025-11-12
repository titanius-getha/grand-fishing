#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

class InfoPanel {
public:
    InfoPanel(sf::RenderWindow& window, const sf::Font& font, unsigned int charSize = 20)
        : m_window(window)
        , m_text(font, "", charSize)
    {
        m_text.setFont(font);
        m_text.setCharacterSize(charSize);
        m_text.setFillColor(sf::Color::Black);
        m_text.setPosition(sf::Vector2f(10.f, 10.f));
    }

    // Передаем строки, которые хотим нарисовать
    void setLines(const std::vector<std::string>& lines)
    {
        m_lines = lines;
        std::string combined;
        for (const auto& l : m_lines)
            combined += l + "\n";
        m_text.setString(combined);
    }

    void draw()
    {
        sf::View infoView(sf::FloatRect(sf::Vector2f(0.f, 0.f), sf::Vector2f(static_cast<float>(m_window.getSize().x), static_cast<float>(m_window.getSize().y))));
        m_window.setView(infoView);

        // Получаем глобальные границы текста после установки строки
        sf::FloatRect bounds = m_text.getLocalBounds();
        m_text.setOrigin(sf::Vector2f(300, 200));

        // Ставим текст в правый нижний угол с отступом
        m_text.setPosition(
            sf::Vector2f(
                static_cast<float>(m_window.getSize().x) - 10.f,
                static_cast<float>(m_window.getSize().y) - 10.f));

        m_window.draw(m_text);
    }

private:
    sf::RenderWindow& m_window;
    sf::Text m_text;
    std::vector<std::string> m_lines;
};
