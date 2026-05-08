/*
 ╔══════════════════════════════════════════════════════════════════════╗
 ║         CROSSY ROAD — C++ Terminal Edition  v5.0  🐔                 ║
 ║    Pseudo-3D Isometric ASCII  |  Linked-List Architecture            ║
 ║    Leaderboard System         |  Player Name Support                 ║
 ║    Fixed: Restart segfault    |  Fixed: Duplicate name handling      ║
 ║    Added: Streak system       |  Added: Lives system                 ║
 ║    Added: Speed milestones    |  Added: Pause feature                ║
 ║    Added: Night mode          |  Added: Safe zone warnings           ║
 ╚══════════════════════════════════════════════════════════════════════╝
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
//  SECTION 1: Terminal Helpers if u see this u gay as fck
// ═══════════════════════════════════════════════════════════════════
namespace Term {
    struct termios orig_attr;
    bool raw_active = false;

    void raw() {
        tcgetattr(STDIN_FILENO, &orig_attr);
        struct termios t = orig_attr;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN]  = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        raw_active = true;
    }

    void restore() {
        if (raw_active) {
            tcsetattr(STDIN_FILENO, TCSANOW, &orig_attr);
            raw_active = false;
        }
    }

    void clear()       { std::cout << "\033[2J\033[H"; }
    void home()        { std::cout << "\033[H";         }
    void hide_cursor() { std::cout << "\033[?25l";      }
    void show_cursor() { std::cout << "\033[?25h";      }
    void reset_attr()  { std::cout << "\033[0m";        }
    void bold()        { std::cout << "\033[1m";        }
    void fg(int n)     { std::cout << "\033[38;5;" << n << "m"; }
    void bg(int n)     { std::cout << "\033[48;5;" << n << "m"; }
    void move(int r, int c) { std::cout << "\033[" << r << ";" << c << "H"; }

    std::string sfg(int n)   { return "\033[38;5;" + std::to_string(n) + "m"; }
    std::string sbg(int n)   { return "\033[48;5;" + std::to_string(n) + "m"; }
    std::string sreset()     { return "\033[0m"; }
    std::string sbold()      { return "\033[1m"; }

    // Canonical line input (for name entry)
    void canonical() {
        struct termios t = orig_attr;
        t.c_lflag |= (ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        raw_active = false;
    }

    int read_key() {
        unsigned char buf[8] = {};
        int n = (int)read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) return 0;
        if (n >= 3 && buf[0] == 27 && buf[1] == '[') {
            if (buf[2] == 'A') return 1000;
            if (buf[2] == 'B') return 1001;
            if (buf[2] == 'C') return 1002;
            if (buf[2] == 'D') return 1003;
        }
        // Handle ESC (pause)
        if (n == 1 && buf[0] == 27) return 1004;
        return (int)buf[0];
    }
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 2: Game Constants
// ═══════════════════════════════════════════════════════════════════

static const int BW           = 40;
static const int BH           = 12;
static const int LANE_H       = 3;
static const int PLAY_ROWS    = BH * LANE_H;
static const int ISO_SKEW     = 1;
static const int SCR_W        = BW + BH * ISO_SKEW + 4;
static const int PLAYER_LANE  = BH - 3;
static const int AHEAD_BUFFER = BH + 4;
static const int BELOW_BUFFER = BH + 4;
static const int MAX_LIVES    = 3;

static const std::string LEADERBOARD_FILE = "leaderboard.csv";
static const int MAX_LEADERBOARD_ENTRIES  = 10;

enum LaneType { SAFE, GRASS, ROAD, WATER };

// ═══════════════════════════════════════════════════════════════════
//  SECTION 3: Leaderboard System
//  FIX: Same name → replace old score instead of duplicating
// ═══════════════════════════════════════════════════════════════════

struct LeaderboardEntry {
    std::string name;
    int         score;
};

std::vector<LeaderboardEntry> loadLeaderboard() {
    std::vector<LeaderboardEntry> entries;
    std::ifstream file(LEADERBOARD_FILE);
    if (!file.is_open()) return entries;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto pos = line.rfind(',');
        if (pos == std::string::npos) continue;

        LeaderboardEntry e;
        e.name  = line.substr(0, pos);
        try {
            e.score = std::stoi(line.substr(pos + 1));
        } catch (...) {
            continue;
        }
        entries.push_back(e);
    }
    return entries;
}

void saveLeaderboard(const std::vector<LeaderboardEntry>& entries) {
    std::ofstream file(LEADERBOARD_FILE);
    if (!file.is_open()) return;
    for (const auto& e : entries) {
        file << e.name << "," << e.score << "\n";
    }
}

// FIX: If name already exists, update their score (keep best). Don't duplicate.
int submitScore(const std::string& name, int score) {
    auto entries = loadLeaderboard();

    // Check if name already exists → update if new score is better
    bool found = false;
    for (auto& e : entries) {
        if (e.name == name) {
            if (score > e.score) e.score = score;
            found = true;
            break;
        }
    }
    if (!found) {
        entries.push_back({name, score});
    }

    std::sort(entries.begin(), entries.end(),
        [](const LeaderboardEntry& a, const LeaderboardEntry& b) {
            return a.score > b.score;
        });

    int rank = -1;
    for (int i = 0; i < (int)entries.size(); ++i) {
        if (entries[i].name == name) {
            rank = i + 1;
            break;
        }
    }

    if ((int)entries.size() > MAX_LEADERBOARD_ENTRIES)
        entries.resize(MAX_LEADERBOARD_ENTRIES);

    saveLeaderboard(entries);
    return rank;
}

void displayLeaderboard() {
    auto entries = loadLeaderboard();

    Term::fg(220); Term::bold();
    std::cout << "\n  ╔══════════════════════════════════════════╗\n";
    std::cout << "  ║             🏆 LEADERBOARD 🏆              ║\n";
    std::cout << "  ╠══════╦══════════════════════╦════════════╣\n";
    std::cout << "  ║ RANK ║ PLAYER               ║ SCORE      ║\n";
    std::cout << "  ╠══════╬══════════════════════╬════════════╣\n";
    Term::reset_attr();

    if (entries.empty()) {
        Term::fg(240);
        std::cout << "  ║      No scores yet — be the first!      ║\n";
    } else {
        int rankColors[] = {220, 248, 130};
        for (int i = 0; i < (int)entries.size(); ++i) {
            int col = (i < 3) ? rankColors[i] : 245;
            Term::fg(col);

            std::string rankStr = "  " + std::to_string(i + 1);
            while ((int)rankStr.size() < 5) rankStr += " ";

            std::string nameStr = " " + entries[i].name;
            while ((int)nameStr.size() < 21) nameStr += " ";

            std::string scoreStr = " " + std::to_string(entries[i].score);
            while ((int)scoreStr.size() < 11) scoreStr += " ";

            std::cout << "  ║" << rankStr << " ║" << nameStr << " ║ " << scoreStr << "║\n";
        }
    }

    Term::fg(220); Term::bold();
    std::cout << "  ╚══════╩══════════════════════╩════════════╝\n\n";
    Term::reset_attr();
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 4: Visual Palette System
// ═══════════════════════════════════════════════════════════════════

struct LanePalette {
    int top_fg, top_bg;
    int front_fg, front_bg;
    int shadow_bg;
    char top_ch;
    char front_ch;
};

LanePalette makePalette(LaneType t, int depthFromPlayer, bool nightMode) {
    int d = std::min(depthFromPlayer, BH - 1);
    float fade = 1.0f - (d / (float)BH) * 0.55f;
    float nightFade = nightMode ? 0.6f : 1.0f;

    auto darken = [&](int base, float f) -> int {
        if (base >= 232 && base <= 255) {
            int v = (int)((base - 232) * f * nightFade);
            return 232 + std::max(0, std::min(23, v));
        }
        return base;
    };

    LanePalette p;
    switch (t) {
        case SAFE:
            p.top_fg    = darken(253, fade * 0.9f);
            p.top_bg    = darken(240, fade);
            p.front_fg  = darken(245, fade * 0.7f);
            p.front_bg  = darken(236, fade);
            p.shadow_bg = 232;
            p.top_ch    = ' ';
            p.front_ch  = ' ';
            break;
        case GRASS:
            {
                // FIX: Use valid xterm256 greens (not arbitrary offsets that land on wrong colors)
                int gtop  = nightMode ? (d < 3 ? 22 : 22) : (d < 3 ? 28 : 22);
                int gfrnt = nightMode ? 22 : 22;
                p.top_fg    = nightMode ? 22 : (d < 3 ? 34 : 28);
                p.top_bg    = gtop;
                p.front_fg  = nightMode ? 22 : (d < 3 ? 28 : 22);
                p.front_bg  = gfrnt;
                p.shadow_bg = 232;
                p.top_ch    = ' ';
                p.front_ch  = ' ';
            }
            break;
        case ROAD:
            {
                // FIX: Clamp all road colors to valid dark grays
                int rtop  = darken(241, fade);
                int rfrnt = darken(237, fade);
                p.top_fg    = darken(248, fade);
                p.top_bg    = rtop;
                p.front_fg  = darken(243, fade * 0.8f);
                p.front_bg  = rfrnt;
                p.shadow_bg = 232;
                p.top_ch    = ' ';
                p.front_ch  = ' ';
            }
            break;
        case WATER:
            {
                // FIX: Use only valid xterm256 blues (17-21, 27, 33, 39, 45, 51)
                // Avoid offsets that jump into wrong color bands
                int wdepth = nightMode ? 17 : (d < 2 ? 27 : (d < 5 ? 20 : 17));
                int wfrnt  = nightMode ? 17 : (d < 2 ? 20 : 17);
                p.top_fg    = nightMode ? 27 : (d < 3 ? 45 : (d < 6 ? 39 : 27));
                p.top_bg    = wdepth;
                p.front_fg  = nightMode ? 20 : (d < 3 ? 27 : 20);
                p.front_bg  = wfrnt;
                p.shadow_bg = 232;
                p.top_ch    = ' ';
                p.front_ch  = ' ';
            }
            break;
    }
    return p;
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 5: Generic Singly-Linked List
// ═══════════════════════════════════════════════════════════════════

template<typename T>
struct SLNode {
    T        data;
    SLNode*  next = nullptr;
    explicit SLNode(const T& d) : data(d) {}
};

template<typename T>
struct SLinkedList {
    SLNode<T>* head = nullptr;
    SLNode<T>* tail = nullptr;
    int        size = 0;

    SLinkedList()                              = default;
    SLinkedList(const SLinkedList&)            = delete;
    SLinkedList& operator=(const SLinkedList&) = delete;
    ~SLinkedList() { clear(); }

    void push_back(const T& d) {
        auto* n = new SLNode<T>(d);
        if (!tail) { head = tail = n; }
        else       { tail->next = n; tail = n; }
        ++size;
    }

    void clear() {
        while (head) {
            auto* t = head; head = head->next; delete t;
        }
        tail = nullptr; size = 0;
    }
};

// ═══════════════════════════════════════════════════════════════════
//  SECTION 6: Obstacle
// ═══════════════════════════════════════════════════════════════════

struct Obstacle {
    double x;
    int    width;
    double speed;
    int    vtype;
    int    colorId; // NEW: per-obstacle color variety

    bool contains(double px) const {
        return px >= x && px < x + (double)width;
    }
    int iLeft()  const { return (int)x; }
    int iRight() const { return (int)(x + width); }
};

// ═══════════════════════════════════════════════════════════════════
//  SECTION 7: Lane
// ═══════════════════════════════════════════════════════════════════

struct Lane {
    LaneType              type;
    SLinkedList<Obstacle> obs;
    int                   shade;
    Lane*                 prev = nullptr;
    Lane*                 next = nullptr;

    Lane(LaneType t, int s) : type(t), shade(s) {}
    Lane(const Lane&)            = delete;
    Lane& operator=(const Lane&) = delete;
};

// ═══════════════════════════════════════════════════════════════════
//  SECTION 8: World
// ═══════════════════════════════════════════════════════════════════

struct World {
    Lane* head  = nullptr;
    Lane* tail  = nullptr;
    int   count = 0;
    int   baseY = -1;

    World()  = default;
    ~World() { destroyAll(); }

    // Non-copyable, non-movable for safety
    World(const World&)            = delete;
    World& operator=(const World&) = delete;

    void destroyAll() {
        Lane* cur = head;
        while (cur) { Lane* n = cur->next; delete cur; cur = n; }
        head = tail = nullptr;
        count = 0;
        baseY = -1;
    }

    int headAbsY() const { return (count > 0) ? baseY + (count - 1) : -1; }

    void pushTop(Lane* l) {
        l->next = head; l->prev = nullptr;
        if (head) head->prev = l; else tail = l;
        head = l; ++count;
        if (count == 1) baseY = 0;
    }

    void popBottom() {
        if (!tail) return;
        Lane* t = tail;
        tail = t->prev;
        if (tail) tail->next = nullptr; else head = nullptr;
        delete t; --count; ++baseY;
    }

    Lane* laneAt(int absY) const {
        if (count == 0) return nullptr;
        int tailAbsY = baseY;
        int headAY   = tailAbsY + (count - 1);
        if (absY < tailAbsY || absY > headAY) return nullptr;
        int fromTail = absY - tailAbsY;
        int fromHead = headAY - absY;
        if (fromTail <= fromHead) {
            Lane* cur = tail;
            for (int i = 0; i < fromTail; ++i) cur = cur->prev;
            return cur;
        } else {
            Lane* cur = head;
            for (int i = 0; i < fromHead; ++i) cur = cur->next;
            return cur;
        }
    }
};

// ═══════════════════════════════════════════════════════════════════
//  SECTION 9: RNG Helpers
// ═══════════════════════════════════════════════════════════════════

static int    rng (int lo, int hi)       { return lo + rand() % (hi - lo + 1); }
static double rngf(double lo, double hi) { return lo + (hi - lo) * (rand() / (double)RAND_MAX); }

// ═══════════════════════════════════════════════════════════════════
//  SECTION 10: Game State (forward declared for lane factory)
// ═══════════════════════════════════════════════════════════════════

struct GameState {
    int  laneGen       = 0;
    bool nightMode     = false;
    int  speedTier     = 0;    // 0=normal, 1=fast, 2=turbo
    int  totalMoves    = 0;    // for statistics
};

static GameState g_state;

// ═══════════════════════════════════════════════════════════════════
//  SECTION 11: Lane Factory
//  Speeds scale with g_state.speedTier for milestone difficulty
// ═══════════════════════════════════════════════════════════════════

Lane* makeLane(int idx) {
    int rel = ((idx % 16) + 16) % 16;

    LaneType type;
    int shade;
    if      (rel == 0 || rel == 7 || rel == 15)              { type = SAFE;  shade = 238; }
    else if (rel == 1 || rel == 2 || rel == 8
          || rel == 13 || rel == 14)                         { type = GRASS; shade = 236; }
    else if (rel >= 3 && rel <= 6)                           { type = ROAD;  shade = 234; }
    else                                                     { type = WATER; shade = 233; }

    Lane* l = new Lane(type, shade);

    // Speed multiplier increases with tier
    double speedMult = 1.0 + g_state.speedTier * 0.35;

    if (type == ROAD) {
        double spd  = rngf(0.20, 0.55) * speedMult;
        if (rand() % 2) spd = -spd;
        int numCars = rng(2, 4);
        double carW = rng(4, 6);
        double minGap = 9.0 / speedMult; // Tighter gaps at higher speeds
        minGap = std::max(6.0, minGap);
        double pos  = rngf(0.0, 4.0);
        // Car color variety
        int carColors[] = {196, 202, 46, 51, 201, 226};
        int cIdx = rng(0, 5);
        for (int i = 0; i < numCars; ++i) {
            Obstacle o;
            o.x = pos; o.width = (int)carW; o.speed = spd;
            o.vtype = 0; o.colorId = carColors[cIdx];
            l->obs.push_back(o);
            pos += carW + minGap + rngf(0, 6);
        }
    } else if (type == WATER) {
        double spd  = rngf(0.12, 0.30) * speedMult;
        if (rand() % 2) spd = -spd;
        int numLogs = rng(3, 5);
        double logW = rngf(5.0, 9.0);
        double gap  = rngf(4.0, 7.0);
        double pos  = rngf(0.0, 4.0);
        for (int i = 0; i < numLogs; ++i) {
            Obstacle o;
            o.x = pos; o.width = (int)logW; o.speed = spd;
            o.vtype = 1; o.colorId = 94; // brown
            l->obs.push_back(o);
            pos += logW + gap;
            if (pos > BW) pos -= BW;
        }
    }
    return l;
}

void buildInitialWorld(World& w) {
    g_state.laneGen = 0;
    Lane* l = makeLane(g_state.laneGen++);
    w.head = w.tail = l;
    w.count = 1;
    w.baseY = 0;
    for (int i = 1; i <= AHEAD_BUFFER; ++i) {
        Lane* nl = makeLane(g_state.laneGen++);
        nl->next = w.head; nl->prev = nullptr;
        if (w.head) w.head->prev = nl;
        w.head = nl;
        ++w.count;
    }
}

void ensureLanes(World& w, int playerAbsY) {
    int needed_high = playerAbsY + AHEAD_BUFFER;
    int needed_low  = playerAbsY - BELOW_BUFFER;
    while (w.headAbsY() < needed_high) {
        Lane* l = makeLane(g_state.laneGen++);
        l->next = w.head; l->prev = nullptr;
        if (w.head) w.head->prev = l; else w.tail = l;
        w.head = l; ++w.count;
    }
    while (w.count > 0 && w.baseY < needed_low)
        w.popBottom();
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 12: Physics
// ═══════════════════════════════════════════════════════════════════

void tickLane(Lane* l) {
    if (!l) return;
    double W = (double)BW;
    auto* cur = l->obs.head;
    while (cur) {
        Obstacle& o = cur->data;
        o.x += o.speed;
        if (o.speed > 0 && o.x >= W)             o.x -= W + o.width;
        if (o.speed < 0 && o.x + o.width <= 0.0) o.x += W + o.width;
        cur = cur->next;
    }
}

Obstacle* findObs(Lane* l, double px) {
    if (!l) return nullptr;
    auto* cur = l->obs.head;
    while (cur) {
        if (cur->data.contains(px)) return &cur->data;
        cur = cur->next;
    }
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 13: Player
// ═══════════════════════════════════════════════════════════════════

struct Player {
    double px        = BW / 2.0;
    int    absY      = 0;
    bool   alive     = true;
    int    score     = 0;
    int    lives     = MAX_LIVES;     // NEW: lives system
    int    streak    = 0;             // NEW: forward move streak
    int    maxStreak = 0;             // NEW: best streak this game
    double logAccum  = 0.0;
    int    jumpFrame = 0;
    bool   movDir    = true;
    bool   invincible = false;        // NEW: brief invincibility after respawn
    int    invincFrames = 0;          // NEW: frames of invincibility left
    bool   hitFlash  = false;         // NEW: visual hit feedback
};

// Use a life instead of dying outright when alive=false after move
// Returns true if player is truly dead (no lives left)
bool loseLife(Player& p) {
    p.lives--;
    if (p.lives <= 0) {
        p.alive = false;
        return true;
    }
    // Respawn with brief invincibility
    p.alive = true;
    p.invincible = true;
    p.invincFrames = 40; // ~3.2 seconds of invincibility
    p.hitFlash = true;
    return false;
}

void tryMove(World& w, Player& p, int dx, int dy) {
    if (p.invincible) return; // Can't move during invincibility frames briefly

    double newPx   = p.px + dx;
    int    newAbsY = p.absY + dy;

    if (newPx < 0.0)      newPx = 0.0;
    if (newPx > BW - 1.0) newPx = BW - 1.0;

    if (dx != 0) p.movDir = (dx > 0);

    Lane* tgt = w.laneAt(newAbsY);
    if (!tgt) return;

    if (tgt->type == ROAD) {
        Obstacle* car = findObs(tgt, newPx);
        p.px = newPx; p.absY = newAbsY; p.logAccum = 0.0;
        if (car) {
            p.hitFlash = true;
            loseLife(p);
        }
    } else if (tgt->type == WATER) {
        Obstacle* log = findObs(tgt, newPx);
        p.px = newPx; p.absY = newAbsY; p.logAccum = 0.0;
        if (!log) {
            p.hitFlash = true;
            loseLife(p);
        }
    } else {
        p.px = newPx; p.absY = newAbsY; p.logAccum = 0.0;
    }

    if (p.alive) {
        p.jumpFrame = 1;
        if (dy > 0) {
            p.streak++;
            if (p.streak > p.maxStreak) p.maxStreak = p.streak;
            g_state.totalMoves++;
        } else if (dy < 0) {
            p.streak = 0; // Break streak on backward move
        }
    }
    if (p.absY > p.score) p.score = p.absY;
}

void physTick(World& w, Player& p) {
    if (!p.alive) return;

    // Count down invincibility
    if (p.invincible) {
        p.invincFrames--;
        if (p.invincFrames <= 0) {
            p.invincible = false;
            p.hitFlash   = false;
        }
    }

    int hi = p.absY + PLAYER_LANE + 2;
    int lo = p.absY - (BH - PLAYER_LANE) - 2;
    for (int ay = lo; ay <= hi; ++ay) tickLane(w.laneAt(ay));

    Lane* cur = w.laneAt(p.absY);

    if (cur && cur->type == WATER) {
        Obstacle* log = findObs(cur, p.px);
        if (!log) {
            if (!p.invincible) {
                p.hitFlash = true;
                loseLife(p);
                return;
            }
        } else {
            p.logAccum += log->speed;
            int drift   = (int)p.logAccum;
            p.logAccum -= drift;
            p.px       += drift;
            if (p.px < 0.0 || p.px >= BW) {
                if (!p.invincible) {
                    p.hitFlash = true;
                    loseLife(p);
                    return;
                }
                p.px = std::max(0.0, std::min((double)BW - 1.0, p.px));
            }
            if (!findObs(cur, p.px) && !p.invincible) {
                p.hitFlash = true;
                loseLife(p);
                return;
            }
        }
    } else {
        p.logAccum = 0.0;
    }

    if (cur && cur->type == ROAD && !p.invincible) {
        if (findObs(cur, p.px)) {
            p.hitFlash = true;
            loseLife(p);
        }
    }

    if (p.jumpFrame > 0) p.jumpFrame = (p.jumpFrame + 1) % 5;

    // Update speed tier based on score
    int newTier = std::min(2, p.score / 25);
    if (newTier > g_state.speedTier) {
        g_state.speedTier = newTier;
        // Note: existing lanes keep old speed, new lanes generate faster
    }
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 14: 3D Rendering Engine
// ═══════════════════════════════════════════════════════════════════

struct ScreenCell {
    std::string seq;
    bool        filled = false;
};

struct ScreenBuf {
    int rows, cols;
    std::vector<std::vector<ScreenCell>> cells;

    ScreenBuf(int r, int c) : rows(r), cols(c),
        cells(r, std::vector<ScreenCell>(c)) {}

    void set(int r, int c, const std::string& seq, bool fill = true) {
        if (r < 0 || r >= rows || c < 0 || c >= cols) return;
        cells[r][c].seq    = seq;
        cells[r][c].filled = fill;
    }

    void flush() {
        std::string out;
        out.reserve(rows * cols * 12);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (cells[r][c].filled)
                    out += cells[r][c].seq;
                else
                    out += "\033[0m ";
            }
            out += "\033[0m\n";
        }
        std::cout << "\033[H" << out;
        std::cout.flush();
    }
};

struct TileInfo {
    bool hasObs    = false;
    int  obsVtype  = -1;
    int  obsColor  = 196;  // NEW: per-obstacle color
    bool obsEdgeL  = false;
    bool obsEdgeR  = false;
    bool isPlayer  = false;
    bool playerDead = false;
};

void drawLane3D(ScreenBuf& buf,
                Lane* lane, LaneType type,
                int screenRow, int xOffset, int depth,
                const Player& p, int /*laneAbsY*/,
                bool playerOnThisLane,
                bool nightMode)
{
    LanePalette pal = makePalette(type, depth, nightMode);
    std::vector<TileInfo> tiles(BW);

    if (lane) {
        auto* cur = lane->obs.head;
        while (cur) {
            const Obstacle& o = cur->data;
            for (int c = o.iLeft(); c < o.iRight() && c < BW; ++c) {
                if (c < 0) continue;
                tiles[c].hasObs   = true;
                tiles[c].obsVtype = o.vtype;
                tiles[c].obsColor = o.colorId;
                tiles[c].obsEdgeL = (c == o.iLeft());
                tiles[c].obsEdgeR = (c == o.iRight() - 1);
            }
            cur = cur->next;
        }
    }

    if (playerOnThisLane) {
        int pc = (int)p.px;
        if (pc >= 0 && pc < BW) {
            tiles[pc].isPlayer   = true;
            tiles[pc].playerDead = !p.alive;
        }
    }

    // Row 0: Top face
    for (int c = 0; c < BW; ++c) {
        int sc = c + xOffset;
        const TileInfo& ti = tiles[c];
        std::string cell;

        if (ti.isPlayer) {
            if (ti.playerDead) {
                cell = Term::sbg(52) + Term::sfg(196) + Term::sbold() + "\xe2\x96\x84";
            } else if (p.invincible && (p.invincFrames % 4 < 2)) {
                // Flashing during invincibility
                cell = Term::sbg(255) + Term::sfg(226) + "\xe2\x96\x84";
            } else {
                int pColor = (p.jumpFrame > 0) ? 226 : 220;
                cell = Term::sbg(pColor) + Term::sfg(214) + "\xe2\x96\x84";
            }
        } else if (ti.hasObs) {
            if (ti.obsVtype == 0) {
                // Car top with per-car color variety
                int carTopBg = (depth < 3) ? 250 : 244;
                int carTopFg = (depth < 3) ? 255 : 248;
                if (ti.obsEdgeL || ti.obsEdgeR)
                    cell = Term::sbg(carTopBg) + Term::sfg(carTopFg) + "\xe2\x96\x84";
                else
                    cell = Term::sbg(carTopBg - 2) + Term::sfg(carTopFg) + "\xe2\x96\x84";
            } else {
                // Log top: brown wood, safe xterm256 browns
                int logBg = (depth < 3) ? 94 : 58;
                int logFg = (depth < 3) ? 130 : 94;
                if (ti.obsEdgeL || ti.obsEdgeR)
                    cell = Term::sbg(logBg) + Term::sfg(logFg + 6 <= 231 ? logFg + 6 : logFg) + "\xe2\x96\x84";
                else
                    cell = Term::sbg((c % 2 == 0) ? logBg : (logBg > 232 ? logBg - 1 : logBg)) + Term::sfg(logFg) + "\xe2\x96\x84";
            }
        } else {
            switch (type) {
                case WATER: {
                    // FIX: Use only valid blue values with safe offsets
                    int wbg = pal.top_bg; // 17, 20, or 27 — valid blues
                    int wfg = pal.top_fg; // 27, 39, or 45 — valid blues
                    // Subtle wave pattern using only valid colors
                    if ((c + (int)(lane && lane->obs.head ? lane->obs.head->data.x * 0.3 : 0)) % 4 == 0) {
                        int wbg2 = (wbg == 27) ? 33 : (wbg == 20) ? 26 : 18;
                        cell = Term::sbg(wbg2) + Term::sfg(wfg) + "\xe2\x96\x84";
                    } else {
                        cell = Term::sbg(wbg) + Term::sfg(wfg) + "\xe2\x96\x84";
                    }
                    break;
                }
                case GRASS: {
                    int gbg = pal.top_bg;
                    int gfg = pal.top_fg;
                    // FIX: safe grass color offsets
                    int gfg2 = (gfg < 34) ? gfg : 34;
                    cell = (c % 5 == 0)
                        ? Term::sbg(gbg) + Term::sfg(gfg2) + "\xe2\x96\x84"
                        : Term::sbg(gbg) + Term::sfg(gfg) + "\xe2\x96\x84";
                    break;
                }
                case ROAD: {
                    int rbg = pal.top_bg;
                    int rfg = pal.top_fg;
                    // Road markings: dashed white line in middle
                    if (c == BW / 2 && c % 4 < 2)
                        cell = Term::sbg(rbg) + Term::sfg(255) + "\xe2\x96\x84";
                    else if (c % 6 == 0)
                        cell = Term::sbg(rbg + 1 <= 255 ? rbg + 1 : rbg) + Term::sfg(rfg) + "\xe2\x96\x84";
                    else
                        cell = Term::sbg(rbg) + Term::sfg(rfg) + "\xe2\x96\x84";
                    break;
                }
                case SAFE: {
                    int sbg2 = pal.top_bg, sfg2 = pal.top_fg;
                    int sfg3 = std::min(255, sfg2 + 2);
                    int sbg3 = std::min(255, sbg2 + 1);
                    cell = ((c + depth) % 3 == 0)
                        ? Term::sbg(sbg3) + Term::sfg(sfg3) + "\xe2\x96\x84"
                        : Term::sbg(sbg2) + Term::sfg(sfg2) + "\xe2\x96\x84";
                    break;
                }
            }
        }
        buf.set(screenRow, sc, cell);
    }

    // Row 1: Front face
    int fr = screenRow + 1;
    for (int c = 0; c < BW; ++c) {
        int sc = c + xOffset;
        const TileInfo& ti = tiles[c];
        std::string cell;

        if (ti.isPlayer) {
            if (ti.playerDead) {
                cell = Term::sbg(52) + Term::sfg(196) + Term::sbold() + "\xe2\x96\x93";
            } else if (p.invincible && (p.invincFrames % 4 < 2)) {
                cell = Term::sbg(255) + Term::sfg(214) + Term::sbold() + "\xe2\x96\x88";
            } else {
                int bodyBg = (p.jumpFrame > 0) ? 214 : 208;
                cell = Term::sbg(bodyBg) + Term::sfg(220) + Term::sbold() + "\xe2\x96\x88";
            }
        } else if (ti.hasObs) {
            if (ti.obsVtype == 0) {
                // Car with color variety on body
                if (ti.obsEdgeL) {
                    cell = Term::sbg(226) + Term::sfg(255) + "\xe2\x96\x93"; // headlight
                } else if (ti.obsEdgeR) {
                    cell = Term::sbg(124) + Term::sfg(196) + "\xe2\x96\x93"; // taillight
                } else {
                    // Car body uses per-obstacle color (but only in valid range)
                    int carBg = (depth < 3) ? 240 : 236;
                    cell = Term::sbg(carBg) + Term::sfg(ti.obsColor) + "\xe2\x96\x88";
                }
            } else {
                // Log front: bark texture with safe colors
                int logFrontBg = (depth < 3) ? 130 : 94;
                int logFrontFg = (depth < 3) ? 172 : 130;
                if (ti.obsEdgeL || ti.obsEdgeR)
                    cell = Term::sbg(logFrontBg) + Term::sfg(logFrontFg) + "\xe2\x96\x93";
                else
                    cell = Term::sbg(logFrontBg - (c % 2 == 0 ? 0 : 0)) + Term::sfg(logFrontFg) + "\xe2\x96\x92";
            }
        } else {
            switch (type) {
                case WATER: {
                    // FIX: Only valid blues for water front
                    int wfbg = pal.front_bg; // 17 or 20
                    int wffg = pal.front_fg; // 20 or 27
                    cell = (c % 3 == 1)
                        ? Term::sbg(wfbg) + Term::sfg(wffg) + "\xe2\x96\x92"
                        : Term::sbg(wfbg) + Term::sfg(wffg) + "\xe2\x96\x88";
                    break;
                }
                case GRASS: {
                    int gfbg = pal.front_bg, gffg = pal.front_fg;
                    cell = (c % 4 == 0)
                        ? Term::sbg(gfbg) + Term::sfg(gffg) + "\xe2\x96\x92"
                        : Term::sbg(gfbg) + Term::sfg(gffg) + "\xe2\x96\x88";
                    break;
                }
                case ROAD: {
                    cell = Term::sbg(pal.front_bg) + Term::sfg(pal.front_fg) + "\xe2\x96\x88";
                    break;
                }
                case SAFE: {
                    int sfbg = pal.front_bg, sffg = pal.front_fg;
                    cell = (c % 3 == 0)
                        ? Term::sbg(sfbg) + Term::sfg(sffg) + "\xe2\x96\x91"
                        : Term::sbg(sfbg) + Term::sfg(sffg) + "\xe2\x96\x88";
                    break;
                }
            }
        }
        buf.set(fr, sc, cell);
    }

    // Row 2: Shadow
    int sr = screenRow + 2;
    for (int c = 0; c < BW; ++c) {
        int sc = c + xOffset;
        const TileInfo& ti = tiles[c];
        std::string cell;
        if (ti.isPlayer && p.alive)
            cell = Term::sbg(232) + Term::sfg(238) + "\xe2\x96\x91";
        else if (ti.hasObs)
            cell = Term::sbg(232) + Term::sfg(235) + "\xe2\x96\x91";
        else
            cell = Term::sbg(232) + Term::sfg(233) + " ";
        buf.set(sr, sc, cell);
    }
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 15: HUD and Footer Rendering
//  NEW: Shows lives, streak, speed tier, night mode indicator
// ═══════════════════════════════════════════════════════════════════

static const int HUD_ROWS = 3;

void drawHUD3D(ScreenBuf& buf, const Player& p, int hi,
               const std::string& playerName) {
    // Top border
    { int col = 0;
      buf.set(0, col++, Term::sbg(234) + Term::sfg(240) + "\xe2\x94\x8c");
      for (int i = 0; i < SCR_W - 2; ++i)
          buf.set(0, col++, Term::sbg(234) + Term::sfg(238) + "\xe2\x94\x80");
      buf.set(0, col, Term::sbg(234) + Term::sfg(240) + "\xe2\x94\x90");
    }

    // Title row with lives, streak, speed tier
    { int col = 0;
      buf.set(1, col++, Term::sbg(234) + Term::sfg(240) + "\xe2\x94\x82");

      std::string title = " \xF0\x9F\x90\x94 CROSSY 3D [" + playerName + "]";
      for (char c : title)
          buf.set(1, col++, Term::sbg(234) + Term::sfg(255) + Term::sbold() + std::string(1, c));

      // Lives display
      std::string livesStr = " \xe2\x9d\xa4";
      for (int i = 0; i < MAX_LIVES; ++i)
          livesStr += (i < p.lives) ? "\xe2\x9d\xa4" : "\xe2\x9d\xa4";

      // Simplified: just show heart count as text
      std::string livesText = "  HP:";
      for (int i = 0; i < MAX_LIVES; i++)
          livesText += (i < p.lives) ? "*" : ".";

      // Speed tier indicator
      std::string tierStr = "  SPD:";
      if (g_state.speedTier == 0) tierStr += "NRM";
      else if (g_state.speedTier == 1) tierStr += "FST";
      else tierStr += "TRB";

      // Night mode indicator
      std::string nightStr = g_state.nightMode ? "  [NIGHT]" : "";

      // Streak
      std::string streakStr = "  STK:" + std::to_string(p.streak);

      std::string scoreStr  = "  S:" + std::to_string(p.score)
                            + " B:" + std::to_string(hi) + " ";

      std::string info = livesText + tierStr + nightStr + streakStr + scoreStr;
      for (char c : info)
          buf.set(1, col++, Term::sbg(234) + Term::sfg(248) + std::string(1, c));

      // Fill rest
      while (col < SCR_W - 1)
          buf.set(1, col++, Term::sbg(234) + " ");
      buf.set(1, SCR_W - 1, Term::sbg(234) + Term::sfg(240) + "\xe2\x94\x82");
    }

    // Separator
    { int col = 0;
      buf.set(2, col++, Term::sbg(234) + Term::sfg(240) + "\xe2\x94\x9c");
      for (int i = 0; i < SCR_W - 2; ++i)
          buf.set(2, col++, Term::sbg(234) + Term::sfg(237) + "\xe2\x94\x80");
      buf.set(2, col, Term::sbg(234) + Term::sfg(240) + "\xe2\x94\xa4");
    }
}

void drawFooter3D(ScreenBuf& buf, int baseRow, bool dead, bool paused) {
    { int col = 0;
      buf.set(baseRow, col++, Term::sbg(234) + Term::sfg(240) + "\xe2\x94\x94");
      for (int i = 0; i < SCR_W - 2; ++i)
          buf.set(baseRow, col++, Term::sbg(234) + Term::sfg(237) + "\xe2\x94\x80");
      buf.set(baseRow, col, Term::sbg(234) + Term::sfg(240) + "\xe2\x94\x98");
    }

    { int col = 0;
      buf.set(baseRow + 1, col++, Term::sbg(232) + " ");
      std::string line;
      if (paused) {
          line = Term::sbg(22) + Term::sfg(255) + Term::sbold()
               + "  \xe2\x95\x94\xe2\x95\x90\xe2\x95\x90 PAUSED \xe2\x95\x90\xe2\x95\x90\xe2\x95\x97  "
               + Term::sreset() + Term::sfg(242)
               + "  ESC/P resume    N night mode    L leaderboard    Q quit";
      } else if (dead) {
          line = Term::sbg(88) + Term::sfg(255) + Term::sbold()
               + "  \xe2\x95\x94\xe2\x95\x90\xe2\x95\x90 GAME OVER \xe2\x95\x90\xe2\x95\x90\xe2\x95\x97  "
               + Term::sreset() + Term::sfg(242)
               + "  R restart    L leaderboard    Q quit";
      } else {
          line = Term::sbg(232) + Term::sfg(240)
               + "  W/\xe2\x86\x91 fwd  S/\xe2\x86\x93 back  "
               + "A/\xe2\x86\x90 left  D/\xe2\x86\x92 right  "
               + "ESC pause  N night  L board  Q quit";
      }
      buf.set(baseRow + 1, 1, line);
      for (int c = 2; c < SCR_W; ++c)
          if (!buf.cells[baseRow + 1][c].filled)
              buf.set(baseRow + 1, c, Term::sbg(232) + " ");
    }
}

void drawDepthRail(ScreenBuf& buf, int startRow, int numLanes) {
    for (int lane = 0; lane < numLanes; ++lane) {
        int xOff = (numLanes - 1 - lane) * ISO_SKEW;
        int r    = startRow + lane * LANE_H;
        for (int row = 0; row < LANE_H - 1; ++row)
            buf.set(r + row, xOff, Term::sbg(232) + Term::sfg(236) + "\xe2\x96\x8c");
    }
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 16: Full Frame Render
// ═══════════════════════════════════════════════════════════════════

void render3D(const World& w, const Player& p, int hi,
              const std::string& playerName, bool paused) {
    int totalRows = HUD_ROWS + PLAY_ROWS + 2;
    int totalCols = SCR_W + 2;
    ScreenBuf buf(totalRows, totalCols);

    // Night mode: very dark background
    int bgCol = g_state.nightMode ? 232 : 233;
    for (int r = 0; r < totalRows; ++r)
        for (int c = 0; c < totalCols; ++c)
            buf.set(r, c, Term::sbg(bgCol) + " ");

    drawHUD3D(buf, p, hi, playerName);

    for (int sl = 0; sl < BH; ++sl) {
        int absY       = p.absY + (PLAYER_LANE - sl);
        int xOffset    = (BH - 1 - sl) * ISO_SKEW;
        int depth      = std::abs(sl - PLAYER_LANE);
        Lane*    lane  = w.laneAt(absY);
        LaneType type  = lane ? lane->type : SAFE;
        int  screenRow = HUD_ROWS + sl * LANE_H;
        bool onLane    = (absY == p.absY);
        drawLane3D(buf, lane, type, screenRow, xOffset, depth, p, absY, onLane, g_state.nightMode);
    }

    drawDepthRail(buf, HUD_ROWS, BH);
    drawFooter3D(buf, HUD_ROWS + PLAY_ROWS, !p.alive, paused);

    // Paused overlay: dim the play area
    if (paused) {
        // Draw a centered pause box over the lanes
        int midRow = HUD_ROWS + PLAY_ROWS / 2;
        int midCol = SCR_W / 2 - 10;
        std::string pauseMsg = "  !! PAUSED !!  ";
        buf.set(midRow, midCol, Term::sbg(22) + Term::sfg(255) + Term::sbold() + pauseMsg);
    }

    buf.flush();
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 17: Input Handling
// ═══════════════════════════════════════════════════════════════════

enum Key {
    K_NONE, K_UP, K_DOWN, K_LEFT, K_RIGHT,
    K_QUIT, K_RESTART, K_LEADERBOARD,
    K_PAUSE, K_NIGHT
};

Key pollKey() {
    int k = Term::read_key();
    switch (k) {
        case 1000: case 'w': case 'W': return K_UP;
        case 1001: case 's': case 'S': return K_DOWN;
        case 1002: case 'd': case 'D': return K_RIGHT;
        case 1003: case 'a': case 'A': return K_LEFT;
        case 'q':  case 'Q':           return K_QUIT;
        case 'r':  case 'R':           return K_RESTART;
        case 'l':  case 'L':           return K_LEADERBOARD;
        case 1004: case 'p': case 'P': return K_PAUSE;
        case 'n':  case 'N':           return K_NIGHT;
        default:                        return K_NONE;
    }
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 18: Splash Screen
// ═══════════════════════════════════════════════════════════════════

std::string splash() {
    Term::clear();

    std::string title[] = {
        " ██████╗██████╗  ██████╗ ███████╗███████╗██╗   ██╗",
        "██╔════╝██╔══██╗██╔═══██╗██╔════╝██╔════╝╚██╗ ██╔╝",
        "██║     ██████╔╝██║   ██║███████╗███████╗ ╚████╔╝ ",
        "██║     ██╔══██╗██║   ██║╚════██║╚════██║  ╚██╔╝  ",
        "╚██████╗██║  ██║╚██████╔╝███████║███████║   ██║   ",
        " ╚═════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝╚══════╝   ╚═╝  "
    };
    std::string title2[] = {
        "██████╗  ██████╗  █████╗ ██████╗     ██████╗ ██████╗ ",
        "██╔══██╗██╔═══██╗██╔══██╗██╔══██╗    ╚════██╗██╔══██╗",
        "██████╔╝██║   ██║███████║██║  ██║     █████╔╝██║  ██║",
        "██╔══██╗██║   ██║██╔══██║██║  ██║     ╚═══██╗██║  ██║",
        "██║  ██║╚██████╔╝██║  ██║██████╔╝    ██████╔╝██████╔╝",
        "╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═╝╚═════╝     ╚═════╝ ╚═════╝ "
    };

    int colors[] = {196, 202, 208, 214, 220, 226};
    for (int i = 0; i < 6; ++i) {
        Term::fg(colors[i]);
        std::cout << "  " << title[i] << "\n";
    }
    std::cout << "\n";
    for (int i = 0; i < 6; ++i) {
        Term::fg(colors[5 - i]);
        std::cout << "  " << title2[i] << "\n";
    }
    Term::reset_attr();
    std::cout << "\n";

    Term::fg(245);
    std::cout << "  ┌──────────────────────────────────────────────────────┐\n";
    std::cout << "  │  C++ Terminal Edition v5.0  │  3D Isometric ASCII    │\n";
    std::cout << "  │  Linked-List Architecture   │  256-Color Renderer    │\n";
    std::cout << "  │  3 Lives System             │  Streak Counter        │\n";
    std::cout << "  │  Speed Milestones           │  Night Mode            │\n";
    std::cout << "  └──────────────────────────────────────────────────────┘\n\n";

    Term::fg(240);
    std::cout << "  ┌─ CONTROLS ──────────────────────────────────────────┐\n"; Term::fg(252);
    std::cout << "  │  W / ↑   Move forward              3 LIVES          │ \n";
    std::cout << "  │  S / ↓   Move back                 N   Night mode   │\n";
    std::cout << "  │  A / ←   Move left                 ESC Pause        │\n";
    std::cout << "  │  D / →   Move right                L   Leaderboard  │\n";
    std::cout << "  │  R       Restart   Q   Quit                         │\n"; Term::fg(240);
    std::cout << "  └─────────────────────────────────────────────────────┘\n\n";

    std::cout << "  Speed tiers unlock at score 25 (FAST) and 50 (TURBO)!\n";
    std::cout << "  Build streaks by moving forward without backtracking!\n\n";

    displayLeaderboard();

    Term::fg(220); Term::bold();
    std::cout << "  Enter your name: ";
    Term::reset_attr();
    std::string name;
    std::getline(std::cin, name);

    if (name.empty()) name = "Anonymous";
    if ((int)name.size() > 20) name = name.substr(0, 20);

    std::cout << "\n";
    Term::fg(244);
    std::cout << "  Welcome, " << name << "! Press ENTER to start ...\n\n";
    Term::reset_attr();
    std::cin.ignore(10000, '\n');

    return name;
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 19: Game Loop
//  FIX: Restart no longer causes segfault — uses destroyAll() + rebuild
// ═══════════════════════════════════════════════════════════════════

void resetGame(World& world, Player& player) {
    // FIX: Safely destroy existing world before rebuilding
    world.destroyAll();

    // Reset global state
    g_state.laneGen   = 0;
    g_state.speedTier = 0;
    g_state.totalMoves = 0;

    // Reset player
    player = Player();

    // Rebuild world
    buildInitialWorld(world);
    ensureLanes(world, player.absY);
}

void showLeaderboardPause(World& world, const Player& player, int hiScore, const std::string& playerName) {
    Term::restore();
    Term::show_cursor();
    Term::clear();
    displayLeaderboard();
    Term::fg(244);
    std::cout << "  Press ENTER to return to game...\n";
    Term::reset_attr();
    Term::canonical();
    std::string dummy;
    std::getline(std::cin, dummy);
    Term::raw();
    Term::hide_cursor();
    Term::clear();
    render3D(world, player, hiScore, playerName, false);
}

void runGame(const std::string& playerName) {
    srand((unsigned)time(nullptr));

    World  world;
    Player player;
    int    hiScore   = 0;
    bool   quit      = false;
    bool   paused    = false;

    resetGame(world, player);

    using Clock = std::chrono::steady_clock;
    using MS    = std::chrono::milliseconds;

    const long TICK_MS  = 90;
    const long FRAME_MS = 50;

    auto lastTick  = Clock::now();
    auto lastFrame = Clock::now();

    Term::raw();
    Term::hide_cursor();
    Term::clear();
    render3D(world, player, hiScore, playerName, paused);

    while (!quit) {
        auto now = Clock::now();
        Key key = pollKey();

        // Handle input regardless of pause for certain keys
        switch (key) {
            case K_QUIT:
                quit = true;
                continue;

            case K_RESTART:
                if (!player.alive) {
                    // FIX: Proper restart — destroyAll then rebuild
                    resetGame(world, player);
                    paused = false;
                    Term::clear();
                    render3D(world, player, hiScore, playerName, paused);
                }
                continue;

            case K_PAUSE:
                if (player.alive) {
                    paused = !paused;
                    render3D(world, player, hiScore, playerName, paused);
                }
                continue;

            case K_NIGHT:
                g_state.nightMode = !g_state.nightMode;
                render3D(world, player, hiScore, playerName, paused);
                continue;

            case K_LEADERBOARD:
                showLeaderboardPause(world, player, hiScore, playerName);
                continue;

            default:
                break;
        }

        // Movement blocked when paused or dead
        if (!paused && player.alive) {
            int dx = 0, dy = 0;
            bool moved = false;
            switch (key) {
                case K_UP:    dy =  1; moved = true; break;
                case K_DOWN:  dy = -1; moved = true; break;
                case K_LEFT:  dx = -1; moved = true; break;
                case K_RIGHT: dx =  1; moved = true; break;
                default: break;
            }
            if (moved) {
                tryMove(world, player, dx, dy);
                ensureLanes(world, player.absY);
            }
        }

        // Physics (skip when paused)
        if (!paused) {
            long tickMs = std::chrono::duration_cast<MS>(now - lastTick).count();
            if (tickMs >= TICK_MS) {
                lastTick = now;
                physTick(world, player);
                ensureLanes(world, player.absY);
                if (player.score > hiScore) hiScore = player.score;
            }
        }

        // Render
        long frameMs = std::chrono::duration_cast<MS>(now - lastFrame).count();
        if (frameMs >= FRAME_MS) {
            lastFrame = now;
            render3D(world, player, hiScore, playerName, paused);
        }

        std::this_thread::sleep_for(MS(8));
    }

    // Session end
    Term::show_cursor();
    Term::restore();
    Term::clear();

    int rank = submitScore(playerName, player.score);

    std::cout << "\n";
    Term::fg(220); Term::bold();
    std::cout << "  ╔══════════════════════════════════════╗\n";
    std::cout << "  ║           Game Summary               ║\n";
    std::cout << "  ╠══════════════════════════════════════╣\n";
    Term::reset_attr(); Term::fg(252);

    auto padLine = [](const std::string& label, const std::string& val) {
        std::string line = "  ║   " + label + ": " + val;
        while ((int)line.size() < 42) line += " ";
        line += " ║";
        std::cout << line << "\n";
    };

    padLine("Player     ", playerName);
    padLine("Score      ", std::to_string(player.score));
    padLine("Best       ", std::to_string(hiScore));
    padLine("Max Streak ", std::to_string(player.maxStreak));
    padLine("Moves      ", std::to_string(g_state.totalMoves));
    padLine("Speed Tier ", g_state.speedTier == 0 ? "Normal" :
                          g_state.speedTier == 1 ? "Fast" : "Turbo");

    if (rank >= 1 && rank <= MAX_LEADERBOARD_ENTRIES)
        padLine("Rank      ", "#" + std::to_string(rank) + " on leaderboard");

    Term::fg(220);
    std::cout << "  ╚══════════════════════════════════════╝\n\n";
    Term::reset_attr();

    displayLeaderboard();
}

// ═══════════════════════════════════════════════════════════════════
//  SECTION 20: Entry Point
// ═══════════════════════════════════════════════════════════════════

static void cleanup(int) {
    Term::show_cursor();
    Term::restore();
    exit(0);
}

int main() {
    signal(SIGINT,  cleanup);
    signal(SIGTERM, cleanup);

    std::string name = splash();
    runGame(name);

    return 0;
}
