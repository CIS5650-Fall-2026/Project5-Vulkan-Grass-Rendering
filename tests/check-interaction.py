from pathlib import Path
from PIL import Image, ImageChops
import struct, json, re
root = Path(__file__).resolve().parents[1]
folder = root / 'build/interaction'
a = Image.open(folder/'visible/119.ppm').convert('RGB')
b = Image.open(folder/'occluded/119.ppm').convert('RGB')
if ImageChops.difference(a, b).getbbox() is not None:
    raise RuntimeError('Occlusion changed visible pixels')
comparisons = []
def compare(reference, culled, repeat, label):
    def changed(left, right):
        diff = ImageChops.difference(Image.open(left), Image.open(right))
        data = diff.tobytes()
        return sum(any(data[i:i+3]) for i in range(0, len(data), 3))
    count = changed(reference, culled)
    noise = changed(reference, repeat)
    # Atomic compaction can change the winner of equal-depth grass fragments.
    # Keep the tolerance tiny (16 of 307,200 pixels) and record repeat-run noise.
    if count > 16 or noise > 16:
        raise RuntimeError(f'Image comparison exceeded depth-tie tolerance: {label}')
    comparisons.append({'view': label, 'changed_pixels': count, 'repeat_changed_pixels': noise})
for view in ('overview', 'distant'):
    compare(folder/f'{view}-visible/119.ppm', folder/f'{view}-occluded/119.ppm',
            folder/f'{view}-repeat/119.ppm', view)
for n in range(0,240,8):
    compare(folder/'moving-reference'/f'{n}.ppm', folder/'moving'/f'{n}.ppm',
            folder/'moving-repeat'/f'{n}.ppm', f'moving-{n}')
def blades(name):
    data = (folder/f'{name}.bin').read_bytes()
    return list(struct.iter_unpack('16f', data))
contact, plain = blades('visible'), blades('no-contact')
changed = sum(any(abs(x[i]-y[i]) > 1e-5 for i in (4,5,6,8,9,10)) for x,y in zip(contact,plain))
if changed == 0:
    raise RuntimeError('No collision response')
def visible_count(name):
    return int(re.search(r'Final visible blades: (\d+)', (folder/f'{name}.stdout.log').read_text()).group(1))
result = {'close_view_identical_pixels': True, 'compared_frames': 33, 'other_comparisons': comparisons, 'blades_affected_by_contact': changed,
          'input_blades': len(contact), 'visible_without_occlusion': visible_count('visible'), 'visible_with_occlusion': visible_count('occluded')}
(root/'performance/interaction-validation.json').write_text(json.dumps(result, indent=2)+'\n')
print("Interaction checks passed:", changed, "blades changed by collision; 33 image pairs checked")
a.save(root/'img/collision.png')
frames = [Image.open(folder/'moving'/f'{n}.ppm').convert('RGB') for n in range(0,240,8)]
swatches = Image.new('RGB',(160,120*len(frames)))
for i, frame in enumerate(frames): swatches.paste(frame.resize((160,120)), (0,i*120))
palette = swatches.quantize(colors=256)
frames = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
frames[0].save(root/'img/collision.gif', save_all=True, append_images=frames[1:], duration=[130,140,130]*10, loop=0, disposal=2)
