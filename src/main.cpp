#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <math.h>
#include <random>

#include "Renderer.hpp"
#include "SFML/System/Vector2.hpp"
#include "SFML/Window/VideoMode.hpp"

#define WIDTH 100'0ULL
#define HEIGHT 100'0ULL
#define POSITION_BOUND WIDTH* HEIGHT - 1
#define SHIP_COUNT 100'000ULL
#define WIN_FISH_COUNT 10'000LL
#define TICKS_PER_SECOND 10
#define TICK_DURATION_MS (1000 / TICKS_PER_SECOND)

// Тип лодки
enum ShipType {
    // Жадная
    GREEDY = 0,
    // Ленивая
    LAZY = 1,
    // Непоседливая
    RESTLESS = 2,
};

// Состояние лодки
enum ShipState {
    // Плывет
    FLOATING = 0,
    // Ждет конца рыбалки
    FISHING = 1,
    // Накопила победное число рыбы и уплывает
    FINISHING = 2,
    // Ушла с карты
    DEAD = 3,
};

/*
Данные лодки упакованы в 64-битное число.
Ниже расшифровка, начиная со старшего бита:

[2 бита - паддинг]
[4 бита - смещение по y]
[4 бита - смещение по x]
[34 бита - положение лодки (от 0 до 10^10-1 < 2^34 => 34 бита требуется)]
[14 бит - сколько рыбы выловила лодка (от 0 до 10000 < 2^14 => 14 бит требуется)]
[2 бита - таймер закидывания сети (1-3 тика)]
[2 бита - состояние лодки]
[2 бита - тип лодки]

Положение лодки хранится как одно число p < 100'000 * 100'000 (10^10).
Cмещения - когда лодка куда-то плывет, можно хранить не новую координату,
а смещение от текущей по x и y, декрементируя их каждый тик.
По 4 бита выбраны, чтобы в целом обеспечить 16 значений на координату:
от 0 до 15 или от -8 до +7, реализуя смещения в плюс и минус координату.
Таким образом, следующую клетку лодка выберет в радиусе 7-8 клеток.
*/

// Константы сдвигов и масок для различных значений лодки.
constexpr int STATE_SHIFT = 2;
constexpr int TIMER_SHIFT = 4;
constexpr int FISH_SHIFT = 6;
constexpr int POSITION_SHIFT = 20;
constexpr int OFFSET_X_SHIFT = 54;
constexpr int OFFSET_Y_SHIFT = 58;
constexpr uint64_t MASK_2BIT = 0x3ULL;
constexpr uint64_t MASK_4BIT = 0xFULL;
constexpr uint64_t MASK_14BIT = 0x3FFFULL;
constexpr uint64_t MASK_34BIT = 0x3FFFFFFFFULL;

// Инициализация распределений для значений симуляции.
std::uniform_int_distribution<int> shipTypeRng { 0, 2 };
std::uniform_int_distribution<int> shipTimerRng { 1, 3 };
std::uniform_int_distribution<int64_t> shipPositionRng { 0, POSITION_BOUND };
std::uniform_int_distribution<int> shipOffsetRng { 0, 15 };
std::uniform_int_distribution<int> catchRng { 1, 10 };
std::uniform_int_distribution<int> fishRng { 0, 15 };
std::uniform_int_distribution<int> cellTimerRnd { 15, 30 };

std::mt19937 rng(std::random_device {}());

// Устанавливает указанное значение с указанной маской и смещением. Вернет обновленное число.
inline uint64_t setbits(uint64_t n, uint64_t shift, uint64_t mask, uint64_t value)
{
    n &= ~(mask << shift); // Обнуляем значение.
    n |= ((value & mask) << shift); // Устанавливаем новое.
    return n;
}

int main()
{
    // Инициализация отрисовки.
    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(800, 600)), "GrandFishing");
    Renderer renderer(window, WIDTH, HEIGHT, 12);

    /*
    Карта для хранения только активных клеток.
    Ключ - координата клетки, аналогична положению лодки - индекс от 0 до 10^10-1.
    Значение - количество рыбы на клетке (0-15).

    Инициализируем примерным количеством активных клеток == числу кораблей.
    */
    std::unordered_map<uint64_t, uint8_t> activeCells;
    activeCells.reserve(SHIP_COUNT);

    /*
    Вектор для кольцевого буфера таймеров клеток.
    Значение - вектор индексов клеток, которые будут переведены в неопределенное состояние,
    когда указатель кольцевого буфера дойдет до их индекса.

    Инициализируем максимальным значением таймера клетки - 30.
    */
    std::vector<std::vector<uint64_t>> cellsTimers(30);

    /*
    Посчитаем среднее истечение клеток за ход, чтобы избежать реаллока векторов, если клеток истечет больше.
    Средний таймер - (15 + 30) / 2 = 22.5 секунды.
    Активных клеток в среднем == число кораблей - 100'000.
    Среднее истечение клеток за ход - 100'000 / 22.5.
    Округлим до тысяч и инициализируем векторы кольцевого буффера.
    */
    int64_t cellsPerTimer = ceil(SHIP_COUNT / 22.5 / 1000) * 1000;
    for (auto& cells : cellsTimers) {
        cells.reserve(cellsPerTimer);
    }

    /*
    Вектор лодок.
    */
    std::vector<uint64_t> ships(SHIP_COUNT);
    // Инициализируем лодки.
    for (int i = 0; i < SHIP_COUNT; i++) {
        // Генерируем лодку сразу в режиме рыбалки.
        uint64_t ship = 0;
        ship |= shipTypeRng(rng); // Тип лодки.
        ship |= ShipState::FISHING << STATE_SHIFT; // Состояние лодки.
        ship |= shipTimerRng(rng) << TIMER_SHIFT; // Таймер ожидания конца улова.
        ship |= shipPositionRng(rng) << POSITION_SHIFT; // Позиция лодки.

        ships[i] = ship;
    }

    // Данные для симуляции.
    int activeShips = SHIP_COUNT;
    uint64_t tick = 1;
    using Clock = std::chrono::steady_clock;
    auto lastTick = Clock::now();
    std::chrono::milliseconds tickDuration(TICK_DURATION_MS);
    // Основной цикл, симулирующий один тик.
    while (activeShips > 0 && window.isOpen()) {
        // Обрабатываем события SFML.
        while (const std::optional event = window.pollEvent()) {
            renderer.handleEvent(event);
        }

        // Отрисовываем сцену.
        renderer.drawScene(activeCells, ships);
        window.display();

        // Не выполняем шаги симуляции, если с последнего тика прошло меньше tickDuration времени.
        auto now = Clock::now();
        if (now - lastTick < tickDuration) {
            continue;
        }

        // Обрабатываем клетки.
        // Индекс текущей группы таймеров, которые заканчиваются.
        int expiringGroupIdx = tick % 30;
        auto& expiring = cellsTimers[expiringGroupIdx];
        for (uint64_t cellIdx : expiring) {
            // Удаляем клетку, переводя ее в неопределенное состояние.
            activeCells.erase(cellIdx);
        }
        // Очищаем индексы удаленных клеток.
        expiring.clear();

        // Обрабатываем суда.
        for (int i = 0; i < SHIP_COUNT; i++) {
            uint64_t ship = ships[i];

            uint8_t shipType = ship & MASK_2BIT; // Тип лодки
            uint8_t shipState = (ship >> STATE_SHIFT) & MASK_2BIT; // Состояние лодки.

            // Обновляем лодку
            switch (shipState) {
            case ShipState::DEAD:
                continue;
            case ShipState::FLOATING: {
                // Обрабатываем передвижение судна.

                uint64_t shipPosition = (ship >> POSITION_SHIFT) & MASK_34BIT;

                // Получаем сдвиги до целевой позиции.
                int64_t offsetX = (ship >> OFFSET_X_SHIFT) & MASK_4BIT;
                offsetX -= 8; // Чтобы получить значения от -8 до 7
                int64_t offsetY = (ship >> OFFSET_Y_SHIFT) & MASK_4BIT;
                offsetY -= 8; // Чтобы получить значения от -8 до 7;

                if (offsetX == 0 && offsetY == 0) {
                    // Если оба сдвига равны нулю, мы доплыли и можем начинать рыбачить.

                    // Обновляем состояние на ожидание окончания рыбалки.
                    ship = setbits(ship, STATE_SHIFT, MASK_2BIT, ShipState::FISHING);

                    // Устанавливаем таймер ожидания конца рыбалки.
                    ship = setbits(ship, TIMER_SHIFT, MASK_2BIT, shipTimerRng(rng));

                    break;
                }

                if (offsetX > 0) {
                    // Если смещение по X > 0, значит плывем в положительную сторону по x.
                    offsetX--;
                    shipPosition++;

                    // Проверяем на пересечение границы сверху.
                    if (shipPosition > POSITION_BOUND) {
                        shipPosition = 0;
                    }

                    // Устанавливаем новые значение смещения и положения.
                    ship = setbits(ship, OFFSET_X_SHIFT, MASK_4BIT, offsetX + 8);
                    ship = setbits(ship, POSITION_SHIFT, MASK_34BIT, shipPosition);

                    break;
                }

                if (offsetX < 0) {
                    // Если смещение по X < 0, значит плывем в отрицательную сторону по x.
                    offsetX++;
                    shipPosition--;

                    /*
                    Проверка на underflow.
                    Логически это можно представить как движение в верхней левой клетке налево.
                    Это приведет к перемещению в POSITION_BOUND координату - нижнюю правую.
                    */
                    if (shipPosition > POSITION_BOUND) {
                        shipPosition = POSITION_BOUND;
                    }

                    // Устанавливаем новые значение смещения и положения.
                    ship = setbits(ship, OFFSET_X_SHIFT, MASK_4BIT, offsetX + 8);
                    ship = setbits(ship, POSITION_SHIFT, MASK_34BIT, shipPosition);

                    break;
                }

                if (offsetY > 0) {
                    /*
                    Если смещение по Y > 0, значит мы должны двигаться вверх.
                    Для этого нужно уменьшить текущее положение на одну ширину карты.
                    */
                    offsetY--;

                    if (shipPosition < WIDTH) {
                        // Если позиция меньше ширины поля, значит мы на первой строке,
                        // и движение наверх должно перенести нас на
                        // самую нижнюю линию.
                        shipPosition = POSITION_BOUND - (WIDTH - shipPosition - 1);
                    } else {
                        shipPosition -= WIDTH;
                    }

                    ship = setbits(ship, OFFSET_Y_SHIFT, MASK_4BIT, offsetY + 8);
                    ship = setbits(ship, POSITION_SHIFT, MASK_34BIT, shipPosition);

                    break;
                }

                if (offsetY < 0) {
                    /*
                    Если смещение по Y < 0, значит мы должны двигаться вниз.
                    Для этого нужно увеличить текущее положение на одну ширину карты.
                    */
                    offsetY++;

                    if (shipPosition + WIDTH > POSITION_BOUND) {
                        /*
                        Если новая позиция выходит за границы, значит мы на нижней строке.
                        Движение еще ниже должно привести нас на первую строку.
                        */
                        shipPosition = WIDTH - (POSITION_BOUND - shipPosition) - 1;
                    } else {
                        shipPosition += WIDTH;
                    }

                    ship = setbits(ship, OFFSET_Y_SHIFT, MASK_4BIT, offsetY + 8);
                    ship = setbits(ship, POSITION_SHIFT, MASK_34BIT, shipPosition);

                    break;
                }

                break;
            }
            case ShipState::FISHING: {
                // Обрабатываем состояние рыбалки.

                uint64_t shipPosition = (ship >> POSITION_SHIFT) & MASK_34BIT;

                // Получаем текущее значение таймера ожидания улова.
                uint8_t fishTimer = (ship >> TIMER_SHIFT) & MASK_2BIT;
                fishTimer--;
                ship = setbits(ship, TIMER_SHIFT, MASK_2BIT, fishTimer);

                if (fishTimer > 0) {
                    break;
                }

                // Если таймер дошел до нуля, реализуем логику вылавливания рыбы.

                // Генерируем количество рыбы, которое выловила лодка.
                uint8_t fishCatched = catchRng(rng);

                /*
                Логика проверки, активна ли текущая клетка.
                Для этого ищем итератор.
                */
                auto it = activeCells.find(shipPosition);
                uint8_t cellFishCounter = 0;
                if (it == activeCells.end()) {
                    /*
                    Если итератор равен end(), текущая клетка была в неопределенном состоянии.
                    Активируем ее.
                    */

                    // Генерируем количество рыбы на клетке
                    cellFishCounter = fishRng(rng);
                    // Корректно изменяем количество рыбы на клетке.
                    if (fishCatched > cellFishCounter) {
                        fishCatched = cellFishCounter;
                        cellFishCounter = 0;
                    } else {
                        cellFishCounter -= fishCatched;
                    }

                    // Сохраняем новое значение рыбы в карте.
                    activeCells[shipPosition] = cellFishCounter;

                    // Генерируем таймер обновления клетки.
                    int cellTimeout = cellTimerRnd(rng);
                    int timerIdx = (tick + cellTimeout) % 30;
                    // Помещаем индекс текущей клетки в кольцевой буфер.
                    cellsTimers[timerIdx].push_back(shipPosition);
                } else {
                    // Если клетка уже есть в карте.

                    cellFishCounter = it->second;
                    // Корректно изменяем количество рыбы на клетке.
                    if (fishCatched > cellFishCounter) {
                        fishCatched = cellFishCounter;
                        cellFishCounter = 0;
                    } else {
                        cellFishCounter -= fishCatched;
                    }

                    // Сохраняем новое значение рыбы в карте.
                    it->second = cellFishCounter;
                }

                // Обновляем общее количество рыбы, которое выловила лодка.
                uint64_t shipFishCounter = (ship >> FISH_SHIFT) & MASK_14BIT;
                shipFishCounter = std::min(static_cast<long long int>(shipFishCounter + fishCatched), WIN_FISH_COUNT);
                ship = setbits(ship, FISH_SHIFT, MASK_14BIT, shipFishCounter);

                // Проверяем условие победы для лодки
                if (shipFishCounter == WIN_FISH_COUNT) {
                    // Лодка победила, ставим ей состояние уплывания с карты.
                    ship = setbits(ship, STATE_SHIFT, MASK_2BIT, ShipState::FINISHING);
                    break;
                }

                /*
                Лодка еще не победила, но рыбалку закончила.
                Определяем дальнейшее поведение лодки согласно ее типу.
                */
                switch (shipType) {
                case ShipType::GREEDY: {
                    // Жадная лодка рыбачит, пока не выловит все на текущей клетке.

                    if (cellFishCounter == 0) {
                        // На текущей клетке закончилась рыба.

                        // Генерируем случайные смещения для лодки.
                        ship = setbits(ship, OFFSET_X_SHIFT, MASK_4BIT, shipOffsetRng(rng));
                        ship = setbits(ship, OFFSET_Y_SHIFT, MASK_4BIT, shipOffsetRng(rng));
                        // Ставим лодке состояние плавания.
                        ship = setbits(ship, STATE_SHIFT, MASK_2BIT, ShipState::FLOATING);

                        break;
                    }

                    // На текущей клетке еще не закончилась рыба.

                    // Просто переустанавливаем таймер ожидания улова.
                    ship = setbits(ship, TIMER_SHIFT, MASK_2BIT, shipTimerRng(rng));

                    break;
                }
                case ShipType::LAZY: {
                    /*
                    Ленивая лодка никогда никуда не двигается.
                    Просто переустанавливаем таймер ожидания улова.
                    */
                    ship = setbits(ship, TIMER_SHIFT, MASK_2BIT, shipTimerRng(rng));

                    break;
                }
                case ShipType::RESTLESS: {
                    // Непоседа просто двигается на 1 клетку вправо.

                    // Устанавливаем сдвиг на 1 по x и состояние плавания.
                    ship = setbits(ship, OFFSET_X_SHIFT, MASK_4BIT, 1 + 8);
                    ship = setbits(ship, STATE_SHIFT, MASK_2BIT, ShipState::FLOATING);

                    break;
                }
                }

                break;
            }
            case ShipState::FINISHING: {
                /*
                Обрабатываем лодку, которая победила и уплывает с карты.
                Для этого просто двигаем ее на +1 по x, проверяя оставшееся расстояние до края карты.
                */

                uint64_t shipPosition = (ship >> POSITION_SHIFT) & MASK_34BIT;

                // Сколько клеток осталось до края карты.
                uint64_t distanceLeft = WIDTH - (shipPosition % WIDTH + 1);

                if (distanceLeft == 0) {
                    // Мы уже стоим у края карты, значит лодка исчезает.
                    ship = setbits(ship, STATE_SHIFT, MASK_2BIT, ShipState::DEAD);
                    activeShips--;
                } else {
                    // Еще осталось место для движения до края.
                    shipPosition++;

                    ship = setbits(ship, POSITION_SHIFT, MASK_34BIT, shipPosition);
                }

                break;
            }
            }

            ships[i] = ship;
        }

        tick++;
        lastTick += tickDuration;
    }

    if (window.isOpen())
        window.close();

    return 0;
}