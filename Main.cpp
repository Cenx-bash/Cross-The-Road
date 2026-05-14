/*
 ╔══════════════════════════════════════════════════════════════════════╗
 ║         CROSSY ROAD — C++ Terminal Edition  v7.0  🐔                  ║
 ║    Clean Renderer    |  20 Visible Lanes                              ║
 ║    Linked-List World |  Leaderboard System                            ║
 ║    3 Lives + Invincibility   |  Streak & Speed Milestones            ║
 ║    Night Mode  |  Pause / Restart  |  Terminal Bell Sound            ║
 ╚══════════════════════════════════════════════════════════════════════╝

  Build:  g++ -std=c++17 -O2 -o crossy crossy_road.cpp && ./crossy
*/

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>
#include <termios.h>
#include <unistd.h>
#include <csignal>
#include <cmath>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════
//  SECTION 1: Terminal Helpers
// ═══════════════════════════════════════════════════════════════════
namespace Term
{
    struct termios orig_attr;
    bool raw_active = false;

    void raw()
    {
        tcgetattr(STDIN_FILENO, &orig_attr);
        struct termios t = orig_attr;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN]  = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        raw_active = true;
    }

    void restore()
    {
        if (raw_active)
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &orig_attr);
            raw_active = false;
        }
    }

    void canonical()
    {
        struct termios t = orig_attr;
        t.c_lflag |= (ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        raw_active = false;
    }

    void clear()        { std::cout << "\033[2J\033[H"; }
    void home()         { std::cout << "\033[H"; }
    void hide_cursor()  { std::cout << "\033[?25l"; }
    void show_cursor()  { std::cout << "\033[?25h"; }
    void reset_attr()   { std::cout << "\033[0m"; }

    std::string sfg(int n)     { return "\033[38;5;" + std::to_string(n) + "m"; }
    std::string sbg(int n)     { return "\033[48;5;" + std::to_string(n) + "m"; }
    std::string sreset()       { return "\033[0m"; }
    std::string sbold()        { return "\033[1m"; }

    int read_key()
    {
        unsigned char buf[8] = {};
        int n = (int)read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) return 0;
        if (n >= 3 && buf[0] == 27 && buf[1] == '[')
        {
            if (buf[2] == 'A') return 1000; // Up
            if (buf[2] == 'B') return 1001; // Down
            if (buf[2] == 'C') return 1002; // Right
            if (buf[2] == 'D') return 1003; // Left
        }
        if (n == 1 && buf[0] == 27) return 1004; // ESC
        return (int)buf[0];
    }

    void beep_move()  { std::cout << "\a"; std::cout.flush(); }
    void beep_hit()
    {
        std::cout << "\a"; std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        std::cout << "\a"; std::cout.flush();
    }
    void beep_death()
    {
        for (int i = 0; i < 3; i++)
        {
            std::cout << "\a"; std::cout.flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        }
    }
    void beep_score()  { std::cout << "\a"; std::cout.flush(); }
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 2: Game Constants
// ═══════════════════════════════════════════════════════════════════

static const int BW            = 44;   // board width in cells
static const int VISIBLE_LANES = 20;
static const int LANE_H        = 2;    // rows per lane (top + front)
static const int PLAY_ROWS     = VISIBLE_LANES * LANE_H;
static const int PLAYER_LANE   = 4;    // slot from bottom of viewport
static const int AHEAD_BUFFER  = VISIBLE_LANES + 6;
static const int BELOW_BUFFER  = VISIBLE_LANES + 6;
static const int MAX_LIVES     = 3;

// HUD: 3 rows top + 1 info row = 4 rows; Footer: divider + controls + bottom = 3 rows
static const int HUD_ROWS    = 4;
static const int FOOTER_ROWS = 3;
// Total screen width: borders (2) + board (BW)
static const int BORDER       = 1;   // one char each side
static const int INNER_W      = BW;  // printable content width
static const int FRAME_W      = INNER_W + 2; // includes left/right border chars

static const std::string LEADERBOARD_FILE       = "leaderboard.csv";
static const int         MAX_LEADERBOARD_ENTRIES = 10;

enum LaneType { SAFE, GRASS, ROAD, WATER };

// ═══════════════════════════════════════════════════════════════════
//  SECTION 3: Leaderboard
// ═══════════════════════════════════════════════════════════════════

struct LeaderboardEntry { std::string name; int score; };

std::vector<LeaderboardEntry> loadLeaderboard()
{
    std::vector<LeaderboardEntry> entries;
    std::ifstream file(LEADERBOARD_FILE);
    if (!file.is_open()) return entries;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty()) continue;
        auto pos = line.rfind(',');
        if (pos == std::string::npos) continue;
        LeaderboardEntry e;
        e.name = line.substr(0, pos);
        try { e.score = std::stoi(line.substr(pos + 1)); }
        catch (...) { continue; }
        entries.push_back(e);
    }
    return entries;
}

void saveLeaderboard(const std::vector<LeaderboardEntry> &entries)
{
    std::ofstream file(LEADERBOARD_FILE);
    if (!file.is_open()) return;
    for (const auto &e : entries)
        file << e.name << "," << e.score << "\n";
}

int submitScore(const std::string &name, int score)
{
    auto entries = loadLeaderboard();
    bool found = false;
    for (auto &e : entries)
        if (e.name == name) { if (score > e.score) e.score = score; found = true; break; }
    if (!found) entries.push_back({name, score});
    std::sort(entries.begin(), entries.end(),
              [](const LeaderboardEntry &a, const LeaderboardEntry &b){ return a.score > b.score; });
    int rank = -1;
    for (int i = 0; i < (int)entries.size(); ++i)
        if (entries[i].name == name) { rank = i + 1; break; }
    if ((int)entries.size() > MAX_LEADERBOARD_ENTRIES)
        entries.resize(MAX_LEADERBOARD_ENTRIES);
    saveLeaderboard(entries);
    return rank;
}

void displayLeaderboard()
{
    auto entries = loadLeaderboard();
    std::cout << Term::sfg(220) << "\033[1m";
    std::cout << "\n  ╔═══════╦══════════════════════╦════════════╗\n";
    std::cout << "  ║  RANK ║ PLAYER               ║ SCORE      ║\n";
    std::cout << "  ╠═══════╬══════════════════════╬════════════╣\n";
    std::cout << Term::sreset();
    if (entries.empty())
    {
        std::cout << Term::sfg(240) << "  ║   --  ║ No scores yet        ║     --     ║\n" << Term::sreset();
    }
    else
    {
        int rankColors[] = {220, 248, 130};
        for (int i = 0; i < (int)entries.size(); ++i)
        {
            int col = (i < 3) ? rankColors[i] : 245;
            std::string rankStr = " #" + std::to_string(i + 1);
            while ((int)rankStr.size() < 6) rankStr += " ";
            std::string nameStr = " " + entries[i].name;
            while ((int)nameStr.size() < 21) nameStr += " ";
            std::string scoreStr = " " + std::to_string(entries[i].score);
            while ((int)scoreStr.size() < 11) scoreStr += " ";
            std::cout << Term::sfg(col) << "  ║" << rankStr << " ║" << nameStr << " ║" << scoreStr << " ║\n" << Term::sreset();
        }
    }
    std::cout << Term::sfg(220) << "\033[1m"
              << "  ╚═══════╩══════════════════════╩════════════╝\n\n"
              << Term::sreset();
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 4: Singly-Linked List
// ═══════════════════════════════════════════════════════════════════

template<typename T>
struct SLNode { T data; SLNode *next = nullptr; explicit SLNode(const T &d) : data(d) {} };

template<typename T>
struct SLinkedList
{
    SLNode<T> *head = nullptr;
    SLNode<T> *tail = nullptr;
    int size = 0;
    SLinkedList() = default;
    SLinkedList(const SLinkedList &) = delete;
    SLinkedList &operator=(const SLinkedList &) = delete;
    ~SLinkedList() { clear(); }

    void push_back(const T &d)
    {
        auto *n = new SLNode<T>(d);
        if (!tail) head = tail = n;
        else { tail->next = n; tail = n; }
        ++size;
    }
    void clear()
    {
        while (head) { auto *t = head; head = head->next; delete t; }
        tail = nullptr; size = 0;
    }
};

// ═══════════════════════════════════════════════════════════════════
//  SECTION 5: Obstacle & Lane
// ═══════════════════════════════════════════════════════════════════

struct Obstacle
{
    double x; int width; double speed; int vtype; int colorFg; int colorBg;
    bool contains(double px) const { return px >= x && px < x + (double)width; }
    int iLeft()  const { return (int)x; }
    int iRight() const { return (int)(x + width); }
};

struct Lane
{
    LaneType type;
    SLinkedList<Obstacle> obs;
    Lane *prev = nullptr;
    Lane *next = nullptr;
    explicit Lane(LaneType t) : type(t) {}
    Lane(const Lane &) = delete;
    Lane &operator=(const Lane &) = delete;
};

// ═══════════════════════════════════════════════════════════════════
//  SECTION 6: World (doubly-linked lane list)
// ═══════════════════════════════════════════════════════════════════

struct World
{
    Lane *head = nullptr;   // highest absY
    Lane *tail = nullptr;   // lowest absY (= baseY)
    int count = 0;
    int baseY = -1;

    World() = default;
    ~World() { destroyAll(); }
    World(const World &) = delete;
    World &operator=(const World &) = delete;

    void destroyAll()
    {
        Lane *cur = head;
        while (cur) { Lane *n = cur->next; delete cur; cur = n; }
        head = tail = nullptr; count = 0; baseY = -1;
    }

    int headAbsY() const { return (count > 0) ? baseY + (count - 1) : -1; }

    void pushTop(Lane *l)
    {
        l->next = head; l->prev = nullptr;
        if (head) head->prev = l; else tail = l;
        head = l; ++count;
        if (count == 1) baseY = 0;
    }

    void popBottom()
    {
        if (!tail) return;
        Lane *t = tail; tail = t->prev;
        if (tail) tail->next = nullptr; else head = nullptr;
        delete t; --count; ++baseY;
    }

    Lane *laneAt(int absY) const
    {
        if (count == 0) return nullptr;
        int tailAY = baseY, headAY = baseY + (count - 1);
        if (absY < tailAY || absY > headAY) return nullptr;
        int fromTail = absY - tailAY, fromHead = headAY - absY;
        if (fromTail <= fromHead)
        {
            Lane *cur = tail;
            for (int i = 0; i < fromTail; ++i) cur = cur->prev;
            return cur;
        }
        Lane *cur = head;
        for (int i = 0; i < fromHead; ++i) cur = cur->next;
        return cur;
    }
};

// ═══════════════════════════════════════════════════════════════════
//  SECTION 7: RNG
// ═══════════════════════════════════════════════════════════════════

static int    rng(int lo, int hi)       { return lo + rand() % (hi - lo + 1); }
static double rngf(double lo, double hi){ return lo + (hi - lo) * (rand() / (double)RAND_MAX); }

// ═══════════════════════════════════════════════════════════════════
//  SECTION 8: Global Game State
// ═══════════════════════════════════════════════════════════════════

struct GameState { int laneGen = 0; bool nightMode = false; int speedTier = 0; int totalMoves = 0; bool soundOn = true; };
static GameState g_state;

// ═══════════════════════════════════════════════════════════════════
//  SECTION 9: Lane Factory
// ═══════════════════════════════════════════════════════════════════

static const int CAR_COLORS[][2] = {
    {196,52},{202,58},{226,100},{46,22},{51,23},{201,90},{255,238},{21,17}
};
static const int NUM_CAR_COLORS = 8;

Lane *makeLane(int idx)
{
    int rel = ((idx % 16) + 16) % 16;
    LaneType type;
    if      (rel == 0 || rel == 15)           type = SAFE;
    else if (rel == 1 || rel == 2 || rel == 14) type = GRASS;
    else if (rel >= 3 && rel <= 6)            type = ROAD;
    else if (rel == 7)                        type = GRASS;
    else if (rel >= 8 && rel <= 9)            type = WATER;
    else if (rel == 10)                       type = GRASS;
    else if (rel >= 11 && rel <= 14)          type = ROAD;
    else                                      type = GRASS;

    Lane *l = new Lane(type);
    double speedMult = 1.0 + g_state.speedTier * 0.40;

    if (type == ROAD)
    {
        double spd = rngf(0.18, 0.50) * speedMult;
        if (rand() % 2) spd = -spd;
        int numCars = rng(2, 5), carW = rng(3, 6);
        double minGap = std::max(5.0, 8.0 / speedMult);
        double pos = rngf(0.0, 5.0);
        int ci = rng(0, NUM_CAR_COLORS - 1);
        for (int i = 0; i < numCars; ++i)
        {
            Obstacle o; o.x = pos; o.width = carW; o.speed = spd; o.vtype = 0;
            o.colorFg = CAR_COLORS[ci][0]; o.colorBg = CAR_COLORS[ci][1];
            l->obs.push_back(o);
            pos += carW + minGap + rngf(0, 5);
        }
    }
    else if (type == WATER)
    {
        double spd = rngf(0.10, 0.28) * speedMult;
        if (rand() % 2) spd = -spd;
        int numLogs = rng(2, 5), logW = (int)rngf(5.0, 9.0);
        double gap = rngf(3.5, 7.0), pos = rngf(0.0, 4.0);
        for (int i = 0; i < numLogs; ++i)
        {
            Obstacle o; o.x = pos; o.width = logW; o.speed = spd; o.vtype = 1;
            o.colorFg = 130; o.colorBg = 94;
            l->obs.push_back(o);
            pos += logW + gap;
            if (pos > BW) pos -= BW;
        }
    }
    return l;
}

void buildInitialWorld(World &w)
{
    g_state.laneGen = 0;
    Lane *l = makeLane(g_state.laneGen++);
    w.head = w.tail = l; w.count = 1; w.baseY = 0;
    for (int i = 1; i <= AHEAD_BUFFER; ++i)
    {
        Lane *nl = makeLane(g_state.laneGen++);
        nl->next = w.head; nl->prev = nullptr;
        if (w.head) w.head->prev = nl;
        w.head = nl; ++w.count;
    }
}

void ensureLanes(World &w, int playerAbsY)
{
    int needed_high = playerAbsY + AHEAD_BUFFER;
    int needed_low  = playerAbsY - BELOW_BUFFER;
    while (w.headAbsY() < needed_high)
    {
        Lane *l = makeLane(g_state.laneGen++);
        l->next = w.head; l->prev = nullptr;
        if (w.head) w.head->prev = l; else w.tail = l;
        w.head = l; ++w.count;
    }
    while (w.count > 0 && w.baseY < needed_low)
        w.popBottom();
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 10: Physics
// ═══════════════════════════════════════════════════════════════════

void tickLane(Lane *l)
{
    if (!l) return;
    double W = (double)BW;
    auto *cur = l->obs.head;
    while (cur)
    {
        Obstacle &o = cur->data;
        o.x += o.speed;
        if (o.speed > 0 && o.x >= W)         o.x -= W + o.width;
        if (o.speed < 0 && o.x + o.width <= 0.0) o.x += W + o.width;
        cur = cur->next;
    }
}

Obstacle *findObs(Lane *l, double px)
{
    if (!l) return nullptr;
    auto *cur = l->obs.head;
    while (cur) { if (cur->data.contains(px)) return &cur->data; cur = cur->next; }
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 11: Player
// ═══════════════════════════════════════════════════════════════════

struct Player
{
    double px       = BW / 2.0;
    int    absY     = 0;
    bool   alive    = true;
    int    score    = 0;
    int    lives    = MAX_LIVES;   // starts at 3
    int    streak   = 0;
    int    maxStreak= 0;
    double logAccum = 0.0;
    int    jumpFrame= 0;
    bool   invincible    = false;
    int    invincFrames  = 0;
    bool   hitFlash      = false;
};

// Returns true if fully dead (no lives left)
bool loseLife(Player &p)
{
    p.lives--;
    if (p.lives <= 0)
    {
        p.alive = false;
        p.lives = 0;
        return true;
    }
    // Respawn at origin with brief invincibility
    p.px          = BW / 2.0;
    p.absY        = 0;
    p.logAccum    = 0.0;
    p.invincible  = true;
    p.invincFrames= 60;
    p.hitFlash    = true;
    return false;
}

void tryMove(World &w, Player &p, int dx, int dy, bool &soundHit, bool &soundMove)
{
    double newPx   = p.px + dx;
    int    newAbsY = p.absY + dy;

    if (newPx < 0.0)         newPx = 0.0;
    if (newPx > BW - 1.0)   newPx = BW - 1.0;

    Lane *tgt = w.laneAt(newAbsY);
    if (!tgt) return;

    p.px    = newPx;
    p.absY  = newAbsY;
    p.logAccum = 0.0;

    bool died = false;
    if (tgt->type == ROAD && !p.invincible && findObs(tgt, newPx))
    {
        p.hitFlash = true; died = loseLife(p); soundHit = true;
    }
    else if (tgt->type == WATER && !p.invincible && !findObs(tgt, newPx))
    {
        p.hitFlash = true; died = loseLife(p); soundHit = true;
    }

    if (!died && p.alive)
    {
        p.jumpFrame = 1;
        soundMove   = true;
        if (dy > 0)
        {
            p.streak++;
            if (p.streak > p.maxStreak) p.maxStreak = p.streak;
            g_state.totalMoves++;
        }
        else if (dy < 0) p.streak = 0;
    }
    if (p.absY > p.score) p.score = p.absY;
}

void physTick(World &w, Player &p, bool &soundHit)
{
    if (!p.alive) return;

    if (p.invincible)
    {
        p.invincFrames--;
        if (p.invincFrames <= 0) { p.invincible = false; p.hitFlash = false; }
    }

    for (int ay = p.absY - VISIBLE_LANES; ay <= p.absY + VISIBLE_LANES; ++ay)
        tickLane(w.laneAt(ay));

    Lane *cur = w.laneAt(p.absY);

    if (cur && cur->type == WATER)
    {
        Obstacle *log = findObs(cur, p.px);
        if (!log && !p.invincible)
        {
            p.hitFlash = true; loseLife(p); soundHit = true; return;
        }
        if (log)
        {
            p.logAccum += log->speed;
            int drift = (int)p.logAccum; p.logAccum -= drift;
            p.px += drift;
            if (p.px < 0.0 || p.px >= BW)
            {
                if (!p.invincible)
                {
                    p.hitFlash = true; loseLife(p); soundHit = true; return;
                }
                p.px = std::max(0.0, std::min((double)BW - 1.0, p.px));
            }
            if (!findObs(cur, p.px) && !p.invincible)
            {
                p.hitFlash = true; loseLife(p); soundHit = true; return;
            }
        }
    }
    else { p.logAccum = 0.0; }

    if (cur && cur->type == ROAD && !p.invincible && findObs(cur, p.px))
    {
        p.hitFlash = true; loseLife(p); soundHit = true;
    }

    if (p.jumpFrame > 0) p.jumpFrame = (p.jumpFrame + 1) % 4;

    int newTier = std::min(2, p.score / 30);
    if (newTier > g_state.speedTier) g_state.speedTier = newTier;
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 12: Renderer
// ═══════════════════════════════════════════════════════════════════

// Count visible (non-escape) characters in a string
static int visLen(const std::string &s)
{
    int len = 0; bool esc = false;
    for (unsigned char c : s)
    {
        if (c == '\033') { esc = true; continue; }
        if (esc) { if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) esc = false; continue; }
        // count multi-byte UTF-8 as one visible char
        if ((c & 0xC0) != 0x80) len++;
    }
    return len;
}

struct ScreenBuf
{
    int rows, cols;
    std::vector<std::string> lines;
    ScreenBuf(int r, int c) : rows(r), cols(c), lines(r) {}
    void flush()
    {
        std::string out; out.reserve(rows * cols * 14);
        out += "\033[H";
        for (int r = 0; r < rows; ++r)
            out += lines[r] + "\033[0m\n";
        std::cout << out; std::cout.flush();
    }
};

struct LP { int topBg, topFg, frontBg, frontFg; };

LP lanePalette(LaneType t, int depth, bool night)
{
    LP p{};
    switch (t)
    {
    case SAFE:
        p.topBg = night ? 236 : 239; p.topFg = night ? 244 : 252;
        p.frontBg= night ? 233 : 235; p.frontFg= night ? 240 : 245;
        break;
    case GRASS:
        p.topBg = night ? 22 : (depth < 4 ? 28 : 22);
        p.topFg = night ? 28 : (depth < 4 ? 34 : 28);
        p.frontBg= night ? 22 : 22;
        p.frontFg= night ? 28 : (depth < 4 ? 28 : 22);
        break;
    case ROAD:
        p.topBg = night ? 237 : 241; p.topFg = night ? 244 : 248;
        p.frontBg= night ? 233 : 236; p.frontFg= night ? 240 : 243;
        break;
    case WATER:
        p.topBg = night ? 17 : (depth < 3 ? 27 : (depth < 7 ? 20 : 17));
        p.topFg = night ? 27 : (depth < 3 ? 45 : (depth < 7 ? 39 : 27));
        p.frontBg= night ? 17 : (depth < 3 ? 20 : 17);
        p.frontFg= night ? 20 : (depth < 3 ? 33 : 20);
        break;
    }
    return p;
}

void renderLane(
    std::string &rowTop, std::string &rowFront,
    Lane *lane, LaneType type, int depth,
    const Player &p, bool playerOnThisLane,
    bool night, int tick)
{
    LP pal = lanePalette(type, depth, night);

    struct TI { bool obs=false; int vtype=-1; int fg=0,bg=0; bool edgeL=false,edgeR=false; };
    std::vector<TI> tiles(BW);

    if (lane)
    {
        auto *cur = lane->obs.head;
        while (cur)
        {
            const Obstacle &o = cur->data;
            for (int c = o.iLeft(); c < o.iRight() && c < BW; ++c)
            {
                if (c < 0) continue;
                tiles[c] = {true, o.vtype, o.colorFg, o.colorBg, c == o.iLeft(), c == o.iRight()-1};
            }
            cur = cur->next;
        }
    }

    int borderBg = night ? 232 : 234;
    rowTop   += Term::sbg(borderBg) + Term::sfg(238) + "│" + Term::sreset();
    rowFront += Term::sbg(borderBg) + Term::sfg(238) + "│" + Term::sreset();

    for (int c = 0; c < BW; ++c)
    {
        const TI &ti = tiles[c];
        bool isPlayer = playerOnThisLane && (c == (int)p.px);

        // ── Top row ──────────────────────────────────────────
        if (isPlayer)
        {
            if (!p.alive)
                rowTop += Term::sbg(52) + Term::sfg(196) + "\033[1m☠\033[0m" + Term::sreset();
            else if (p.invincible && (p.invincFrames % 6 < 3))
                rowTop += Term::sbg(255) + Term::sfg(226) + "★" + Term::sreset();
            else
                rowTop += Term::sbg(p.jumpFrame > 0 ? 226 : 220) + Term::sfg(214) + "▄" + Term::sreset();
        }
        else if (ti.obs)
        {
            if (ti.vtype == 0) // car
            {
                rowTop += (ti.edgeL || ti.edgeR)
                    ? Term::sbg(248) + Term::sfg(255) + "▄" + Term::sreset()
                    : Term::sbg(246) + Term::sfg(250) + "▄" + Term::sreset();
            }
            else // log
            {
                int logBg = (depth < 4) ? 94 : 58;
                rowTop += (ti.edgeL || ti.edgeR)
                    ? Term::sbg(logBg) + Term::sfg(136) + "▐" + Term::sreset()
                    : Term::sbg((c%2==0)?logBg:logBg) + Term::sfg(130) + "▄" + Term::sreset();
            }
        }
        else
        {
            switch (type)
            {
            case WATER: {
                int wave = (c + tick/3) % 5;
                int wbg2 = (pal.topBg == 27) ? 33 : (pal.topBg == 20) ? 26 : 18;
                rowTop += Term::sbg(wave == 0 ? wbg2 : pal.topBg) + Term::sfg(pal.topFg) + (wave == 0 ? "≈" : "▄") + Term::sreset();
                break;
            }
            case GRASS:
                rowTop += Term::sbg(pal.topBg) + Term::sfg(pal.topFg) + (c%4==0 ? "▓" : "▒") + Term::sreset();
                break;
            case ROAD:
                if (c == BW/2 && (tick/4)%3 < 2)
                    rowTop += Term::sbg(pal.topBg) + Term::sfg(255) + "─" + Term::sreset();
                else
                    rowTop += Term::sbg(pal.topBg) + Term::sfg(pal.topFg) + "▄" + Term::sreset();
                break;
            case SAFE:
                rowTop += Term::sbg(pal.topBg) + Term::sfg(pal.topFg) + ((c+depth)%3==0 ? "░" : " ") + Term::sreset();
                break;
            }
        }

        // ── Front row ─────────────────────────────────────────
        if (isPlayer)
        {
            if (!p.alive)
                rowFront += Term::sbg(52)  + Term::sfg(196) + "\033[1m▓\033[0m" + Term::sreset();
            else if (p.invincible && (p.invincFrames % 6 < 3))
                rowFront += Term::sbg(255) + Term::sfg(214) + "\033[1m█\033[0m" + Term::sreset();
            else
            {
                int bodyBg = (p.jumpFrame > 0) ? 214 : 208;
                rowFront += Term::sbg(bodyBg) + Term::sfg(220) + "\033[1m█\033[0m" + Term::sreset();
            }
        }
        else if (ti.obs)
        {
            if (ti.vtype == 0) // car
            {
                if (ti.edgeL)       rowFront += Term::sbg(226) + Term::sfg(255) + "▌" + Term::sreset();
                else if (ti.edgeR)  rowFront += Term::sbg(124) + Term::sfg(196) + "▐" + Term::sreset();
                else                rowFront += Term::sbg(ti.bg) + Term::sfg(ti.fg) + "█" + Term::sreset();
            }
            else // log
            {
                int lfBg = (depth < 4) ? 94 : 58;
                int lfFg = (depth < 4) ? 130 : 94;
                rowFront += Term::sbg(lfBg) + Term::sfg(lfFg) + (c%3==1 ? "▒" : "▓") + Term::sreset();
            }
        }
        else
        {
            switch (type)
            {
            case WATER: rowFront += Term::sbg(pal.frontBg) + Term::sfg(pal.frontFg) + (c%3==1 ? "▒" : "█") + Term::sreset(); break;
            case GRASS: rowFront += Term::sbg(pal.frontBg) + Term::sfg(pal.frontFg) + (c%4==0 ? "▒" : "█") + Term::sreset(); break;
            case ROAD:  rowFront += Term::sbg(pal.frontBg) + Term::sfg(pal.frontFg) + "█" + Term::sreset(); break;
            case SAFE:  rowFront += Term::sbg(pal.frontBg) + Term::sfg(pal.frontFg) + (c%3==0 ? "░" : "█") + Term::sreset(); break;
            }
        }
    }

    rowTop   += Term::sbg(borderBg) + Term::sfg(238) + "│" + Term::sreset();
    rowFront += Term::sbg(borderBg) + Term::sfg(238) + "│" + Term::sreset();
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 13: HUD & Footer helpers
// ═══════════════════════════════════════════════════════════════════

// Pad a line with spaces so its visible length == FRAME_W - 2, then add right border
static void finishLine(std::string &ln, int borderBg)
{
    int target = FRAME_W - 2; // subtract left and right border chars
    int vl = visLen(ln) - 1;  // -1 for the left border char already added
    while (vl < target) { ln += Term::sbg(234) + " "; vl++; }
    ln += Term::sbg(borderBg) + Term::sfg(238) + "│" + Term::sreset();
}

static std::string hline(const char *join_l, const char *mid, const char *join_r, int borderBg)
{
    std::string s;
    s += Term::sbg(borderBg) + Term::sfg(238);
    s += join_l;
    for (int i = 0; i < FRAME_W - 2; ++i) s += mid;
    s += join_r;
    s += Term::sreset();
    return s;
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 14: Full Frame Render
// ═══════════════════════════════════════════════════════════════════

void renderFrame(const World &w, const Player &p, int hi,
                 const std::string &name, bool paused, int tick)
{
    const int totalRows = HUD_ROWS + PLAY_ROWS + FOOTER_ROWS;
    ScreenBuf buf(totalRows, FRAME_W + 2);

    int borderBg = g_state.nightMode ? 232 : 234;

    // ── HUD Row 0: top border ─────────────────────────────────────
    buf.lines[0] = hline("╔", "═", "╗", borderBg);

    // ── HUD Row 1: status bar ─────────────────────────────────────
    {
        std::string ln;
        ln += Term::sbg(borderBg) + Term::sfg(238) + "│";

        // Title
        ln += Term::sbg(borderBg) + Term::sfg(220) + "\033[1m 🐔 CROSSY\033[0m";
        ln += Term::sbg(borderBg) + "  ";

        // Player name
        ln += Term::sfg(252) + name;
        ln += Term::sbg(borderBg) + "  ";

        // Lives — always show MAX_LIVES hearts, filled or empty
        for (int i = 0; i < MAX_LIVES; ++i)
            ln += (i < p.lives) ? Term::sfg(196) + "♥" : Term::sfg(238) + "♡";
        ln += Term::sbg(borderBg) + "  ";

        // Score / Best
        ln += Term::sfg(248) + "SCR:";
        ln += Term::sfg(220) + "\033[1m" + std::to_string(p.score) + "\033[0m";
        ln += Term::sbg(borderBg) + "  ";
        ln += Term::sfg(248) + "BEST:";
        ln += Term::sfg(33)  + "\033[1m" + std::to_string(hi) + "\033[0m";
        ln += Term::sbg(borderBg) + "  ";

        // Streak
        ln += Term::sfg(248) + "STK:";
        ln += Term::sfg(40)  + "\033[1m" + std::to_string(p.streak) + "\033[0m";
        ln += Term::sbg(borderBg) + "  ";

        // Speed tier
        ln += Term::sfg(248) + "SPD:";
        if      (g_state.speedTier == 0) ln += Term::sfg(46)  + "NRM";
        else if (g_state.speedTier == 1) ln += Term::sfg(226) + "\033[1mFST\033[0m";
        else                             ln += Term::sfg(196) + "\033[1mTRB\033[0m";
        ln += Term::sbg(borderBg) + "  ";

        // Night / sound icons
        ln += (g_state.nightMode ? Term::sfg(105) + "☾" : Term::sfg(240) + "☼");
        ln += Term::sbg(borderBg) + " ";
        ln += (g_state.soundOn   ? Term::sfg(242) + "♪" : Term::sfg(242) + "✕");

        finishLine(ln, borderBg);
        buf.lines[1] = ln;
    }

    // ── HUD Row 2: divider ────────────────────────────────────────
    buf.lines[2] = hline("├", "─", "┤", borderBg);

    // ── HUD Row 3: milestone hint ─────────────────────────────────
    {
        std::string ln;
        ln += Term::sbg(borderBg) + Term::sfg(238) + "│";
        if (g_state.speedTier == 0)
        {
            ln += Term::sbg(borderBg) + Term::sfg(242) + " Speed: ";
            ln += Term::sfg(226) + "FAST@30" + Term::sfg(242) + "  ";
            ln += Term::sfg(196) + "TURBO@60";
        }
        else if (g_state.speedTier == 1)
            ln += Term::sbg(borderBg) + Term::sfg(226) + "\033[1m⚡ FAST MODE  — obstacles move quicker!\033[0m";
        else
            ln += Term::sbg(borderBg) + Term::sfg(196) + "\033[1m🔥 TURBO MODE — maximum speed!\033[0m";
        finishLine(ln, borderBg);
        buf.lines[3] = ln;
    }

    // ── Play area ─────────────────────────────────────────────────
    int playerSlot = VISIBLE_LANES - 1 - PLAYER_LANE;
    for (int sl = 0; sl < VISIBLE_LANES; ++sl)
    {
        int absY  = p.absY + (playerSlot - sl);
        int depth = std::abs(sl - playerSlot);
        Lane    *lane = w.laneAt(absY);
        LaneType type = lane ? lane->type : SAFE;
        bool     onLane = (absY == p.absY);
        int      screenRow = HUD_ROWS + sl * LANE_H;

        std::string rowTop, rowFront;
        renderLane(rowTop, rowFront, lane, type, depth, p, onLane, g_state.nightMode, tick);

        buf.lines[screenRow]     = rowTop   + Term::sreset();
        buf.lines[screenRow + 1] = rowFront + Term::sreset();
    }

    // ── Paused overlay ────────────────────────────────────────────
    if (paused)
    {
        int mid = HUD_ROWS + (VISIBLE_LANES / 2) * LANE_H;
        if (mid + 2 < totalRows)
        {
            std::string bar(FRAME_W, ' ');
            buf.lines[mid]   = Term::sbg(22) + Term::sfg(255) + "\033[1m" + "  ╔══════════════════════╗" + Term::sreset();
            buf.lines[mid+1] = Term::sbg(22) + Term::sfg(255) + "\033[1m" + "  ║   ⏸  GAME PAUSED    ║" + Term::sreset();
            buf.lines[mid+2] = Term::sbg(22) + Term::sfg(255) + "\033[1m" + "  ╚══════════════════════╝" + Term::sreset();
        }
    }

    // ── Footer: divider ───────────────────────────────────────────
    int footerBase = HUD_ROWS + PLAY_ROWS;
    buf.lines[footerBase] = hline("├", "─", "┤", borderBg);

    // ── Footer: controls line ─────────────────────────────────────
    {
        std::string ln;
        ln += Term::sbg(borderBg) + Term::sfg(238) + "│";
        auto key = [&](const std::string &k, const std::string &desc)
        {
            ln += Term::sbg(borderBg) + Term::sfg(252) + " " + k;
            ln += Term::sfg(240) + ":" + desc;
        };

        if (!p.alive)
        {
            ln += Term::sbg(borderBg) + Term::sfg(196) + "\033[1m ☠ GAME OVER\033[0m";
            key("R", "restart"); key("L", "board"); key("Q", "quit");
        }
        else if (paused)
        {
            ln += Term::sbg(borderBg) + Term::sfg(255) + "\033[1m ⏸ PAUSED\033[0m";
            key("ESC", "resume"); key("N", "night"); key("V", "sound"); key("L", "board"); key("Q", "quit");
        }
        else
        {
            key("W/↑", "fwd"); key("S/↓", "back"); key("A/←", "left"); key("D/→", "right");
            key("ESC", "pause"); key("N", "night"); key("V", "snd"); key("L", "board"); key("Q", "quit");
        }
        finishLine(ln, borderBg);
        buf.lines[footerBase + 1] = ln;
    }

    // ── Footer: bottom border ─────────────────────────────────────
    buf.lines[footerBase + 2] = hline("╚", "═", "╝", borderBg);

    buf.flush();
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 15: Input
// ═══════════════════════════════════════════════════════════════════

enum Key { K_NONE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_QUIT, K_RESTART, K_LEADERBOARD, K_PAUSE, K_NIGHT, K_SOUND };

Key pollKey()
{
    switch (Term::read_key())
    {
    case 1000: case 'w': case 'W': return K_UP;
    case 1001: case 's': case 'S': return K_DOWN;
    case 1002: case 'd': case 'D': return K_RIGHT;
    case 1003: case 'a': case 'A': return K_LEFT;
    case 'q': case 'Q': return K_QUIT;
    case 'r': case 'R': return K_RESTART;
    case 'l': case 'L': return K_LEADERBOARD;
    case 1004: case 'p': case 'P': return K_PAUSE;
    case 'n': case 'N': return K_NIGHT;
    case 'v': case 'V': return K_SOUND;
    default: return K_NONE;
    }
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 16: Game Lifecycle
// ═══════════════════════════════════════════════════════════════════

void resetGame(World &world, Player &player)
{
    world.destroyAll();
    g_state.laneGen     = 0;
    g_state.speedTier   = 0;
    g_state.totalMoves  = 0;
    player              = Player();
    buildInitialWorld(world);
    ensureLanes(world, player.absY);
}

void showLeaderboardScreen(World &world, const Player &player,
                           int hi, const std::string &name)
{
    Term::restore(); Term::show_cursor(); Term::clear();
    displayLeaderboard();
    std::cout << Term::sfg(244) << "  Press ENTER to return...\n" << Term::sreset();
    Term::canonical();
    std::string dummy; std::getline(std::cin, dummy);
    Term::raw(); Term::hide_cursor(); Term::clear();
    renderFrame(world, player, hi, name, false, 0);
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 17: Splash Screen
// ═══════════════════════════════════════════════════════════════════

std::string splash()
{
    Term::clear();
    const char *T1[] = {
        " ██████╗██████╗  ██████╗ ███████╗███████╗██╗   ██╗",
        "██╔════╝██╔══██╗██╔═══██╗██╔════╝██╔════╝╚██╗ ██╔╝",
        "██║     ██████╔╝██║   ██║███████╗███████╗ ╚████╔╝ ",
        "██║     ██╔══██╗██║   ██║╚════██║╚════██║  ╚██╔╝  ",
        "╚██████╗██║  ██║╚██████╔╝███████║███████║   ██║   ",
        " ╚═════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝╚══════╝   ╚═╝  "};
    const char *T2[] = {
        "██████╗  ██████╗  █████╗ ██████╗ ",
        "██╔══██╗██╔═══██╗██╔══██╗██╔══██╗",
        "██████╔╝██║   ██║███████║██║  ██║",
        "██╔══██╗██║   ██║██╔══██║██║  ██║",
        "██║  ██║╚██████╔╝██║  ██║██████╔╝",
        "╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═╝╚═════╝"};
    int colors[] = {196,202,208,214,220,226};
    for (int i = 0; i < 6; ++i) { std::cout << Term::sfg(colors[i]) << "  " << T1[i] << "\n"; }
    std::cout << "\n";
    for (int i = 0; i < 6; ++i) { std::cout << Term::sfg(colors[5-i]) << "  " << T2[i] << "\n"; }
    Term::reset_attr();
    std::cout << "\n";

    std::cout << Term::sfg(240)
              << "  ┌─ CONTROLS ─────────────────────────────────────────────┐\n"
              << Term::sfg(252)
              << "  │  W/↑  Forward      A/←  Left      ESC/P  Pause         │\n"
              << "  │  S/↓  Back         D/→  Right     R      Restart        │\n"
              << "  │  N    Night mode   V    Sound      L      Leaderboard    │\n"
              << "  │  Q    Quit                                               │\n"
              << Term::sfg(240)
              << "  └────────────────────────────────────────────────────────┘\n\n";

    displayLeaderboard();

    std::cout << Term::sfg(220) << "\033[1m  Enter your name: " << Term::sreset();
    std::string name; std::getline(std::cin, name);
    if (name.empty()) name = "Anonymous";
    if ((int)name.size() > 16) name = name.substr(0, 16);
    std::cout << "\n" << Term::sfg(244) << "  Welcome, " << name << "!  Press ENTER to start...\n\n" << Term::sreset();
    std::string dummy; std::getline(std::cin, dummy);
    return name;
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 18: Main Game Loop
// ═══════════════════════════════════════════════════════════════════

void runGame(const std::string &playerName)
{
    srand((unsigned)time(nullptr));

    World  world;
    Player player;
    int    hiScore = 0;
    bool   quit    = false;
    bool   paused  = false;
    int    tick    = 0;

    resetGame(world, player);

    using Clock = std::chrono::steady_clock;
    using MS    = std::chrono::milliseconds;
    const long TICK_MS  = 85;
    const long FRAME_MS = 40;

    auto lastTick  = Clock::now();
    auto lastFrame = Clock::now();

    Term::raw(); Term::hide_cursor(); Term::clear();
    renderFrame(world, player, hiScore, playerName, paused, tick);

    while (!quit)
    {
        auto now = Clock::now();
        Key  key = pollKey();

        switch (key)
        {
        case K_QUIT:
            quit = true;
            continue;
        case K_RESTART:
            if (!player.alive)
            {
                resetGame(world, player);
                paused = false; tick = 0;
                Term::clear();
                renderFrame(world, player, hiScore, playerName, paused, tick);
            }
            continue;
        case K_PAUSE:
            if (player.alive) { paused = !paused; renderFrame(world, player, hiScore, playerName, paused, tick); }
            continue;
        case K_NIGHT:
            g_state.nightMode = !g_state.nightMode;
            renderFrame(world, player, hiScore, playerName, paused, tick);
            continue;
        case K_SOUND:
            g_state.soundOn = !g_state.soundOn;
            renderFrame(world, player, hiScore, playerName, paused, tick);
            continue;
        case K_LEADERBOARD:
            showLeaderboardScreen(world, player, hiScore, playerName);
            continue;
        default: break;
        }

        if (!paused && player.alive)
        {
            int dx = 0, dy = 0; bool moved = false;
            switch (key)
            {
            case K_UP:    dy =  1; moved = true; break;
            case K_DOWN:  dy = -1; moved = true; break;
            case K_LEFT:  dx = -1; moved = true; break;
            case K_RIGHT: dx =  1; moved = true; break;
            default: break;
            }
            if (moved)
            {
                bool soundHit = false, soundMove = false;
                tryMove(world, player, dx, dy, soundHit, soundMove);
                ensureLanes(world, player.absY);
                if (g_state.soundOn)
                {
                    if      (soundHit && player.lives > 0) Term::beep_hit();
                    else if (!player.alive)                Term::beep_death();
                    else if (soundMove)                    Term::beep_move();
                }
            }
        }

        if (!paused)
        {
            long ms = std::chrono::duration_cast<MS>(now - lastTick).count();
            if (ms >= TICK_MS)
            {
                lastTick = now;
                bool soundHit = false;
                physTick(world, player, soundHit);
                ensureLanes(world, player.absY);
                if (g_state.soundOn && soundHit)
                    (!player.alive ? Term::beep_death() : Term::beep_hit());
                if (player.score > hiScore)
                {
                    if (g_state.soundOn && player.score % 10 == 0) Term::beep_score();
                    hiScore = player.score;
                }
                ++tick;
            }
        }

        {
            long ms = std::chrono::duration_cast<MS>(now - lastFrame).count();
            if (ms >= FRAME_MS)
            {
                lastFrame = now;
                renderFrame(world, player, hiScore, playerName, paused, tick);
            }
        }

        std::this_thread::sleep_for(MS(6));
    }

    // ── End-of-game summary ───────────────────────────────────────
    Term::show_cursor(); Term::restore(); Term::clear();
    int rank = submitScore(playerName, player.score);

    std::cout << Term::sfg(220) << "\033[1m";
    std::cout << "\n  ╔══════════════════════════════════════════╗\n";
    std::cout << "  ║              Game Summary                ║\n";
    std::cout << "  ╠══════════════════════════════════════════╣\n";
    std::cout << Term::sreset();

    auto row = [](const std::string &lbl, const std::string &val)
    {
        std::string line = "  ║  " + lbl + val;
        while ((int)line.size() < 46) line += " ";
        std::cout << line << "║\n";
    };
    std::cout << Term::sfg(252);
    row("Player:       ", playerName);
    row("Score:        ", std::to_string(player.score));
    row("Session Best: ", std::to_string(player.score)); // hiScore = player.score if just died
    row("Max Streak:   ", std::to_string(player.maxStreak));
    row("Total Moves:  ", std::to_string(g_state.totalMoves));
    row("Speed Tier:   ", g_state.speedTier == 0 ? "Normal" : g_state.speedTier == 1 ? "Fast" : "Turbo");
    if (rank >= 1 && rank <= MAX_LEADERBOARD_ENTRIES)
        row("Leaderboard:  ", "#" + std::to_string(rank));
    std::cout << Term::sfg(220) << "\033[1m"
              << "  ╚══════════════════════════════════════════╝\n\n"
              << Term::sreset();
    displayLeaderboard();
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 19: Entry Point
// ═══════════════════════════════════════════════════════════════════

static void cleanup(int) { Term::show_cursor(); Term::restore(); exit(0); }

int main()
{
    signal(SIGINT,  cleanup);
    signal(SIGTERM, cleanup);
    std::string name = splash();
    runGame(name);
    return 0;
}
