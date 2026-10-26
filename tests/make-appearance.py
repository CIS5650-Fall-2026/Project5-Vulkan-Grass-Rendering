from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
for name in ('wind-controls', 'lighting'):
    paths = sorted((root / 'build/appearance' / name).glob('*.ppm'), key=lambda p: int(p.stem))
    if len(paths) != 30:
        raise RuntimeError(f'Expected 30 frames for {name}, got {len(paths)}')
    frames = []
    for path in paths:
        frame = Image.open(path).convert('RGB')
        if name == 'wind-controls':
            number = int(path.stem)
            text = 'Wind 6 / direction 0' if number < 60 else ('Wind 16 / direction 90' if number < 120 else 'Wind 2 / direction 180')
            draw = ImageDraw.Draw(frame)
            draw.rectangle((8, 8, 220, 32), fill=(25, 35, 25))
            draw.text((16, 15), text, fill=(240, 240, 230))
        frames.append(frame)
    paletteImage = Image.new('RGB', (160, 120 * len(frames)))
    for i, frame in enumerate(frames):
        paletteImage.paste(frame.resize((160, 120)), (0, i * 120))
    palette = paletteImage.quantize(colors=256)
    frames = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
    frames[0].save(root / 'img' / f'{name}.gif', save_all=True, append_images=frames[1:],
                   duration=100 if name == 'wind-controls' else [70, 60, 70] * 10,
                   loop=0, optimize=False, disposal=2)
Image.open(root / 'build/appearance/lighting/176.ppm').save(root / 'img/lighting.png')
