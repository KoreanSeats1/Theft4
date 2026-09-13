#!/usr/bin/env python3
"""Package the supplied Apple and Android PNGs into checked-in launcher assets.

ImageMagick performs image conversion; Python calculates platform sizes and
writes asset metadata. Normal app builds do not need ImageMagick.
"""
from __future__ import annotations

import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

from generate_desktop_icons import png_dimensions, run


def generate(apple: Path, android: Path, repository: Path) -> dict:
    apple, android = apple.resolve(strict=True), android.resolve(strict=True)
    for source in (apple, android):
        width, height = png_dimensions(source)
        if width != height or width < 1024:
            raise ValueError(f'Supply a square PNG at least 1024 pixels wide: {source}')
    magick = shutil.which('magick')
    if not magick:
        raise RuntimeError('Regeneration requires ImageMagick.')
    outputs: list[Path] = []
    with tempfile.TemporaryDirectory(prefix='liberty-mobile-icons-') as temporary:
        stage = Path(temporary)

        def resize(source: Path, relative: Path, size: int, canvas: int | None = None) -> None:
            output = stage / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            command = [magick, str(source), '-colorspace', 'sRGB', '-filter', 'Lanczos',
                       '-resize', f'{size}x{size}']
            if canvas:
                command += ['-background', 'none', '-gravity', 'center',
                            '-extent', f'{canvas}x{canvas}']
            else:
                # iOS app icons must be opaque. The artwork's background is black.
                command += ['-background', 'black', '-alpha', 'remove', '-alpha', 'off',
                            '-type', 'TrueColor']
            run(command + ['-depth', '8', '-strip', '-define', 'png:exclude-chunks=date,time',
                           str(output)])
            expected = canvas or size
            if png_dimensions(output) != (expected, expected):
                raise RuntimeError(f'Incorrect generated size: {relative}')
            outputs.append(relative)

        for catalog in (Path('LibertyRecomp/res/ios/Assets.xcassets/AppIcon.appiconset'),
                        Path('os/ios/Assets.xcassets/AppIcon.appiconset')):
            metadata = json.loads((repository / catalog / 'Contents.json').read_text())
            for item in metadata['images']:
                item.setdefault('filename', 'icon_1024pt.png')
                size = Decimal(item['size'].split('x')[0])
                scale = Decimal(item.get('scale', '1x').removesuffix('x'))
                pixels = size * scale
                if pixels != int(pixels):
                    raise ValueError(f'Non-integral icon size: {item}')
                relative = catalog / item['filename']
                if relative not in outputs:
                    resize(apple, relative, int(pixels))
            (stage / catalog / 'Contents.json').write_text(json.dumps(metadata, indent=2) + '\n')
            outputs.append(catalog / 'Contents.json')

        resources = Path('os/android/app/src/main/res')
        for density, scale in (('mdpi', Decimal('1')), ('hdpi', Decimal('1.5')),
                               ('xhdpi', Decimal('2')), ('xxhdpi', Decimal('3')),
                               ('xxxhdpi', Decimal('4'))):
            legacy = int(48 * scale)
            foreground = int(66 * scale)
            canvas = int(108 * scale)
            for name in ('ic_launcher.png', 'ic_launcher_round.png'):
                resize(android, resources / f'mipmap-{density}' / name, legacy)
            resize(android, resources / f'drawable-{density}' / 'ic_launcher_artwork.png',
                   foreground, canvas)

        for relative, source in ((Path('LibertyRecomp/res/icons/apple.png'), apple),
                                  (Path('LibertyRecomp/res/icons/android.png'), android)):
            (stage / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, stage / relative)
            outputs.append(relative)
        # Install only after all conversions have succeeded.
        for relative in outputs:
            (repository / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(stage / relative, repository / relative)

    report = {'android_layer_dp': 108, 'android_artwork_dp': 66,
              'assets': {str(p): hashlib.sha256((repository / p).read_bytes()).hexdigest()
                         for p in outputs}}
    (repository / 'LibertyRecomp/res/icons/mobile_manifest.json').write_text(
        json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apple-source', type=Path, required=True)
    parser.add_argument('--android-source', type=Path, required=True)
    parser.add_argument('--repository', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    print(json.dumps(generate(args.apple_source, args.android_source, args.repository.resolve()), indent=2))
