# Crossy Road — C++ Terminal Edition v5.0

A fully playable terminal-based recreation of the classic Crossy Road game written in modern C++. This project features a pseudo-3D isometric ASCII renderer, linked-list world generation, leaderboard persistence, dynamic difficulty scaling, night mode, lives system, and colorful terminal graphics using ANSI escape codes.

## Features
- Pseudo-3D isometric ASCII rendering
- 256-color terminal graphics
- Procedural lane generation
- Linked-list world architecture
- Persistent leaderboard system
- Player name support
- Lives and respawn system
- Forward streak tracking
- Dynamic speed milestones
- Pause system
- Night mode rendering
- Water physics with moving logs
- Collision detection
- Restart system without segfaults
- Safe zone warnings
- Real-time keyboard input
- Optimized screen buffer rendering

## Gameplay
Your goal is to move forward as far as possible while avoiding cars on road lanes, falling into water, and drifting off logs. The farther you go, the faster the game becomes.

### Speed Tiers
- Normal → Score 0+
- Fast → Score 25+
- Turbo → Score 50+

You have 3 lives before the game ends.

## Controls
| Key | Action |
|---|---|
| W / ↑ | Move Forward |
| S / ↓ | Move Backward |
| A / ← | Move Left |
| D / → | Move Right |
| ESC / P | Pause Game |
| N | Toggle Night Mode |
| L | Open Leaderboard |
| R | Restart After Death |
| Q | Quit Game |

## Requirements
This project is designed for Linux/macOS terminals with ANSI escape code support.

### Required
- g++
- POSIX terminal support
- ANSI-compatible terminal

### Tested On
- Linux
- WSL
- macOS Terminal

### Not Officially Supported
- Windows CMD
- PowerShell without ANSI support

## Compilation
Compile using g++:

```bash
g++ -std=c++17 -O2 -pthread main.cpp -o crossy
```

## Running the Game

```bash
./crossy
```

## Project Architecture

### Terminal System
Handles:
- Raw keyboard input
- Cursor control
- ANSI colors
- Screen clearing

### Linked-List World Engine
The game world is generated dynamically using:
- Singly linked lists for obstacles
- Doubly linked lane chains
- Infinite scrolling terrain

### Physics Engine
Handles:
- Collision detection
- Water drift physics
- Log riding
- Car movement
- Player movement

### Rendering Engine
Features:
- Isometric pseudo-3D rendering
- Depth shading
- Dynamic palettes
- Shadow layers
- Double-layer tiles

### Leaderboard System
Stores high scores in:

```text
leaderboard.csv
```

Duplicate player names automatically update their best score instead of creating duplicates.

## File Structure

```text
project/
├── main.cpp
├── leaderboard.csv
└── README.md
```

## Technical Highlights

### Data Structures Used
- Singly Linked Lists
- Doubly Linked Lists
- Dynamic Buffers
- Vectors
- Struct-based game entities

### Programming Concepts
- Object-oriented design
- Procedural generation
- Terminal graphics
- Real-time game loops
- Memory management
- Physics simulation
- ANSI escape rendering
- Non-blocking input
- File handling

## Rendering Details
The renderer creates a fake 3D perspective using lane skew offsets, multi-row tile rendering, dynamic shadows, color fading by depth, and layered ASCII blocks. The game uses Unicode block characters for smoother visuals.

## Leaderboard
Scores are automatically saved locally. Top 10 scores are preserved between sessions.

Example leaderboard entry:

```text
Karl,52
Zenn,41
Anonymous,19
```

## Known Limitations
- Requires a terminal that supports ANSI escape sequences
- Unicode rendering may vary by font
- Window resizing during gameplay may distort rendering

## Future Improvements
Possible future additions:
- Sound effects
- Multiplayer mode
- More obstacle types
- Animated environments
- Configurable controls
- Better Windows support
- Power-ups
- Difficulty settings

## Author
Created as a C++ terminal game project demonstrating:
- Linked-list data structures
- Terminal graphics programming
- Real-time game systems
- Procedural world generation

## License
This project is open-source and free to modify for educational purposes.
