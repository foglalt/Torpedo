#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>

#if (defined _WIN32 || defined _WIN64)
    #include <Windows.h>   
#else
    #error "Not on windows, can't compile"
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

static int SHIP_COUNTS[SHIP_TYPES] = { SHIP_L1, SHIP_L2, SHIP_L3, SHIP_L4 };
static int SHIP_COUNTS2[SHIP_TYPES] = { SHIP_L1, SHIP_L2, SHIP_L3, SHIP_L4 };

HANDLE hConsole;
void ClearScreen()
{
    COORD coord = { 0, 0 };
    DWORD count;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    GetConsoleScreenBufferInfo(hConsole, &csbi);
    FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X * csbi.dwSize.Y, coord, &count);
    SetConsoleCursorPosition(hConsole, coord);
}

void GoToXY(int x, int y)
{
    COORD coord;
    coord.X = x;
    coord.Y = y;

    SetConsoleCursorPosition(hConsole, coord);
}

void GetConsoleSize(int* rows, int* cols)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
}

int ShipsLeft()
{
    int total = 0;
    for (int i = 0; i < SHIP_TYPES; i++)
    {
        total += SHIP_COUNTS2[i];
    }
    return total;
}

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

int GameState()
{
    for (int i = 0; i < SHIP_NUMBER; i++)
    {
        if (ships2[i].exists)
        {
            return 0;
        }
    }

    return 1;
}

void DebugShip(ship_t* ship)
{
    printf("Ship at 0x%x\n", ship);
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
            SHIP_COUNTS[length - 1]--;
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
        
        while (SHIP_COUNTS[i] != 0)
        {
            printf("You have %d ships left of length %d", SHIP_COUNTS[i] , length);
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

void RandomPlaceShip(int length, int* array)
{
    static int dr = 0;
    dr = ~dr;
    /*if (length > 1)
    {
        srand(time(NULL));
        dr = (rand() + (unsigned)time(NULL)) & 0x1;
    }*/
    int x, y, ok;
    do
    {
        if (dr)
        {
            x = (rand() % 10) + 1;
            y = (rand() % (11 - length)) + 1;
            ok = Check(x - 2, y - 2, 3, length + 2, array) == 0;
        }
        else
        {
            x = (rand() % (11 - length)) + 1;
            y = (rand() % 10) + 1;
            ok = Check(x - 2, y - 2, length + 2, 3, array) == 0;
        }
    } while (!ok);

    if (dr)
    {
        for (int k = y; k < y + length; k++)
        {
            SetState(x - 1, k - 1, array);
        }
    }
    else
    {
        for (int k = x; k < x + length; k++)
        {
            SetState(k - 1, y - 1, array);
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

    ships2[last_ship2++] = ship;
}

void GenerateRandomMap(int* map)
{
    int length;
    for (int i = 0; i < SHIP_TYPES; i++)
    {
        length = i + 1;
        while (SHIP_COUNTS2[i] != 0)
        {
            RandomPlaceShip(length, map);
            SHIP_COUNTS2[i]--;
        }
        SHIP_COUNTS2[i] = SHIP_TYPES - i;
    }
}

ship_t* Hit(int x, int y, int* arr)
{
    ship_t* hit = 0;
    for (int i = 0; i < SHIP_NUMBER; i++)
    {
        ship_t* tmp = &ships2[i];
        if (tmp->exists)
        {
            int idx = InShip(tmp, x, y);
            if (idx > -1)
            {
                hit = tmp;
                hit->state |= (0x1 << (3 - idx));
                break;
            }
        }
    }

    if (hit)
    {
        if ((hit->state & SHIP_DESTROYED_MASK) == SHIP_DESTROYED_MASK)
        {
            SHIP_COUNTS2[hit->length]--;
            hit->exists = 0;
            if (hit->direction)
            {
                RevealSurroundings(hit->x - 1, hit->y - 1, 3, length(hit) + 2, arr);
            }
            else
            {
                RevealSurroundings(hit->x - 1, hit->y - 1, length(hit) + 2, 3, arr);
            }
        }
    }

    return hit;
}

int Shoot(int x, int y, int* map, int* shot)
{
    int hit = 0;
    if (!GetState(x, y, shot) && GetState(x, y, map))
    {
        Hit(x, y, shot);
        hit = 1;
    }
    SetState(x, y, shot);
    return hit;
}

char GetDirections(int x, int y, int* map_shot)
{
    char d = 0b1111;//le, bal, fel, jobb
    if (x == 0)
    {
        d &= 0b1011;
    }
    else
    {
        d &= (0b1011 | ((GetState(x - 1, y, map_shot) & 0x1) << 2));
    }

    if (x == 9)
    {
        d &= 0b1110;
    }
    else
    {
        d &= (0b1110 | ((GetState(x + 1, y, map_shot) & 0x1) << 0));
    }

    if (y == 0)
    {
        d &= 0b0111;
    }
    else
    {
        d &= (0b0111 | ((GetState(x, y - 1, map_shot) & 0x1) << 3));
    }

    if (y == 9)
    {
        d &= 0b1101;
    }
    else
    {
        d &= (0b1101 | GetState(x, y + 1, map_shot) << 1);
    }
    
    return d;
}

#define NO_TARGET 0
#define RANDOM 1
#define HORIZONTAL 2
#define VERTICAL 3
void RandomShooting()
{
    static int shoot_state = NO_TARGET;
    static int index = -1, x = -1, y = -1;
    static int beginX, beginY;

    if (shoot_state == NO_TARGET)
    {
        //shoot, if hit, random
        do
        {
            srand(time(NULL));
            index = rand() % CELLS;
            x = index / ROWS;
            y = index % ROWS;
            beginX = x;
            beginY = y;
        } while (!GetState(x, y, map_shot));

        ship_t* ship = Hit(x, y, map);
        SetState(x, y, map_shot);
        if (ship && ship->exists)
        {
            shoot_state = RANDOM;
        }
    }
    else if (shoot_state == RANDOM)
    {
        static char directions_shot = 0;

        char possible_direction = GetDirections(beginX, beginY, map_shot);

        int direction;
        do
        {
            srand(time(NULL));
            direction = rand() % 4;
            if (!(0x1 << direction & possible_direction))
            {
                direction = -1;
            }

        } while (direction == -1 || (directions_shot & (0x1 << direction)));

        if (direction == 0)
        {
            ship_t* ship = Hit(x + 1, y, map);
            SetState(x + 1, y, map_shot);
            if (ship && ship->exists)
            {
                shoot_state = HORIZONTAL;
            }
            else
            {
                shoot_state = NO_TARGET;
            }
        }
        else if (direction == 1)
        {
            ship_t* ship = Hit(x, y - 1, map);
            SetState(x, y - 1, map_shot);
            if (ship && ship->exists)
            {
                shoot_state = VERTICAL;
            }
            else
            {
                shoot_state = NO_TARGET;
            }
        }
        else if (direction == 2)
        {
            ship_t* ship = Hit(x - 1, y, map);
            SetState(x - 1, y, map_shot);
            if (ship && ship->exists)
            {
                shoot_state = HORIZONTAL;
            }
            else
            {
                shoot_state = NO_TARGET;
            }
        }
        else
        {
            ship_t* ship = Hit(x, y + 1, map);
            SetState(x, y + 1, map_shot);
            if (ship && ship->exists)
            {
                shoot_state = VERTICAL;
            }
            else
            {
                shoot_state = NO_TARGET;
            }
        }
        //shoot 4 dirs, if hit, select dir
    }
    else if (shoot_state == HORIZONTAL)
    {

    }
    else
    {

    }
    switch (shoot_state)
    {
    case NO_TARGET:
        

        break;
    case RANDOM:
        
        break;
    case HORIZONTAL:

        //LEFT, RIGHT
        break;
    case VERTICAL:
        //UP, DOWN
        break;
    }

}

void Draw()
{
    ClearScreen();
    PrintDiscovered(map2, map_shot2, 0, 0);
    PrintDiscovered(map, map_shot, 60, 0);
    memset(map_shot, 0xFF, sizeof(int) * STORAGE_SIZE);
    int rows, cols;
    GetConsoleSize(&rows, &cols);
    
    for (int i = 0; i < SHIP_TYPES; i++)
    {
        GoToXY(cols - 20, i);
        printf("Length %d : %d left\n", i + 1, SHIP_COUNTS2[i]);
    }
    GoToXY(0, 11);
}

int main(int argc, char* argv[])
{
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    PlaceShips();
    GenerateRandomMap(map2);

    int x, y;
    while (!GameState())
    {
        Draw();

        while (!GetCoordinates(&x, &y));
        Shoot(x - 1, y - 1, map2, map_shot2);
        RandomShooting();
    }

    RevealSurroundings(0, 0, 10, 10, map_shot2);
    Draw();
    printf("Congrats bro u won\n");
    
    return 0;
}

