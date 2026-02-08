import pygame
import serial
import math
import time
from collections import deque


# ================= CONFIG =================
PORT = "COM4"
BAUD = 115200

WIDTH = 1000
HEIGHT = 650
FPS = 60
TIMEOUT = 5.0


# ================= COLORS =================
BG = (15, 25, 50)
PANEL = (28, 42, 85)

GREEN = (0, 255, 120)
GREEN_FOV = (0, 255, 120)
RED = (255, 80, 80)
LIGHT = (220, 235, 255)
GRAY = (140, 160, 200)


# ================= RADAR =================
RADAR_CENTER = (350, 325)
RADAR_RADIUS = 200
FOV_HALF = 15      # ±15°


# ================= SERIAL =================
ser = serial.Serial(PORT, BAUD, timeout=0.1)
time.sleep(2)
ser.reset_input_buffer()


# ================= PYGAME =================
pygame.init()
screen = pygame.display.set_mode((WIDTH, HEIGHT))
pygame.display.set_caption("UAV RSSI Tracker")

clock = pygame.time.Clock()

font = pygame.font.SysFont("consolas", 14)
big = pygame.font.SysFont("consolas", 20)
title = pygame.font.SysFont("consolas", 26, bold=True)


# ================= DATA =================
pan = 0
tilt = 0
rssi = -100
filtered = -100

alpha = 0.3

RSSI_BUF = deque(maxlen=8)
rssi_log = deque(maxlen=160)

# Best-angle memory
best_angle = 0
best_rssi = -100

last_data_time = 0
connected = False


# =====================================================
def status_text():

    global connected

    if last_data_time == 0:
        return "Waiting", GRAY

    if time.time() - last_data_time > TIMEOUT:
        connected = False
        rssi_log.clear()
        return "Scanning", (255, 200, 0)

    connected = True
    return "Connected", GREEN


# =====================================================
running = True

while running:

    clock.tick(FPS)


    # -------- Events --------
    for e in pygame.event.get():

        if e.type == pygame.QUIT:
            running = False


    # -------- SERIAL READ --------
    try:

        line = ser.readline().decode(errors="ignore").strip()

        if line and "PAN" in line and "TILT" in line and "RSSI" in line:

            # PAN 142° | TILT 14° | RSSI -50

            clean = line.replace("°", "").replace("|", "")
            parts = clean.split()

            pan  = float(parts[parts.index("PAN") + 1])
            tilt = float(parts[parts.index("TILT") + 1])
            rssi = float(parts[parts.index("RSSI") + 1])

            # Clamp pan (0–180)
            pan = max(0, min(180, pan))


            # ---------- Hybrid Filter ----------
            RSSI_BUF.append(rssi)

            avg = sum(RSSI_BUF) / len(RSSI_BUF)

            filtered = alpha * avg + (1 - alpha) * filtered


            # ---------- Adaptive Angle Tracking ----------
            LOCK_ALPHA = 0.15

            # First time init
            if best_angle == 0:
                best_angle = pan

            # Smooth follow
            best_angle = (1 - LOCK_ALPHA) * best_angle + LOCK_ALPHA * pan


            # ---------- Logging ----------
            rssi_log.append(filtered)

            last_data_time = time.time()

    except Exception as e:

        print("Serial error:", e)


    # -------- Use Stable Angle --------
    display_angle = best_angle


    # -------- DRAW --------
    screen.fill(BG)

    screen.blit(
        title.render("UAV RSSI Tracking", True, LIGHT),
        (20, 15)
    )


    # -------- Radar Panel --------
    pygame.draw.rect(
        screen, PANEL,
        (40, 70, 620, 510),
        0, 10
    )


    # Radar rings
    pygame.draw.circle(screen, GREEN, RADAR_CENTER, RADAR_RADIUS, 2)
    pygame.draw.circle(screen, GREEN, RADAR_CENTER, RADAR_RADIUS // 2, 1)


    # Grid
    for a in range(0, 360, 45):

        r = math.radians(a)

        x = RADAR_CENTER[0] + math.cos(r) * RADAR_RADIUS
        y = RADAR_CENTER[1] - math.sin(r) * RADAR_RADIUS

        pygame.draw.line(
            screen,
            (0, 130, 90),
            RADAR_CENTER,
            (x, y),
            1
        )


    # -------- FOV --------
    if connected:

        fov = pygame.Surface(
            (RADAR_RADIUS * 2, RADAR_RADIUS * 2),
            pygame.SRCALPHA
        )

        left  = max(0, display_angle - FOV_HALF)
        right = min(180, display_angle + FOV_HALF)

        start = math.radians(left)
        end   = math.radians(right)

        pts = [(RADAR_RADIUS, RADAR_RADIUS)]

        for i in range(41):

            a = start + (end - start) * i / 40

            x = RADAR_RADIUS + math.cos(a) * RADAR_RADIUS
            y = RADAR_RADIUS - math.sin(a) * RADAR_RADIUS

            pts.append((x, y))


        pygame.draw.polygon(
            fov,
            (*GREEN_FOV, 110),
            pts
        )

        screen.blit(
            fov,
            (RADAR_CENTER[0] - RADAR_RADIUS,
             RADAR_CENTER[1] - RADAR_RADIUS)
        )


    # -------- Radar Line & Target --------
    if connected:

        a = math.radians(display_angle)

        lx = RADAR_CENTER[0] + math.cos(a) * RADAR_RADIUS
        ly = RADAR_CENTER[1] - math.sin(a) * RADAR_RADIUS

        pygame.draw.line(
            screen,
            GREEN,
            RADAR_CENTER,
            (lx, ly),
            2
        )


        d = min(abs(filtered) * 2.5, RADAR_RADIUS)

        tx = RADAR_CENTER[0] + math.cos(a) * d
        ty = RADAR_CENTER[1] - math.sin(a) * d

        pygame.draw.circle(
            screen,
            RED,
            (int(tx), int(ty)),
            6
        )


    # -------- Info Panel --------
    pygame.draw.rect(
        screen, PANEL,
        (700, 90, 260, 260),
        0, 10
    )


    status, scol = status_text()

    info = [
        ("PAN", f"{pan:.1f}°"),
        ("TILT", f"{tilt:.1f}°"),
        ("RSSI", f"{rssi:.1f} dBm"),
        ("Filtered", f"{filtered:.1f} dBm"),
        ("Lock Dir", f"{best_angle:.1f}°"),
        ("Status", status)
    ]


    y = 120

    for k, v in info:

        screen.blit(
            font.render(k, True, GRAY),
            (720, y)
        )

        screen.blit(
            big.render(v, True, GREEN if k != "Status" else scol),
            (720, y + 18)
        )

        y += 38


    # -------- RSSI Graph --------
    gx, gy, gw, gh = 700, 380, 260, 180

    pygame.draw.rect(
        screen, PANEL,
        (gx, gy, gw, gh),
        0, 10
    )


    graph = pygame.Rect(
        gx + 10,
        gy + 30,
        gw - 20,
        gh - 45
    )

    pygame.draw.rect(screen, (0, 0, 0), graph)
    pygame.draw.rect(screen, (60, 80, 120), graph, 1)


    if len(rssi_log) > 2:

        pts = []

        for i, v in enumerate(rssi_log):

            v = max(-100, min(-30, v))

            x = graph.left + i * (graph.width / len(rssi_log))
            y = graph.bottom - ((v + 100) / 70) * graph.height

            pts.append((x, y))


        clip = screen.get_clip()

        screen.set_clip(graph)

        pygame.draw.lines(
            screen,
            GREEN,
            False,
            pts,
            2
        )

        screen.set_clip(clip)


    screen.blit(
        font.render("RSSI History", True, LIGHT),
        (gx + 12, gy + 6)
    )


    pygame.display.flip()


pygame.quit()
ser.close()