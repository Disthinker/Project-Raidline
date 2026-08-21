# Project Raidline P0 Audio Sources

This package was authorized by the user on 2026-08-21. The user confirmed
that the source packs are free or purchased for project use. This file records
provenance; it is not a redistribution license.

Runtime files are 48 kHz, mono, 16-bit PCM WAV. The deterministic processing
recipe and every exact ZIP entry are recorded in
`tools/audio_pipeline/build_p0_audio.ps1`. Whole archives are never copied into
the game repository.

## Source archives

- `E:/WorkPlace/Projects/ArtWorkbench/07_音效候选/freeweaponsounds.zip`
  - Three handgun shots and three assault-rifle shots.
  - Handgun/rifle magazine-out, magazine-in, chamber/bolt and equip Foley.
  - One handgun and one assault-rifle outdoor tail.
- `Sonniss.com-GDC2026-GameAudioBundle1of5.zip`
  - 344 Audio East Coast America electricity hum.
  - 344 Audio Cinematic Fight four body impacts.
- `Sonniss.com-GDC2026-GameAudioBundle2of5.zip`
  - Epic Stock Media HD Lock and Mechanism small mechanism/latch/spring Foley.
  - Epic Stock Media Humanoid Creatures weak breath and restrained death
    vocalization source elements.
- `Sonniss.com-GDC2026-GameAudioBundle3of5.zip`
  - InMotionAudio T-Shirt cloth Foley, instrument-case Foley and medical
    thermometer button/beep.
- `Sonniss.com-GDC2026-GameAudioBundle5of5.zip`
  - SoundBits short violent-humanoid exhale.
  - The Noisery wind-through-metal source from City Rain.

The Sonniss archives contain their original `License - GDC Game Audio.pdf` and
`Readme.txt`. Those documents remain in ArtWorkbench and are not copied into
the runtime package.

## Processing decisions

- Resample and downmix all selected clips to 48 kHz mono PCM.
- Apply per-family loudness normalization and true-peak limits.
- Trim the four-impact source into restrained non-verbal player damage and
  enemy-impact clips; no protagonist voice identity is established here.
- Produce the second infected variants with small pitch changes; avoid anime,
  fantasy, alien and exaggerated monster sources.
- Build 18-second Base and Raid loops from 20-second bodies with a two-second
  head/tail crossfade.
- Keep shotgun, suppressor, grenade, radio, voiced crowd, storm/rain, vehicle,
  door and other environment families deferred.

No PNG, character art, enemy art, weapon art, map art or attack animation was
published or modified by this package. `art/ASSET_MANIFEST.yaml` is unchanged.
