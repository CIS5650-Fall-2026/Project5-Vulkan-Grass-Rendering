from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
for name, output in [(name, name + '.gif') for name in ('stage-static', 'stage-simulation', 'stage-culling')] + [('wind', 'wind.gif'), ('orbit', 'grass-demo.gif')]:
    paths = sorted((root / 'build/captures' / name).glob('*.ppm'), key=lambda p: int(p.stem))
    if len(paths) != 30:
        raise RuntimeError(f'Expected 30 captured frames for {name}, got {len(paths)}')
    frames = [Image.open(path).convert('RGB') for path in paths]
    swatches = Image.new('RGB', (160, 120 * len(frames)))
    for i, frame in enumerate(frames):
        swatches.paste(frame.resize((160, 120)), (0, i * 120))
    palette = swatches.quantize(colors=256)
    frames = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
    frames[0].save(root / 'img' / output, save_all=True, append_images=frames[1:],
                   duration=[70, 60, 70] * 10, loop=0, optimize=False, disposal=2)
for name, output in [('lod-fixed', 'lod-fixed.png'), ('lod-distance', 'lod-distance.png'), ('none', 'culling-off.png'), ('all', 'culling-on.png'), ('simulated', 'simulated-grass.png')]:
    Image.open(root / 'build/captures' / name / '119.ppm').save(root / 'img' / output)
