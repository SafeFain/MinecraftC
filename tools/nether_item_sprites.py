"""Original binary-alpha bottle and smithing-template sprite structures."""


def sprite(template, shades):
    image = [(0, 0, 0, 0)] * 256
    if template == "smithing_template":
        for y in range(2, 14):
            for x in range(3, 13):
                edge = x in (3, 12) or y in (2, 13)
                rune = (x in (6, 9) and 5 <= y <= 10) or (y == 7 and 6 <= x <= 9)
                image[y * 16 + x] = shades[0 if edge else 3 if rune else 1]
        return image
    splash = template == "splash_potion"
    for y in range(2, 15):
        for x in range(3, 13):
            neck = 6 <= x <= 9 and 2 <= y <= 6
            body = (5 <= x <= 10 and y == 7) or (4 <= x <= 11 and 8 <= y <= 13) or (5 <= x <= 10 and y == 14)
            if not (neck or body):
                continue
            if neck:
                image[y * 16 + x] = (158, 111, 63, 255) if y == 2 else (173, 201, 213, 255)
            elif x in (4, 11) or y == 14:
                image[y * 16 + x] = (136, 178, 198, 255)
            elif y >= 10 and template != "empty_bottle":
                image[y * 16 + x] = shades[1 if x > 8 else 3]
            elif x in (5, 6):
                image[y * 16 + x] = (220, 238, 241, 255)
    if splash:
        for x, y in ((10, 2), (11, 3), (12, 4)):
            image[y * 16 + x] = (158, 111, 63, 255)
    return image
