/*
 * Star Trek (1971), compact KIM-1 port.
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Derived from the ANSI C port by busfahrer, based on Michael Birken's
 * C# port of Mike Mayfield's original game.
 *
 * This version uses integer course directions and KIM-1 monitor serial I/O.
 * The gameplay and command structure follow the original while avoiding
 * floating point and hosted C library dependencies.
 */

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;

extern u8 __fastcall__ kim_getc(void);
extern void __fastcall__ kim_putc(u8 ch);

#define QUADS 64
#define SECTORS 64
#define TILE_EMPTY 0
#define TILE_SHIP 1
#define TILE_KLINGON 2
#define TILE_BASE 3
#define TILE_STAR 4

static u8 galaxy[QUADS];
static u8 known[QUADS];
static u8 board[SECTORS];
static u8 enemy_x[3];
static u8 enemy_y[3];
static int enemy_shields[3];
static s8 damage[8];

static u16 rng_state;
static int qx;
static int qy;
static int sx;
static int sy;
static int energy;
static int shields;
static int torpedoes;
static int stardate;
static int deadline;
static int total_klingons;
static int klingons_left;
static int bases_left;
static int local_klingons;
static int local_bases;
static int local_stars;
static int game_over;

static const s8 course_dx[9] = { 0, 1, 1, 0, -1, -1, -1, 0, 1 };
static const s8 course_dy[9] = { 0, 0, -1, -1, -1, 0, 1, 1, 1 };
static const char *device_names[8] = {
    "WARP ENGINES", "SHORT RANGE SENSORS", "LONG RANGE SENSORS",
    "PHASER CONTROL", "PHOTON TUBES", "DAMAGE CONTROL",
    "SHIELD CONTROL", "LIBRARY COMPUTER"
};

static void putch(char ch)
{
    kim_putc((u8)ch);
}

static void print(const char *text)
{
    while (*text) {
        putch(*text++);
    }
}

static void newline(void)
{
    putch(13);
    putch(10);
}

static void print_num(int value)
{
    char digits[6];
    int count;
    unsigned int n;

    count = 0;
    if (value < 0) {
        putch('-');
        n = (unsigned int)(-value);
    } else {
        n = (unsigned int)value;
    }
    do {
        digits[count++] = (char)('0' + n % 10);
        n /= 10;
    } while (n && count < 5);
    while (count) {
        putch(digits[--count]);
    }
}

static int read_number(void)
{
    int value;
    int sign;
    int have_digit;
    u8 ch;

    value = 0;
    sign = 1;
    have_digit = 0;
    for (;;) {
        ch = kim_getc();
        if (ch == 13 || ch == 10) {
            if (have_digit) {
                newline();
                return value * sign;
            }
            continue;
        }
        if (ch == '-' && !have_digit) {
            sign = -1;
        } else if (ch >= '0' && ch <= '9') {
            have_digit = 1;
            value = value * 10 + (ch - '0');
            if (value > 30000) {
                value = 30000;
            }
        }
    }
}

static int qindex(int x, int y)
{
    return (y - 1) * 8 + x - 1;
}

static int sindex(int x, int y)
{
    return (y - 1) * 8 + x - 1;
}

static u8 random_byte(void)
{
    u16 x;
    x = rng_state;
    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    rng_state = x;
    return (u8)x;
}

static int random_range(int limit)
{
    if (limit <= 0) {
        return 0;
    }
    return (int)(random_byte() % limit);
}

static void print3(int value)
{
    if (value < 100) {
        putch('0');
    }
    if (value < 10) {
        putch('0');
    }
    print_num(value);
}

static int cell_klingons(int code)
{
    return code & 3;
}

static int cell_bases(int code)
{
    return (code >> 2) & 1;
}

static int cell_stars(int code)
{
    return ((code >> 3) & 7) + 1;
}

static int display_code(int code)
{
    return cell_klingons(code) * 100
        + cell_bases(code) * 10
        + cell_stars(code);
}

static void make_galaxy(void)
{
    int i;
    int r;
    int k;
    int b;
    int stars;

    do {
        total_klingons = 0;
        bases_left = 0;
        for (i = 0; i < QUADS; ++i) {
            r = random_byte();
            if (r < 5) {
                k = 3;
            } else if (r < 18) {
                k = 2;
            } else if (r < 58) {
                k = 1;
            } else {
                k = 0;
            }
            b = random_byte() < 10 ? 1 : 0;
            stars = (random_byte() & 7) + 1;
            galaxy[i] = (u8)(k | (b << 2) | ((stars - 1) << 3));
            total_klingons += k;
            bases_left += b;
            known[i] = 255;
        }
    } while (total_klingons == 0 || bases_left == 0);
    klingons_left = total_klingons;
}

static void find_empty(int *x, int *y)
{
    int tries;
    int i;

    for (tries = 0; tries < 200; ++tries) {
        *x = random_range(8) + 1;
        *y = random_range(8) + 1;
        if (board[sindex(*x, *y)] == TILE_EMPTY) {
            return;
        }
    }
    for (i = 0; i < SECTORS; ++i) {
        if (board[i] == TILE_EMPTY) {
            *x = i % 8 + 1;
            *y = i / 8 + 1;
            return;
        }
    }
    *x = 1;
    *y = 1;
}

static void update_quadrant(void)
{
    galaxy[qindex(qx, qy)] =
        (u8)(local_klingons | (local_bases << 2) | ((local_stars - 1) << 3));
}

static void load_quadrant(int ship_x, int ship_y)
{
    int i;
    int code;
    int x;
    int y;

    code = galaxy[qindex(qx, qy)];
    local_klingons = cell_klingons(code);
    local_bases = cell_bases(code);
    local_stars = cell_stars(code);
    for (i = 0; i < SECTORS; ++i) {
        board[i] = TILE_EMPTY;
    }
    for (i = 0; i < 3; ++i) {
        enemy_x[i] = 0;
        enemy_y[i] = 0;
        enemy_shields[i] = 0;
    }

    sx = ship_x;
    sy = ship_y;
    if (sx < 1 || sx > 8 || sy < 1 || sy > 8) {
        find_empty(&sx, &sy);
    }
    if (board[sindex(sx, sy)] != TILE_EMPTY) {
        find_empty(&sx, &sy);
    }
    board[sindex(sx, sy)] = TILE_SHIP;

    for (i = 0; i < local_klingons; ++i) {
        find_empty(&x, &y);
        enemy_x[i] = (u8)x;
        enemy_y[i] = (u8)y;
        enemy_shields[i] = 200;
        board[sindex(x, y)] = TILE_KLINGON;
    }
    for (i = 0; i < local_bases; ++i) {
        find_empty(&x, &y);
        board[sindex(x, y)] = TILE_BASE;
    }
    for (i = 0; i < local_stars; ++i) {
        find_empty(&x, &y);
        board[sindex(x, y)] = TILE_STAR;
    }
    update_quadrant();
}

static int docked(void)
{
    int x;
    int y;
    int xx;

    for (y = sy - 1; y <= sy + 1; ++y) {
        for (x = sx - 1; x <= sx + 1; ++x) {
            if (x >= 1 && x <= 8 && y >= 1 && y <= 8) {
                xx = sindex(x, y);
                if (board[xx] == TILE_BASE) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void check_docking(void)
{
    if (docked()) {
        energy = 3000;
        shields = 0;
        torpedoes = 10;
        print("STARBASE DOCKED: ENERGY AND TORPEDOES RESTORED");
        newline();
    }
}

static void print_sector(void)
{
    int x;
    int y;
    int tile;

    print("-=--=--=--=--=--=--=--=-");
    newline();
    for (y = 1; y <= 8; ++y) {
        for (x = 1; x <= 8; ++x) {
            tile = board[sindex(x, y)];
            if (tile == TILE_SHIP) {
                print("<*>");
            } else if (tile == TILE_KLINGON) {
                print("+++");
            } else if (tile == TILE_BASE) {
                print(">!<");
            } else if (tile == TILE_STAR) {
                print(" * ");
            } else {
                print("   ");
            }
        }
        if (y == 2) {
            print(" STARDATE ");
            print_num(stardate);
        } else if (y == 3) {
            print(" CONDITION ");
            if (docked()) {
                print("DOCKED");
            } else if (local_klingons > 0) {
                print("RED");
            } else if (energy < 300) {
                print("YELLOW");
            } else {
                print("GREEN");
            }
        } else if (y == 4) {
            print(" QUADRANT ");
            print_num(qx);
            putch(',');
            print_num(qy);
        } else if (y == 5) {
            print(" SECTOR   ");
            print_num(sx);
            putch(',');
            print_num(sy);
        } else if (y == 6) {
            print(" ENERGY    ");
            print_num(energy);
        } else if (y == 7) {
            print(" SHIELDS   ");
            print_num(shields);
        } else if (y == 8) {
            print(" PHOTON TORPEDOES ");
            print_num(torpedoes);
        }
        newline();
    }
    print("-=--=--=--=--=--=--=--=-");
    newline();
}

static void long_scan(void)
{
    int x;
    int y;
    int qx0;
    int code;
    int column;

    if (damage[2] < 0) {
        print("LONG RANGE SENSORS INOPERABLE");
        newline();
        return;
    }
    print("LONG RANGE SENSOR SCAN FOR QUADRANT ");
    print_num(qx);
    putch(',');
    print_num(qy);
    newline();
    print("-------------------");
    newline();
    for (y = qy - 1; y <= qy + 1; ++y) {
        for (column = 0; column < 3; ++column) {
            x = qx + column - 1;
            if (x < 1 || x > 8 || y < 1 || y > 8) {
                code = 0;
            } else {
                qx0 = qindex(x, y);
                code = display_code(galaxy[qx0]);
                known[qx0] = galaxy[qx0];
            }
            print("| ");
            print3(code);
            putch(' ');
        }
        putch('|');
        newline();
        print("-------------------");
        newline();
    }
}

static void save_klingon_kill(int slot)
{
    board[sindex(enemy_x[slot], enemy_y[slot])] = TILE_EMPTY;
    enemy_shields[slot] = 0;
    enemy_x[slot] = 0;
    enemy_y[slot] = 0;
    --local_klingons;
    --klingons_left;
    update_quadrant();
    print("*** KLINGON DESTROYED ***");
    newline();
    if (klingons_left == 0) {
        print("THE FEDERATION HAS BEEN SAVED!");
        newline();
        game_over = 1;
    }
}

static void enemy_attack(void)
{
    int i;
    int dx;
    int dy;
    int distance;
    int hit;

    for (i = 0; i < 3; ++i) {
        if (enemy_shields[i] > 0) {
            dx = (int)enemy_x[i] - sx;
            dy = (int)enemy_y[i] - sy;
            if (dx < 0) {
                dx = -dx;
            }
            if (dy < 0) {
                dy = -dy;
            }
            distance = dx + dy;
            if (distance < 1) {
                distance = 1;
            }
            hit = (random_range(80) + 20) / distance;
            if (hit < 1) {
                hit = 1;
            }
            print("KLINGON HIT: ");
            print_num(hit);
            print(" UNITS");
            newline();
            if (shields >= hit) {
                shields -= hit;
            } else {
                energy -= hit - shields;
                shields = 0;
            }
            if (energy <= 0 && shields <= 0) {
                energy = 0;
                print("THE ENTERPRISE HAS BEEN DESTROYED");
                newline();
                game_over = 1;
                return;
            }
        }
    }
}

static void advance_time(void)
{
    int i;

    ++stardate;
    for (i = 0; i < 8; ++i) {
        if (damage[i] < 0) {
            ++damage[i];
        }
    }
    if (random_range(5) == 0) {
        i = random_range(8);
        if (random_range(2) == 0) {
            damage[i] -= random_range(4) + 1;
            print("DAMAGE: ");
            print(device_names[i]);
            newline();
        } else if (damage[i] < 0) {
            ++damage[i];
            print("REPAIRS IMPROVED: ");
            print(device_names[i]);
            newline();
        }
    }
}

static void move_ship(void)
{
    int course;
    int warp;
    int steps;
    int i;
    int nx;
    int ny;
    int nqx;
    int nqy;
    int tile;

    print("COURSE (1-8, 0 CANCEL) ");
    course = read_number();
    if (course == 0) {
        return;
    }
    if (course < 1 || course > 8) {
        print("INVALID COURSE");
        newline();
        return;
    }
    print("WARP FACTOR (0-8) ");
    warp = read_number();
    if (warp < 0 || warp > 8) {
        print("INVALID WARP FACTOR");
        newline();
        return;
    }
    if (damage[0] < 0 && warp > 0) {
        print("WARP ENGINES DAMAGED");
        newline();
        return;
    }
    if (energy <= 0 || warp == 0) {
        return;
    }

    steps = warp * 8;
    board[sindex(sx, sy)] = TILE_EMPTY;
    for (i = 0; i < steps; ++i) {
        board[sindex(sx, sy)] = TILE_EMPTY;
        nx = sx + course_dx[course];
        ny = sy + course_dy[course];
        nqx = qx;
        nqy = qy;
        if (nx < 1) {
            nx = 8;
            --nqx;
        } else if (nx > 8) {
            nx = 1;
            ++nqx;
        }
        if (ny < 1) {
            ny = 8;
            --nqy;
        } else if (ny > 8) {
            ny = 1;
            ++nqy;
        }
        if (nqx < 1 || nqx > 8 || nqy < 1 || nqy > 8) {
            print("GALACTIC EDGE REACHED");
            newline();
            break;
        }
        if (nqx != qx || nqy != qy) {
            update_quadrant();
            qx = nqx;
            qy = nqy;
            sx = nx;
            sy = ny;
            load_quadrant(sx, sy);
            check_docking();
        } else {
            tile = board[sindex(nx, ny)];
            if (tile != TILE_EMPTY) {
                print("WARP ENGINES STOPPED: OBSTRUCTION");
                newline();
                break;
            }
            sx = nx;
            sy = ny;
            board[sindex(sx, sy)] = TILE_SHIP;
        }
    }
    board[sindex(sx, sy)] = TILE_SHIP;
    energy -= steps + 5;
    if (energy < 0) {
        energy = 0;
    }
    advance_time();
    if (stardate > deadline) {
        print("TIME HAS EXPIRED. THE FEDERATION IS CONQUERED.");
        newline();
        game_over = 1;
        return;
    }
    update_quadrant();
    if (local_klingons > 0) {
        enemy_attack();
    }
}

static void fire_phasers(void)
{
    int units;
    int share;
    int i;
    int hit;

    if (local_klingons == 0) {
        print("NO KLINGONS IN THIS QUADRANT");
        newline();
        return;
    }
    if (damage[3] < 0) {
        print("PHASER CONTROL INOPERABLE");
        newline();
        return;
    }
    print("ENERGY AVAILABLE ");
    print_num(energy);
    newline();
    print("UNITS TO FIRE (0 CANCEL) ");
    units = read_number();
    if (units <= 0 || units > energy) {
        return;
    }
    energy -= units;
    share = units / local_klingons;
    for (i = 0; i < 3; ++i) {
        if (enemy_shields[i] > 0) {
            hit = share + random_range(share + 1);
            enemy_shields[i] -= hit;
            print_num(hit);
            print(" UNIT HIT ON KLINGON AT ");
            print_num(enemy_x[i]);
            putch(',');
            print_num(enemy_y[i]);
            newline();
            if (enemy_shields[i] <= 0) {
                save_klingon_kill(i);
                if (game_over) {
                    return;
                }
            }
        }
    }
    if (!game_over) {
        enemy_attack();
    }
}

static void fire_torpedo(void)
{
    int course;
    int x;
    int y;
    int i;
    int tile;

    if (damage[4] < 0) {
        print("PHOTON TUBES INOPERABLE");
        newline();
        return;
    }
    if (torpedoes == 0) {
        print("NO TORPEDOES REMAIN");
        newline();
        return;
    }
    print("TORPEDO COURSE (1-8, 0 CANCEL) ");
    course = read_number();
    if (course == 0 || course < 1 || course > 8) {
        return;
    }
    --torpedoes;
    x = sx;
    y = sy;
    for (i = 0; i < 8; ++i) {
        x += course_dx[course];
        y += course_dy[course];
        if (x < 1 || x > 8 || y < 1 || y > 8) {
            print("TORPEDO MISSED");
            newline();
            enemy_attack();
            return;
        }
        tile = board[sindex(x, y)];
        if (tile == TILE_KLINGON) {
            for (i = 0; i < 3; ++i) {
                if (enemy_x[i] == x && enemy_y[i] == y) {
                    save_klingon_kill(i);
                    break;
                }
            }
            return;
        }
        if (tile == TILE_BASE) {
            board[sindex(x, y)] = TILE_EMPTY;
            --local_bases;
            --bases_left;
            update_quadrant();
            print("STARBASE DESTROYED");
            newline();
            enemy_attack();
            return;
        }
        if (tile == TILE_STAR) {
            print("TORPEDO HIT A STAR");
            newline();
            enemy_attack();
            return;
        }
    }
    print("TORPEDO MISSED");
    newline();
    enemy_attack();
}

static void set_shields(void)
{
    int units;
    int available;

    if (damage[6] < 0) {
        print("SHIELD CONTROL INOPERABLE");
        newline();
        return;
    }
    available = energy + shields;
    print("ENERGY AVAILABLE ");
    print_num(available);
    newline();
    print("UNITS TO SHIELDS (0 CANCEL) ");
    units = read_number();
    if (units <= 0 || units > available) {
        return;
    }
    energy = available - units;
    shields = units;
}

static void damage_report(void)
{
    int i;

    if (damage[5] < 0) {
        print("DAMAGE CONTROL INOPERABLE");
        newline();
        return;
    }
    print("DEVICE                 REPAIR");
    newline();
    for (i = 0; i < 8; ++i) {
        print(device_names[i]);
        print(" ");
        print_num(damage[i]);
        newline();
    }
}

static void status_report(void)
{
    print("KLINGONS LEFT ");
    print_num(klingons_left);
    print("  STARDATES LEFT ");
    print_num(deadline - stardate);
    print("  STARBASES LEFT ");
    print_num(bases_left);
    newline();
    print("ENERGY ");
    print_num(energy);
    print("  SHIELDS ");
    print_num(shields);
    print("  TORPEDOES ");
    print_num(torpedoes);
    newline();
}

static void galaxy_record(void)
{
    int x;
    int y;
    int code;

    print("GALACTIC RECORD (??? = NOT SCANNED)");
    newline();
    for (y = 1; y <= 8; ++y) {
        for (x = 1; x <= 8; ++x) {
            code = known[qindex(x, y)];
            if (code == 255) {
                print("??? ");
            } else {
                print3(display_code(code));
                putch(' ');
            }
        }
        newline();
    }
}

static void computer(void)
{
    int option;
    int i;
    int dx;
    int dy;
    int course;
    int best;

    if (damage[7] < 0) {
        print("LIBRARY COMPUTER DISABLED");
        newline();
        return;
    }
    print("COMPUTER: 0 GALACTIC RECORD, 1 STATUS, 2 TORPEDO DATA ");
    option = read_number();
    if (option == 0) {
        galaxy_record();
    } else if (option == 1) {
        status_report();
        damage_report();
    } else if (option == 2) {
        for (i = 0; i < 3; ++i) {
            if (enemy_shields[i] > 0) {
                dx = (int)enemy_x[i] - sx;
                dy = sy - (int)enemy_y[i];
                course = 1;
                best = 100;
                if (dx > 0 && dy == 0) {
                    course = 1;
                } else if (dx > 0 && dy > 0) {
                    course = 2;
                } else if (dx == 0 && dy > 0) {
                    course = 3;
                } else if (dx < 0 && dy > 0) {
                    course = 4;
                } else if (dx < 0 && dy == 0) {
                    course = 5;
                } else if (dx < 0 && dy < 0) {
                    course = 6;
                } else if (dx == 0 && dy < 0) {
                    course = 7;
                } else if (dx > 0 && dy < 0) {
                    course = 8;
                }
                if (dx < 0) {
                    dx = -dx;
                }
                if (dy < 0) {
                    dy = -dy;
                }
                best = dx > dy ? dx : dy;
                print("KLINGON ");
                print_num(i + 1);
                print(" SECTOR ");
                print_num(enemy_x[i]);
                putch(',');
                print_num(enemy_y[i]);
                print(" COURSE ");
                print_num(course);
                print(" DISTANCE ");
                print_num(best);
                newline();
            }
        }
    } else {
        print("INVALID COMPUTER OPTION");
        newline();
    }
}

static void instructions(void)
{
    static const char *lines[] = {
        "     INSTRUCTIONS:",
        "",
        "<*> = ENTERPRISE",
        "+++ = KLINGON",
        ">!< = STARBASE",
        " *  = STAR",
        "",
        "COMMAND 0 = WARP ENGINE CONTROL",
        "  'COURSE IS IN A CIRCULAR NUMERICAL         4    3    2",
        "  VECTOR ARRANGEMENT AS SHOWN.                `.  :  .'",
        "  INTERGER AND REAL VALUES MAY BE               `.:.'",
        "  USED.  THEREFORE COURSE 1.5 IS             5---<*>---1",
        "  HALF WAY BETWEEN 1 AND 2.                     .':`.",
        "                                              .'  :  `.",
        "  A VECTOR OF 9 IS UNDEFINED, BUT            6    7    8",
        "  VALUES MAY APPROACH 9.",
        "                                               COURSE",
        "  ONE 'WARP FACTOR' IS THE SIZE OF",
        "  ONE QUADRANT.  THEREFORE TO GET FROM",
        "  QUADRANT 5,6 TO 5,5 YOU WOULD USE COURSE 3, WARP",
        "  FACTOR 1. COORDINATES ARE SPECIFIED USING X,Y NOTATION",
        "  WITH X 1-8 FROM LEFT-RIGHT AND Y 1-8 FROM TOP-BOTTOM.",
        "",
        "COMMAND 1 = SHORT RANGE SENSOR SCAN",
        "  PRINTS THE QUADRANT YOU ARE CURRENTLY IN, INCLUDING",
        "  STARS, KLINGONS, STARBASES, AND THE ENTERPRISE; ALONG",
        "  WITH OTHER PERTINATE INFORMATION.",
        "",
        "COMMAND 2 = LONG RANGE SENSOR SCAN",
        "  SHOWS CONDITIONS IN SPACE FOR ONE QUADRANT ON EACH SIDE",
        "  OF THE ENTERPRISE IN THE MIDDLE OF THE SCAN.  THE SCAN",
        "  IS CODED IN THE FORM XXX, WHERE THE UNITS DIGIT IS THE",
        "  NUMBER OF STARS, THE TENS DIGIT IS THE NUMBER OF STAR-",
        "  BASES, THE HUNDREDS DIGIT IS THE NUMBER OF KLINGONS.",
        "",
        "COMMAND 3 = PHASER CONTROL",
        "  ALLOWS YOU TO DESTROY THE KLINGONS BY HITTING HIM WITH",
        "  SUITABLY LARGE NUMBERS OF ENERGY UNITS TO DEPLETE HIS ",
        "  SHIELD POWER.  KEEP IN MIND THAT WHEN YOU SHOOT AT",
        "  HIM, HE GONNA DO IT TO YOU TOO.",
        "",
        "COMMAND 4 = PHOTON TORPEDO CONTROL",
        "  COURSE IS THE SAME AS USED IN WARP ENGINE CONTROL",
        "  IF YOU HIT THE KLINGON, HE IS DESTROYED AND CANNOT FIRE",
        "  BACK AT YOU.  IF YOU MISS, HE WILL SHOOT HIS PHASERS AT",
        "  YOU.",
        "   NOTE: THE LIBRARY COMPUTER (COMMAND 7) HAS AN OPTION",
        "   TO COMPUTE TORPEDO TRAJECTORY FOR YOU (OPTION 2).",
        "",
        "COMMAND 5 = SHIELD CONTROL",
        "  DEFINES NUMBER OF ENERGY UNITS TO ASSIGN TO SHIELDS",
        "  ENERGY IS TAKEN FROM TOTAL SHIP'S ENERGY.",
        "",
        "COMMAND 6 = DAMAGE CONTROL REPORT",
        "  GIVES STATE OF REPAIRS OF ALL DEVICES.  A STATE OF REPAIR",
        "  LESS THAN ZERO SHOWS THAT THAT DEVICE IS TEMPORARALY",
        "  DAMAGED.",
        "",
        "COMMAND 7 = LIBRARY COMPUTER",
        "  THE LIBRARY COMPUTER CONTAINS THREE OPTIONS:",
        "    OPTION 0 = CUMULATIVE GALACTIC RECORD",
        "     SHOWS COMPUTER MEMORY OF THE RESULTS OF ALL PREVIOUS",
        "     LONG RANGE SENSOR SCANS",
        "    OPTION 1 = STATUS REPORT",
        "     SHOWS NUMBER OF KLINGONS, STARDATES AND STARBASES",
        "     LEFT.",
        "    OPTION 2 = PHOTON TORPEDO DATA",
        "     GIVES TRAJECTORY AND DISTANCE BETWEEN THE ENTERPRISE",
        "     AND ALL KLINGONS IN YOUR QUADRANT",
        0
    };
    int i;

    newline();
    for (i = 0; lines[i] != 0; ++i) {
        print(lines[i]);
        newline();
    }
    newline();
}

static void play(void)
{
    int command;

    for (;;) {
        if (game_over) {
            return;
        }
        check_docking();
        if (energy <= 0 && shields <= 0) {
            print("THE ENTERPRISE IS DEAD IN SPACE");
            newline();
            game_over = 1;
            return;
        }
        print_sector();
        if (local_klingons > 0) {
            print("CONDITION RED");
            newline();
        }
        print("COMMAND (0-7) ");
        command = read_number();
        if (command == 0) {
            move_ship();
        } else if (command == 1) {
            if (damage[1] < 0) {
                print("SHORT RANGE SENSORS INOPERABLE");
                newline();
            } else {
                print_sector();
            }
        } else if (command == 2) {
            long_scan();
        } else if (command == 3) {
            fire_phasers();
        } else if (command == 4) {
            fire_torpedo();
        } else if (command == 5) {
            set_shields();
        } else if (command == 6) {
            damage_report();
        } else if (command == 7) {
            computer();
        } else {
            print("ENTER A COMMAND FROM 0 TO 7");
            newline();
        }
    }
}

static void initialize_game(int seed)
{
    int i;
    int q;
    int s;

    rng_state = (u16)(seed < 0 ? -seed : seed);
    if (rng_state == 0) {
        rng_state = 0xACE1;
    }
    make_galaxy();
    for (i = 0; i < 8; ++i) {
        damage[i] = 0;
    }
    energy = 3000;
    shields = 0;
    torpedoes = 10;
    stardate = 2000 + random_range(20) * 100;
    deadline = stardate + 3000;
    q = random_range(64);
    qx = q % 8 + 1;
    qy = q / 8 + 1;
    s = random_range(64);
    sx = s % 8 + 1;
    sy = s / 8 + 1;
    game_over = 0;
    load_quadrant(sx, sy);
}

int main(void)
{
    int choice;
    int seed;

    print("STAR TREK");
    newline();
    newline();
    newline();
    newline();
    print("ENTER 1 FOR INSTRUCTIONS OR 2 TO PLAY ");
    choice = read_number();
    while (choice != 1 && choice != 2) {
        print("ENTER 1 FOR INSTRUCTIONS OR 2 TO PLAY ");
        choice = read_number();
    }
    if (choice == 1) {
        instructions();
    }
    newline();
    print("SEED NUMBER ");
    seed = read_number();
    initialize_game(seed);
    print("DESTROY ");
    print_num(klingons_left);
    print(" KLINGONS IN 30 STARDATES. STARBASES: ");
    print_num(bases_left);
    newline();
    play();
    print("GAME OVER - RESET TO PLAY AGAIN");
    newline();
    return 0;
}
