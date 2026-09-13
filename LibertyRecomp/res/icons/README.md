# App icons

The supplied artwork is preserved byte-for-byte:

| Source | Packaged uses |
| --- | --- |
| `apple.png` | macOS ICNS and both iOS app icon catalogs |
| `source.png` | Windows ICO, desktop SDL BMP, Linux PNG sizes |
| `android.png` | Android adaptive and legacy launcher icons |

Normal builds use the checked-in outputs. To regenerate them on macOS with
ImageMagick installed, run from the repository root:

```sh
python3 tools/generate_desktop_icons.py LibertyRecomp/res/icons/source.png \
  --apple-source LibertyRecomp/res/icons/apple.png
python3 tools/generate_mobile_icons.py \
  --apple-source LibertyRecomp/res/icons/apple.png \
  --android-source LibertyRecomp/res/icons/android.png
python3 tools/test_desktop_icons.py
```

`manifest.json` and `mobile_manifest.json` record output hashes. The Windows
resource script, macOS bundle plist, Android manifest and iOS asset catalogs
reference these generated assets directly.

iOS icons have an opaque black background and sizes matching the catalog entries.
The main catalog uses a single 1024-pixel source; the older catalog retains its
explicit iPhone/iPad variants. [Apple asset catalog configuration](https://developer.apple.com/documentation/xcode/configuring-your-app-icon),
[Apple icon opacity requirements](https://developer.apple.com/library/archive/qa/qa1686/_index.html).

Android adaptive foregrounds contain the supplied artwork scaled to 66 dp within
a 108 dp layer, with transparent padding and a black background layer. Launchers
apply their own mask. Density variants and legacy launcher images are checked in.
[Android adaptive icon guidance](https://developer.android.com/codelabs/basic-android-kotlin-compose-training-change-app-icon).
