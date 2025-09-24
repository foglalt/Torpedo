#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#endif

#define ASSERT(x) if(!(x)) { printf("ERROR in file %s, line: %d: %s", __FILE__, __LINE__, #x); exit(-1); }

#define DESTROYED_FIRST_MASK 0x8
#define DESTROYED_SECOND_MASK 0x4
#define DESTROYED_THIRD_MASK 0x2
#define DESTROYED_FOURTH_MASK 0x1
#define SHIP_DESTROYED_MASK (DESTROYED_FIRST_MASK | DESTROYED_SECOND_MASK | DESTROYED_THIRD_MASK | DESTROYED_FOURTH_MASK)

#define N 10
#define ROWS N
#define COLS N
#define ROW_FROM(x) (x/ROWS)
#define COL_FROM(x) (x%COLS)
#define CELLS (ROWS*COLS)
#define MAX_INDEX (CELLS - 1)
#define INT_BITS (8 * sizeof(int))
#define STORAGE_SIZE ((CELLS / INT_BITS) + 1)

static int map[STORAGE_SIZE];
static int map_shot[STORAGE_SIZE];

static int map2[STORAGE_SIZE];
static int map_shot2[STORAGE_SIZE];

#define SHIP_L4 1
#define SHIP_L3 2
#define SHIP_L2 2
#define SHIP_L1 0
#define SHIP_TYPES 4
#define SHIP_NUMBER (SHIP_L4 + SHIP_L3 + SHIP_L2 + SHIP_L1)

static const int SHIP_TEMPLATE[SHIP_TYPES] = { SHIP_L1, SHIP_L2, SHIP_L3, SHIP_L4 };
static int player_ships_to_place[SHIP_TYPES];
static int player_ships_remaining[SHIP_TYPES];
static int enemy_ships_remaining[SHIP_TYPES];

#if defined(_WIN32) || defined(_WIN64)
static HANDLE hConsole;

static void ClearScreen(void)
{
    COORD coord = { 0, 0 };
    DWORD count;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    GetConsoleScreenBufferInfo(hConsole, &csbi);
    FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X * csbi.dwSize.Y, coord, &count);
    SetConsoleCursorPosition(hConsole, coord);
}

static void GoToXY(int x, int y)
{
    COORD coord;
    coord.X = x;
    coord.Y = y;

    SetConsoleCursorPosition(hConsole, coord);
}
#else
static void ClearScreen(void)
{
    printf("\033[2J\033[H");
    fflush(stdout);
}

static void GoToXY(int x, int y)
{
    printf("\033[%d;%dH", y + 1, x + 1);
    fflush(stdout);
}
#endif

typedef struct ship
{
    // yyyyxxxx
    // essssdll -> exist, state -> destroyed/not destroyed, direction: 0 -> horizontal, 1 -> vertical, length = ll+1
    unsigned char y : 4;
    unsigned char x : 4;
    unsigned char exists : 1;
    unsigned char state : 4;
    unsigned char direction : 1;
    unsigned char length : 2;
} ship_t;

typedef enum
{
    GAME_CONTINUES = 0,
    PLAYER_WON,
    AI_WON
} game_state_t;

int length(ship_t* ship)
{
    return ship->length + 1;
}

//User - Player
static int last_ship = 0;
static ship_t ships[SHIP_NUMBER];

//AI - Player2
static int last_ship2 = 0;
static ship_t ships2[SHIP_NUMBER];

static game_state_t GameState(void)
{
    bool player_has_ships = false;
    bool enemy_has_ships = false;

    for (int i = 0; i < SHIP_NUMBER; i++)
    {
        if (ships[i].exists)
        {
            player_has_ships = true;
        }

        if (ships2[i].exists)
        {
            enemy_has_ships = true;
        }
    }

    if (!enemy_has_ships)
    {
        return PLAYER_WON;
    }

    if (!player_has_ships)
    {
        return AI_WON;
    }

    return GAME_CONTINUES;
}

void DebugShip(ship_t* ship)
{
    printf("Ship at %p\n", (void*)ship);
    printf("\tat (%d,%d), direction: %s\n", ship->x, ship->y, ship->direction ? "vertical" : "horizontal");
    printf("\tlength: %d\n", ship->length + 1);
    printf("\tstate is %x, exists: %d\n", ship->state, ship->exists);
}

//-1 -> nincs a hajóban, ha 0-3, az az index
int InShip(ship_t* ship, int x, int y)
{
    if (ship->direction)
    {
        if (ship->x != x)
        {
            return -1;
        }

        for (int i = 0; i < length(ship); i++)
        {
            if ((ship->y + i) == y)
            {
                return i;
            }
        }
    }
    else
    {
        if (ship->y != y)
        {
            return -1;
        }

        for (int i = 0; i < length(ship); i++)
        {
            if ((ship->x + i) == x)
            {
                return i;
            }
        }
    }

    return -1;
}

size_t flength(FILE* file)
{
    int pos = ftell(file);
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, pos, SEEK_SET);
    return size;
}

int GetState(int col, int row, int* array)
{
    ASSERT(col >= 0 && row >= 0 && col < COLS && row < ROWS && array != 0);

    int index = row * COLS + col;
    int i = (index / INT_BITS);
    int j = (index % INT_BITS);
    return ((array[i] & (0x1 << j)) >> j) & 0x1; //0-t vagy 1-et ad vissza = nem talált vagy de
}

void SetState(int col, int row, int* array)
{
    //printf("%d   %d    %d",col ,row, array);
    ASSERT(col >= 0 && row >= 0 && col < COLS && row < ROWS && array != 0);

    int index = row * COLS + col;
    int i = (index / INT_BITS);
    int j = (index % INT_BITS);
    array[i] |= (0x1 << j); //bitjátékok
}

int Check(int x, int y, int width, int height, int* array)
{
    if (width < 1 || height < 1 || array == 0)
    {
        return -1;
    }

    int result = 0;
    for (int i = x; i < (x + width); i++)
    {
        for (int j = y; j < (y + height); j++)
        {
            if (i < 0 || i >= COLS || j < 0 || j >= ROWS)
            {
                continue;
            }

            result += GetState(i, j, array);
        }
    }

    return result;
}

void RevealSurroundings(int x, int y, int width, int height, int* array)
{
    if (width < 1 || height < 1 || array == 0)
    {
        return;
    }

    for (int i = x; i < (x + width); i++)
    {
        for (int j = y; j < (y + height); j++)
        {
            if (i < 0 || i >= COLS || j < 0 || j >= ROWS)
            {
                continue;
            }

            SetState(i, j, array);
        }
    }
}

int GetCoordinates(int* x, int* y)
{
    static char input[4] = { 0 };
    scanf("%s", input);

    if (!(input[0] >= 'A' && input[0] <= 'J'))
        return 0;

    if (!(input[1] >= '1' && input[1] <= '9'))
        return 0;

    if (!((input[1] == '1' && input[2] == '0') || (input[2] == 0)))
        return 0;

    *y = (input[0] - 'A') + 1;
    *x = (input[1] - '1') + 1;
    if (input[1] == '1' && input[2] == '0')
        *x = 10;
    return 1;
}

void GetShip(int length)
{
    int ok = 0;
    int x, y, dr = -1;

    do
    {
        if (length > 1)
        {
            ok = 1;
            do
            {
                printf("Which direction is your ship facing? (0 - horizontal; 1 - vertical)\n");
                char in[4] = { 0 };
                scanf("%s", in);
                if (in[1] != 0 && (in[0] < '0' || in[0] > '1'))
                {
                    continue;
                }

                dr = in[0] - '0';
            } while (dr != 1 && dr != 0);
        }
        else
        {
            dr = 0;
        }

        printf("Please tell us the position of your ship's upper left corner\n");
        do
        {
            if (!GetCoordinates(&x, &y))
                continue;

            if (dr)
            {
                if (length + y > 11 || x >= 11)
                    ok = 0;
                else
                    ok = 1;
            }
            else
            {
                if (length + x > 11 || y >= 11)
                    ok = 0;
                else
                    ok = 1;
            }
        } while (!ok);

        if (dr)
        {
            ok = Check(x - 2, y - 2, 3, length + 2, map) == 0;
        }
        else
        {
            ok = Check(x - 2, y - 2, length + 2, 3, map) == 0;
        }

        if (ok)
        {
            player_ships_to_place[length - 1]--;
            printf("The ship has set sail!\n");
        }
        else
        {
            printf("\nI'll let the rules sink in...\n");
        }
    } while (!ok);

    if (dr)
    {
        for (int i = y; i < y + length; i++)
        {
            SetState(x - 1, i - 1, map);
        }
    }
    else
    {
        for (int i = x; i < x + length; i++)
        {
            SetState(i - 1, y - 1, map);
        }
    }

    int state = 0;
    if (length != 4)
    {
        for (int i = 0; i < 4 - length; i++)
        {
            state |= 0x1 << i; //ez miez?
        }
    }

    ship_t ship;
    memset(&ship, 0, sizeof(ship_t));
    ship.exists = 1;
    ship.length = length - 1;
    ship.direction = dr;
    ship.state = state;
    ship.x = x - 1;
    ship.y = y - 1;

    ships[last_ship++] = ship;
}

void print_map(int* arr)
{
    char abc[N];
    for (int i = 0; i < N; i++)
    {
        abc[i] = ('A' + i);
    }

    for (int i = 0; i < ROWS * COLS; i++)
    {
        if (!i)
        {
            printf("\n   ");
            for (int j = 0; j < N; j++)
            {
                printf(" %d ", j + 1);
            }
            printf("\n");
        }
        if (!(i % N))
        {
            printf(" %c ", abc[i / N]);
        }
        printf(" %d ", GetState(COL_FROM(i), ROW_FROM(i), arr));

        if (!((i + 1) % N))
        {
            printf("\n");
        }
    }
}

void PlaceShips()
{
    int length;
    for (int i = 0; i < SHIP_TYPES; i++)
    {
        length = i + 1;
        
        while (player_ships_to_place[i] != 0)
        {
            printf("You have %d ships left of length %d\n", player_ships_to_place[i], length);
            print_map(map);
            GetShip(length);
            ClearScreen();
        }
    }
}

void PrintDiscovered(int* map2, int* shot2, int x, int y)
{
    char abc[N];
    for (int i = 0; i < N; i++)
    {
        abc[i] = ('A' + i);
    }

    GoToXY(x, y);
    for (int i = 0; i < ROWS * COLS; i++)
    {
        if (!i)
        {
            printf("   ");
            for (int j = 0; j < N; j++)
            {
                printf(" %d ", j + 1);
            }
            //printf("\n");
            GoToXY(x, y + 1);
        }
        if (!(i % N))
        {
            printf(" %c ", abc[i / N]);
        }

        if (GetState(COL_FROM(i), ROW_FROM(i), shot2))
        {
            if (GetState(COL_FROM(i), ROW_FROM(i), map2))
            {
                printf(" X ");
            }
            else
            {
                printf(" O ");
            }
        }
        else
        {
            printf("   ");
        }

        if (!((i + 1) % N))
        {
            GoToXY(x, y + 1 + ((i + 1) / N));
            //printf("\n");
        }
    }
}

static void RandomPlaceShip(int length, int* array)
{
    bool placed = false;

    while (!placed)
    {
        int vertical = (length > 1) ? rand() % 2 : 0;
        int x;
        int y;

        if (vertical)
        {
            x = rand() % COLS;
            y = rand() % (ROWS - length + 1);
            if (Check(x - 1, y - 1, 3, length + 2, array) != 0)
            {
                continue;
            }

            for (int k = 0; k < length; ++k)
            {
                SetState(x, y + k, array);
            }
        }
        else
        {
            x = rand() % (COLS - length + 1);
            y = rand() % ROWS;
            if (Check(x - 1, y - 1, length + 2, 3, array) != 0)
            {
                continue;
            }

            for (int k = 0; k < length; ++k)
            {
                SetState(x + k, y, array);
            }
        }

        int state = 0;
        if (length != 4)
        {
            for (int i = 0; i < 4 - length; i++)
            {
                state |= 0x1 << i;
            }
        }

        ship_t ship;
        memset(&ship, 0, sizeof(ship_t));
        ship.exists = 1;
        ship.length = length - 1;
        ship.direction = vertical;
        ship.state = state;
        ship.x = x;
        ship.y = y;

        ships2[last_ship2++] = ship;
        placed = true;
    }
}

static void GenerateRandomMap(int* target_map)
{
    memset(target_map, 0, sizeof(int) * STORAGE_SIZE);
    memset(ships2, 0, sizeof(ships2));
    last_ship2 = 0;

    for (int i = 0; i < SHIP_TYPES; ++i)
    {
        int length = i + 1;
        int count = SHIP_TEMPLATE[i];

        while (count-- > 0)
        {
            RandomPlaceShip(length, target_map);
        }
    }

    memcpy(enemy_ships_remaining, SHIP_TEMPLATE, sizeof(enemy_ships_remaining));
}

static ship_t* HitShip(ship_t* fleet, int* remaining_counts, int x, int y, int* shots)
{
    for (int i = 0; i < SHIP_NUMBER; i++)
    {
        ship_t* tmp = &fleet[i];
        if (!tmp->exists)
        {
            continue;
        }

        int idx = InShip(tmp, x, y);
        if (idx > -1)
        {
            tmp->state |= (0x1 << (3 - idx));

            if ((tmp->state & SHIP_DESTROYED_MASK) == SHIP_DESTROYED_MASK)
            {
                tmp->exists = 0;
                if (remaining_counts)
                {
                    ASSERT(tmp->length < SHIP_TYPES);
                    remaining_counts[tmp->length]--;
                }

                if (shots)
                {
                    if (tmp->direction)
                    {
                        RevealSurroundings(tmp->x - 1, tmp->y - 1, 3, length(tmp) + 2, shots);
                    }
                    else
                    {
                        RevealSurroundings(tmp->x - 1, tmp->y - 1, length(tmp) + 2, 3, shots);
                    }
                }
            }

            return tmp;
        }
    }

    return NULL;
}

static int Shoot(int x, int y, int* map, int* shot);

enum
{
    DIR_RIGHT = 0,
    DIR_LEFT = 1,
    DIR_UP = 2,
    DIR_DOWN = 3
};

static const int DIRECTION_DELTAS[4][2] = {
    { 1, 0 },
    { -1, 0 },
    { 0, -1 },
    { 0, 1 }
};

typedef enum
{
    AI_STATE_NO_TARGET = 0,
    AI_STATE_SEEK,
    AI_STATE_HORIZONTAL,
    AI_STATE_VERTICAL
} ai_mode_t;

typedef struct
{
    ai_mode_t state;
    int origin_col;
    int origin_row;
    int last_col;
    int last_row;
    int direction;
    int alternate_direction;
    int directions_order[4];
    int directions_index;
} ai_memory_t;

static ai_memory_t ai_memory = { AI_STATE_NO_TARGET, -1, -1, -1, -1, -1, -1, { 0, 1, 2, 3 }, 0 };

static int OppositeDirection(int direction)
{
    switch (direction)
    {
    case DIR_RIGHT:
        return DIR_LEFT;
    case DIR_LEFT:
        return DIR_RIGHT;
    case DIR_UP:
        return DIR_DOWN;
    case DIR_DOWN:
        return DIR_UP;
    default:
        return direction;
    }
}

static void ResetAiMemory(void)
{
    ai_memory.state = AI_STATE_NO_TARGET;
    ai_memory.origin_col = -1;
    ai_memory.origin_row = -1;
    ai_memory.last_col = -1;
    ai_memory.last_row = -1;
    ai_memory.direction = -1;
    ai_memory.alternate_direction = -1;
    ai_memory.directions_index = 0;

    for (int i = 0; i < 4; ++i)
    {
        ai_memory.directions_order[i] = i;
    }
}

static void ShuffleDirections(int directions[4])
{
    for (int i = 0; i < 4; ++i)
    {
        directions[i] = i;
    }

    for (int i = 3; i > 0; --i)
    {
        int j = rand() % (i + 1);
        int tmp = directions[i];
        directions[i] = directions[j];
        directions[j] = tmp;
    }
}

static bool IsValidCellCoord(int col, int row)
{
    return col >= 0 && col < COLS && row >= 0 && row < ROWS;
}

static bool AlreadyShot(int col, int row, int* shot_map)
{
    return GetState(col, row, shot_map) != 0;
}

static bool PickRandomUnshotCell(int* col, int* row, int* shot_map)
{
    for (int attempt = 0; attempt < CELLS * 2; ++attempt)
    {
        int index = rand() % CELLS;
        int candidate_col = index % COLS;
        int candidate_row = index / COLS;

        if (!AlreadyShot(candidate_col, candidate_row, shot_map))
        {
            *col = candidate_col;
            *row = candidate_row;
            return true;
        }
    }

    for (int r = 0; r < ROWS; ++r)
    {
        for (int c = 0; c < COLS; ++c)
        {
            if (!AlreadyShot(c, r, shot_map))
            {
                *col = c;
                *row = r;
                return true;
            }
        }
    }

    return false;
}

static int AiShootAt(int col, int row, ship_t** ship)
{
    if (!IsValidCellCoord(col, row) || AlreadyShot(col, row, map_shot))
    {
        return -1;
    }

    int hit = Shoot(col, row, map, map_shot);
    if (hit)
    {
        *ship = HitShip(ships, player_ships_remaining, col, row, map_shot);
    }
    else
    {
        *ship = NULL;
    }

    return hit;
}

static void AiHandleNoTarget(void)
{
    int col;
    int row;

    if (!PickRandomUnshotCell(&col, &row, map_shot))
    {
        return;
    }

    ship_t* ship = NULL;
    int result = AiShootAt(col, row, &ship);
    if (result == 1 && ship)
    {
        if (ship->exists)
        {
            ai_memory.state = AI_STATE_SEEK;
            ai_memory.origin_col = col;
            ai_memory.origin_row = row;
            ai_memory.last_col = col;
            ai_memory.last_row = row;
            ShuffleDirections(ai_memory.directions_order);
            ai_memory.directions_index = 0;
        }
        else
        {
            ResetAiMemory();
        }
    }
}

static void AiHandleSeek(void)
{
    while (ai_memory.directions_index < 4)
    {
        int dir = ai_memory.directions_order[ai_memory.directions_index];
        int next_col = ai_memory.origin_col + DIRECTION_DELTAS[dir][0];
        int next_row = ai_memory.origin_row + DIRECTION_DELTAS[dir][1];

        if (!IsValidCellCoord(next_col, next_row) || AlreadyShot(next_col, next_row, map_shot))
        {
            ai_memory.directions_index++;
            continue;
        }

        ship_t* ship = NULL;
        int result = AiShootAt(next_col, next_row, &ship);
        if (result == 1 && ship)
        {
            if (ship->exists)
            {
                ai_memory.direction = dir;
                ai_memory.alternate_direction = OppositeDirection(dir);
                ai_memory.last_col = next_col;
                ai_memory.last_row = next_row;
                ai_memory.state = (dir == DIR_RIGHT || dir == DIR_LEFT) ? AI_STATE_HORIZONTAL : AI_STATE_VERTICAL;
            }
            else
            {
                ResetAiMemory();
            }
        }
        else
        {
            ai_memory.directions_index++;
        }

        return;
    }

    ResetAiMemory();
}

static void AiHandleLine(void)
{
    if (ai_memory.direction < 0)
    {
        ResetAiMemory();
        return;
    }

    int dir = ai_memory.direction;
    int next_col = ai_memory.last_col + DIRECTION_DELTAS[dir][0];
    int next_row = ai_memory.last_row + DIRECTION_DELTAS[dir][1];

    if (!IsValidCellCoord(next_col, next_row) || AlreadyShot(next_col, next_row, map_shot))
    {
        dir = ai_memory.alternate_direction;
        ai_memory.direction = dir;
        ai_memory.alternate_direction = OppositeDirection(dir);
        ai_memory.last_col = ai_memory.origin_col;
        ai_memory.last_row = ai_memory.origin_row;
        next_col = ai_memory.last_col + DIRECTION_DELTAS[dir][0];
        next_row = ai_memory.last_row + DIRECTION_DELTAS[dir][1];

        if (!IsValidCellCoord(next_col, next_row) || AlreadyShot(next_col, next_row, map_shot))
        {
            ResetAiMemory();
            return;
        }
    }

    ship_t* ship = NULL;
    int result = AiShootAt(next_col, next_row, &ship);
    if (result == 1 && ship)
    {
        if (ship->exists)
        {
            ai_memory.last_col = next_col;
            ai_memory.last_row = next_row;
        }
        else
        {
            ResetAiMemory();
        }
    }
    else
    {
        ai_memory.direction = ai_memory.alternate_direction;
        ai_memory.alternate_direction = OppositeDirection(ai_memory.direction);
        ai_memory.last_col = ai_memory.origin_col;
        ai_memory.last_row = ai_memory.origin_row;
    }
}

static int Shoot(int x, int y, int* map, int* shot)
{
    if (GetState(x, y, shot))
    {
        return 0;
    }

    SetState(x, y, shot);

    if (GetState(x, y, map))
    {
        return 1;
    }

    return 0;
}

static void RandomShooting(void)
{
    switch (ai_memory.state)
    {
    case AI_STATE_NO_TARGET:
        AiHandleNoTarget();
        break;
    case AI_STATE_SEEK:
        AiHandleSeek();
        break;
    case AI_STATE_HORIZONTAL:
    case AI_STATE_VERTICAL:
        AiHandleLine();
        break;
    }
}

static void Draw(void)
{
    ClearScreen();
    PrintDiscovered(map2, map_shot2, 0, 0);
    PrintDiscovered(map, map_shot, 60, 0);

    GoToXY(0, ROWS + 2);
    printf("Enemy fleet remaining:\n");
    for (int i = 0; i < SHIP_TYPES; ++i)
    {
        printf("  Length %d: %d\n", i + 1, enemy_ships_remaining[i]);
    }

    printf("\nYour fleet remaining:\n");
    for (int i = 0; i < SHIP_TYPES; ++i)
    {
        printf("  Length %d: %d\n", i + 1, player_ships_remaining[i]);
    }

    printf("\n");
    fflush(stdout);
}

int main(int argc, char* argv[])
{
#if defined(_WIN32) || defined(_WIN64)
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
#endif

    srand((unsigned int)time(NULL));

    memset(map, 0, sizeof(map));
    memset(map_shot, 0, sizeof(map_shot));
    memset(map2, 0, sizeof(map2));
    memset(map_shot2, 0, sizeof(map_shot2));
    memset(ships, 0, sizeof(ships));
    memset(ships2, 0, sizeof(ships2));
    last_ship = 0;
    last_ship2 = 0;
    ResetAiMemory();

    memcpy(player_ships_to_place, SHIP_TEMPLATE, sizeof(player_ships_to_place));
    memcpy(player_ships_remaining, SHIP_TEMPLATE, sizeof(player_ships_remaining));

    PlaceShips();
    memcpy(player_ships_remaining, SHIP_TEMPLATE, sizeof(player_ships_remaining));

    GenerateRandomMap(map2);

    game_state_t state = GAME_CONTINUES;

    while ((state = GameState()) == GAME_CONTINUES)
    {
        Draw();

        int x = 0;
        int y = 0;
        int col = 0;
        int row = 0;

        while (1)
        {
            printf("Enter target coordinates (e.g. A5): ");
            fflush(stdout);

            if (!GetCoordinates(&x, &y))
            {
                printf("Invalid coordinates. Try again.\n");
                continue;
            }

            col = x - 1;
            row = y - 1;

            if (GetState(col, row, map_shot2))
            {
                printf("You already fired at that location. Try again.\n");
                continue;
            }

            break;
        }

        if (Shoot(col, row, map2, map_shot2))
        {
            HitShip(ships2, enemy_ships_remaining, col, row, map_shot2);
        }

        state = GameState();
        if (state != GAME_CONTINUES)
        {
            break;
        }

        RandomShooting();
    }

    RevealSurroundings(0, 0, 10, 10, map_shot2);
    Draw();

    if (state == PLAYER_WON)
    {
        printf("Congrats! You sunk the enemy fleet.\n");
    }
    else if (state == AI_WON)
    {
        printf("The computer sunk your fleet. Better luck next time.\n");
    }

    return 0;
}

